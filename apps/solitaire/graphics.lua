-- Jersey 25/OFL glyphs repacked losslessly from CrossPlay's 1-bit Epd cuts.
-- Load once; retain compact strings rather than per-glyph Lua tables.
local G={}
local fonts={}
function G.init()
 for _,size in ipairs({10,20,30}) do fonts[size]=assert(smudge.read_file('art/font'..size..'.bin',20000),'Missing CrossPlay font') end
end
local function record(font,c)
 c=math.max(32,math.min(90,c));return string.byte(font,(c-32)*7+1,(c-32)*7+7)
end
function G.width(text,size)
 local width=0
 for i=1,#text do local _,_,advance=record(fonts[size],text:byte(i));width=width+advance end
 return width
end
-- Native EpdFont measures ink bounds, including the initial zero origin,
-- rather than the final glyph advance. Needed by the original status button.
function G.inkWidth(text,size)
 local pen,left,right=0,0,0
 for i=1,#text do
  local w,_,advance,bearing=record(fonts[size],text:byte(i))
  if bearing>127 then bearing=bearing-256 end
  left=math.min(left,pen+bearing);right=math.max(right,pen+bearing+w);pen=pen+advance
 end
 return right-left
end
function G.fit(text,width,size)
 if G.inkWidth(text,size)<=width then return text end
 local low,high=0,#text
 while low<high do
  local mid=(low+high+1)//2
  if G.inkWidth(text:sub(1,mid)..'...',size)<width then low=mid else high=mid-1 end
 end
 return text:sub(1,low)..'...'
end
function G.status(text,b)
 -- Original FreeInkUI button: 4px horizontal/2px vertical text inset,
 -- toybox_20 line advance 42, ascender 34 and capital top 25.
 smudge.rect(b.x,b.y,b.w,b.h,true,true)
 G.text(text,b.x+4+(b.w-8-G.inkWidth(text,20))//2,
  b.y+2+math.max(0,(b.h-4-42)//2)+34-25,20,true)
end
function G.text(text,x,y,size,paper)
 local font=fonts[size];local cap=size==30 and 38 or size==20 and 25 or 13
 for i=1,#text do
  local w,h,advance,left,top,lo,hi=record(font,text:byte(i))
  if left>127 then left=left-256 end
  local offset=lo+hi*256
  if w>0 then smudge.bitmap(x+left,y+cap-top,w,h,font:sub(offset+1,offset+((w+7)//8)*h),paper or false) end
  x=x+advance
 end
end
function G.center(text,x,y,w,h,size,paper)
 G.text(text,x+(w-G.inkWidth(text,size))//2,y+(h-(size==30 and 38 or size==20 and 25 or 13))//2,size,paper)
end

-- The native CardArt rounded gray fill, row spans from the same circle radius.
function G.roundDither(x,y,w,h,r,dark)
 r=math.min(r,w//2,h//2)
 for row=0,h-1 do
  local dy=row<r and r-row or row>=h-r and row-(h-r-1) or 0
  local inset=dy>0 and r-math.floor(math.sqrt(r*r-dy*dy)) or 0
  smudge.rect_dither(x+inset,y+row,w-2*inset,1,dark)
 end
end
function G.slot(x,y,w,h)
 for xx=x+8,x+w-9,20 do local n=math.min(10,x+w-8-xx);smudge.rect(xx,y,n,2,true,true);smudge.rect(xx,y+h-2,n,2,true,true)end
 for yy=y+8,y+h-9,20 do local n=math.min(10,y+h-8-yy);smudge.rect(x,yy,2,n,true,true);smudge.rect(x+w-2,yy,2,n,true,true)end
end
function G.buttonText(text,b,paper)
 -- Actions must remain readable in narrow portrait controls, never ellipsized.
 local size=G.inkWidth(text,20)<=b.w-8 and 20 or 10
 local cap=size==20 and 25 or 13
 local top=size==20 and b.y+2+math.max(0,(b.h-4-42)//2)+9 or b.y+(b.h-cap)//2
 G.text(text,b.x+4+(b.w-8-G.inkWidth(text,size))//2,top,size,paper)
end
return G
