#!/usr/bin/env python3
"""Compile actual icon/rotate/pixel bodies; only renderer storage/SDK boundary is synthetic."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

R = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
cpp = (R / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()

def body(signature):
    start = cpp.index(signature)
    end = cpp.index('\n}\n', start) + 3
    return cpp[start:end]

methods = []
for name in ('drawIcon', 'drawIconInverted'):
    for signature in (f'void GfxRenderer::{name}(const uint8_t bitmap[], const int x, const int y, const int size)',
                      f'void GfxRenderer::{name}(const uint8_t bitmap[], const int x, const int y, const int width,'):
        methods.append(body(signature))
source = r'''
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "components/icons/cover.h"
#include "components/icons/book24.h"
#define LOG_ERR(...) ((void)0)
struct Cache { bool scan=false; bool isScanning(){return scan;} };
struct GfxRenderer {
 enum Orientation {Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise};
 enum RenderMode {BW, GRAYSCALE_LSB, GRAYSCALE_MSB};
 Orientation orientation=Portrait; RenderMode renderMode=BW;
 int panelWidth=128,panelHeight=96,panelWidthBytes=16;
 uint8_t* frameBuffer=nullptr; Cache* fontCacheManager_=nullptr;
 bool _stripActive=false; int _stripY0=0,_stripRows=0; uint8_t* _stripBuf=nullptr;
 int clipLeft_=0,clipTop_=0,clipRight_=32767,clipBottom_=32767;
 bool textClipActive_=false; int textClipLeft_=0,textClipTop_=0,textClipRight_=0,textClipBottom_=0;
 int getScreenWidth() const {return orientation==Portrait||orientation==PortraitInverted?panelHeight:panelWidth;}
 int getScreenHeight() const {return orientation==Portrait||orientation==PortraitInverted?panelWidth:panelHeight;}
 void drawPixel(int,int,bool) const;
 void drawIcon(const uint8_t[],int,int,int) const;
 void drawIcon(const uint8_t[],int,int,int,int) const;
 void drawIconInverted(const uint8_t[],int,int,int) const;
 void drawIconInverted(const uint8_t[],int,int,int,int) const;
};
'''
source += body('void rotateCoordinates(') + body('void GfxRenderer::drawPixel(') + '\n'.join(methods)
source += r'''
int main(){
 int checks=0, failures=0; int byOrientation[4][2]={};
 const uint8_t rectangle[]={0x61,0x80,0xFE,0x00,0x83,0x80,0x7F,0x80,0x41,0x00,0xDF,0x80,0xA5,0x00}; // 7 logical x 9 logical y, padded packed rows
 uint8_t transparent[128];memset(transparent,255,sizeof transparent);
 struct Asset {const uint8_t* p;int w,h;};
 const Asset assets[]={{CoverIcon,32,32},{Book24Icon,24,24},{rectangle,7,9},{transparent,32,32}};
 for(int o=0;o<4;++o)for(int inv=0;inv<2;++inv)for(int mode=0;mode<3;++mode)
 for(int context=0;context<5;++context)for(const auto& asset:assets){
  GfxRenderer g;g.orientation=static_cast<GfxRenderer::Orientation>(o);g.renderMode=static_cast<GfxRenderer::RenderMode>(mode);
  const int positions[][2]={{16,16},{17,19},{-3,9},{9,-4},{-40,-40},{g.getScreenWidth()-5,20},{20,g.getScreenHeight()-4},{g.getScreenWidth()+1,4}};
  for(const auto& pos:positions){
   std::array<uint8_t,1536> actual,expected,strip,expectedStrip;
   for(unsigned i=0;i<actual.size();++i)actual[i]=expected[i]=uint8_t(i*37+91);
   strip.fill(0xA5);expectedStrip=strip;
   g.frameBuffer=actual.data();g._stripBuf=strip.data();g._stripActive=context==1;g._stripY0=13;g._stripRows=11;
   g.clipLeft_=context==2?20:0;g.clipTop_=context==2?21:0;g.clipRight_=context==2?35:32767;g.clipBottom_=context==2?37:32767;
   g.textClipActive_=context==3;g.textClipLeft_=22;g.textClipTop_=23;g.textClipRight_=39;g.textClipBottom_=41;
   Cache cache;cache.scan=context==4;g.fontCacheManager_=&cache;
   GfxRenderer oracle=g;oracle.frameBuffer=expected.data();oracle._stripBuf=expectedStrip.data();
   if(inv)g.drawIconInverted(asset.p,pos[0],pos[1],asset.w,asset.h);
   else g.drawIcon(asset.p,pos[0],pos[1],asset.w,asset.h);
   if(!cache.scan)for(int lx=0;lx<asset.w;++lx)for(int ly=0;ly<asset.h;++ly){
    // Independent logical-coordinate lookup in the existing portrait-packed asset.
    int index=(asset.w-1-lx)*((asset.h+7)/8)+ly/8;
    if(!(asset.p[index]&(128>>(ly%8))))oracle.drawPixel(pos[0]+lx,pos[1]+ly,!inv);
   }
   ++checks;
   if(actual!=expected||strip!=expectedStrip){++failures;++byOrientation[o][inv];if(failures<12)printf("FAIL icon o=%d inverted=%d mode=%d context=%d position=%d,%d asset=%dx%d\n",o,inv,mode,context,pos[0],pos[1],asset.w,asset.h);}
   // The square wrapper must dispatch to the identical production rectangle body.
   if(asset.w==asset.h){
    actual=expected;strip=expectedStrip;
    if(inv)g.drawIconInverted(asset.p,pos[0],pos[1],asset.w);else g.drawIcon(asset.p,pos[0],pos[1],asset.w);
    ++checks;if(actual!=expected||strip!=expectedStrip)++failures;
   }
  }
 }
 for(int o=0;o<4;++o)for(int inv=0;inv<2;++inv)printf("orientation=%d inverted=%d failed_rectangles=%d\n",o,inv,byOrientation[o][inv]);
 printf("icon cases=%d failures=%d\n",checks,failures);
 if(failures)return 1;
 puts("PASS production icon orientation: four rotations, packed assets, rectangle/padding, transparency, clipping, strips, modes, scanning, square overloads");
}
'''
with tempfile.TemporaryDirectory(prefix='icon-probe-', dir=os.environ.get('TMPDIR', '/root/.hermes/cache/scratch')) as d:
    d = Path(d)
    (d / 'probe.cpp').write_text(source)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if a.sanitize else []
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                    '-I' + str(R / 'src'), str(d / 'probe.cpp'), '-o', str(d / 'probe')], check=True)
    subprocess.run([str(d / 'probe')], check=True)
