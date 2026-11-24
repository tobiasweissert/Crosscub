local ok,device=pcall(smudge.get_device)
if not ok or device~='sticky'then return dofile('legacy_graphics.lua')end
-- Original Jersey masks; strings load once, only on PSRAM-class Lua heaps.
local G={};local fonts={};local steps={};local metrics={[10]={cap=13,asc=17,line=21},[14]={cap=18,asc=24,line=29},[20]={cap=25,asc=34,line=42},[30]={cap=38,asc=51,line=63},[44]={cap=57,asc=74,line=92}};local caps={[10]=13,[14]=18,[20]=25,[30]=38}
function G.init()for _,size in ipairs({10,14,20,30})do fonts[size]=assert(smudge.read_file('art/toybox_'..size..'.bin',65536))end end
local function record(font,c)c=math.max(32,math.min(255,c));return font:byte((c-32)*7+1,(c-32)*7+7)end
function G.width(text,size)local w=0;for i=1,#text do local _,_,advance=record(fonts[size],text:byte(i));w=w+advance end;return w end
function G.text(text,x,y,size,paper)
 text=tostring(text);local font=fonts[size]
 for i=1,#text do local w,h,advance,left,top,lo,hi=record(font,text:byte(i));if left>127 then left=left-256 end;if top>127 then top=top-256 end
  local offset=lo+hi*256;if w>0 and h>0 then smudge.bitmap(x+left,y+caps[size]-top,w,h,font:sub(offset+1,offset+((w+7)//8)*h),paper or false)end;x=x+advance
 end
end
function G.center(text,x,y,w,h,size,paper)G.text(text,x+(w-G.width(text,size))//2,y+(h-caps[size])//2,size,paper)end
function G.fit(text,width,size)while #text>0 and G.width(text,size)>width do text=text:sub(1,-2)end;return text end
function G.box(x,y,w,h,fill,white,stroke,r)
 smudge.rounded_rect(x,y,w,h,r or 8,fill or false,not white,stroke or 1)
end
function G.label(text,x,y,w,h,size,white)
 text=tostring(text);size=size or 20
 for _,cut in ipairs({size,14,10})do if G.width(text,cut)<=w-8 then return G.center(text,x,y,w,h,cut,white)end end
 G.center(G.fit(text,w-8,10),x,y,w,h,10,white)
end
function G.wrap(text,x,y,w,size,leading)
 local line='';for word in text:gmatch('%S+')do local test=line==''and word or line..' '..word
  if G.width(test,size)>w and line~=''then G.text(line,x,y,size);y=y+leading;line=word else line=test end
 end;if line~=''then G.text(line,x,y,size);y=y+leading end;return y
end
function G.nativeWidth(text,size)
 local pen,left,right=0,0,0
 for i=1,#text do
  local w,_,advance,bearing=record(fonts[size],text:byte(i))
  if bearing>127 then bearing=bearing-256 end
  left=math.min(left,pen+bearing);right=math.max(right,pen+bearing+w)
  pen=pen+(steps[size] and i<#text and steps[size]:byte((text:byte(i)-32)*95+text:byte(i+1)-31)or advance)
 end
 return right-left
end
local function truncDiv(value,divisor)
 local q=math.abs(value)//math.abs(divisor);return (value<0)~=(divisor<0)and -q or q
end
function G.native(text,b,size,align,paper,lines,centered)
 -- Original UI-count cut has no U+2026 glyph: GfxRenderer::truncatedText uses three periods.
 if size==20 and (lines or 1)==1 and G.nativeWidth(text,size)>b.w then
  local lo,hi=0,#text
  while lo<hi do local mid=(lo+hi+1)//2
   if G.nativeWidth(text:sub(1,mid)..'...',size)<b.w then lo=mid else hi=mid-1 end
  end
  text=text:sub(1,lo)..'...'
 end
 local m=metrics[size];local output={}
 if lines and lines>1 and G.nativeWidth(text,size)>b.w then
  local line=''
  for word in text:gmatch('%S+')do
   local next=line==''and word or line..' '..word
   if line~=''and G.nativeWidth(next,size)>b.w then output[#output+1]=line;line=word else line=next end
  end
  if line~=''then output[#output+1]=line end
 else output[1]=text end
 local y=b.y+(centered and truncDiv(b.h-m.line,2)or math.max(0,(b.h-#output*m.line)//2))
 for n=1,math.min(lines or 1,#output)do
  local run=output[n];local width=G.nativeWidth(run,size);local x=b.x
  if align=='center'then x=b.x+math.max(0,(b.w-width)//2)elseif align=='right'then x=b.x+math.max(0,b.w-width)end
  local py=y
  if align=='center'and #run==1 and run:match('%d')then
   local w,h,_,bearing,top=record(fonts[size],run:byte());if bearing>127 then bearing=bearing-256 end
   if w<=b.w and h<=m.line then
    x=b.x+(b.w-w)//2-bearing
    -- centredText hands the target a complete line box; ordinary text uses its original box.
    local by=centered and y or b.y;local bh=centered and m.line or b.h
    py=by+(bh-h)//2+top-m.asc
   end
  end
  G.text(run,x,py+m.asc-m.cap,size,paper)
  y=y+m.line
 end
end

function G.nativeLabel(text,b,cut,align,paper,font)
 local c=metrics[cut];G.native(tostring(text),{x=b.x,y=b.y+(b.h+c.cap)//2-c.asc,w=b.w,h=c.line},font or cut,align or 'left',paper)
end
function G.capsLabel(text,b,size,align,paper)
 text=tostring(text);size=size or 20
 for _,candidate in ipairs({size,14,10})do if G.nativeWidth(text,candidate)<=b.w then size=candidate;break end end
 local x=b.x;local width=G.nativeWidth(text,size)
 if align=='right'then x=x+b.w-width elseif align~='left'then x=x+(b.w-width)//2 end
 G.text(text,x,b.y+(b.h-caps[size])//2,size,paper)
end
function G.button(text,b,fill,outline,paper)
 if fill~=nil then smudge.rect(b.x,b.y,b.w,b.h,true,fill)end
 if outline then smudge.rect(b.x,b.y,b.w,b.h,false,1,not paper)end
 local size=10
 for _,candidate in ipairs({20,14,10})do if G.nativeWidth(text,candidate)<=b.w-8 then size=candidate;break end end
 G.native(text,{x=b.x+4,y=b.y+2,w=b.w-8,h=b.h-4},size,'center',paper)
end
function G.roundDither(x,y,w,h,r,dark)
 r=math.min(r,w//2,h//2)
 for row=0,h-1 do
  local dy=row<r and r-row or row>=h-r and row-(h-r-1)or 0
  if h==2*r and(row==r-1 or row==r)then dy=0 end
  local inset=dy>0 and r-math.floor(math.sqrt(r*r-dy*dy))or 0
  smudge.rect_dither(x+inset,y+row,w-2*inset,1,dark)
 end
end

return G
