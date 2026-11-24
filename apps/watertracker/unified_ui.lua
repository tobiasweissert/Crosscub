-- Unified UI v1. App-local, stateless; uses existing CrossSmudge primitives only.
local U={}
local function valid_rect(r)
 if type(r)~='table' then return false end
 for _,key in ipairs({'x','y','w','h'}) do
  local n=r[key];if type(n)~='number' or n%1~=0 then return false end
 end
 local w,h=smudge.get_bounds()
 return r.x>=0 and r.y>=0 and r.w>0 and r.h>0 and r.x<=w-r.w and r.y<=h-r.h
end
function U.metrics()
 local w,h=smudge.get_bounds()
 return {w=w,h=h,pad=14,gap=8,header_h=64,body_top=78,
  body_bottom=h-68,footer_y=h-60,footer_h=48,landscape=w>h}
end
-- Valid UTF-8 in, valid UTF-8 out; native offline builds do not expose utf8.
function U.fit(text,width,font,bold)
 local s=tostring(text or ''):gsub('[%z\1-\31\127]',' ')
 font=font or 'ui10';width=math.max(0,math.floor(width))
 if smudge.text_width(s,font,bold or false)<=width then return s end
 local suffix='...'
 if smudge.text_width(suffix,font,bold or false)>width then return '' end
 local function boundary(n)
  while n>0 and s:byte(n+1) and s:byte(n+1)>=128 and s:byte(n+1)<192 do n=n-1 end
  return n
 end
 local lo,hi=0,#s
 while lo<hi do
  local mid=(lo+hi+1)//2
  local candidate=s:sub(1,boundary(mid))..suffix
  if smudge.text_width(candidate,font,bold or false)<=width then lo=mid else hi=mid-1 end
 end
 return s:sub(1,boundary(lo))..suffix
end
function U.text(x,y,text,font,bold,align,ink)
 if ink==nil then ink=true end
 smudge.text(x,y,tostring(text or ''),font or 'ui10',bold or false,align or 'left',ink)
end
function U.header(title,status)
 local m=U.metrics()
 local right=U.fit(status,math.floor((m.w-2*m.pad)*0.35),'ui10')
 local rw=smudge.text_width(right,'ui10',false)
 local left=U.fit(string.upper(tostring(title or '')),m.w-2*m.pad-(rw>0 and rw+m.gap or 0),'lexend16',true)
 smudge.rect(0,0,m.w,m.header_h,true,true)
 U.text(m.pad,(m.header_h-smudge.line_height('lexend16'))//2,left,'lexend16',true,'left',false)
 U.text(m.w-m.pad,(m.header_h-smudge.line_height('ui10'))//2,right,'ui10',false,'right',false)
 return left,right
end
function U.panel(r,filled)
 assert(valid_rect(r),'panel requires positive integer rect inside logical bounds')
 smudge.rect(r.x,r.y,r.w,r.h,true,filled==true)
 if not filled then smudge.rect(r.x,r.y,r.w,r.h,false,math.min(2,r.w,r.h),true) end
end
function U.button(label,r,filled,enabled)
 assert(valid_rect(r) and r.h>=smudge.line_height('ui10'),'button requires bounded integer rect and native line height')
 enabled=enabled~=false
 filled=filled==true and enabled
 if enabled then U.panel(r,filled) else
  smudge.rect(r.x,r.y,r.w,r.h,true,false)
  smudge.rect(r.x,r.y,r.w,r.h,false,1,true)
 end
 local font='ui10'
 local s=U.fit(label,math.max(0,r.w-12),font,enabled)
 U.text(r.x+r.w//2,r.y+(r.h-smudge.line_height(font))//2,s,font,enabled,'center',not filled)
 return {x=r.x,y=r.y,w=r.w,h=r.h,enabled=enabled}
end
function U.hit(r,x,y)
 return valid_rect(r) and r.enabled~=false and type(x)=='number' and type(y)=='number'
  and x%1==0 and y%1==0 and x>=r.x and y>=r.y and x<r.x+r.w and y<r.y+r.h
end
function U.footer(labels,primary,enabled)
 local m=U.metrics();local n=#labels;local rects={}
 assert(n<=4,'footer supports 0..4 actions; use an app-local secondary row for more')
 if n==0 then return rects end
 local available=m.w-2*m.pad-(n-1)*m.gap
 local base=available//n;local remainder=available%n;local x=m.pad
 for i,label in ipairs(labels) do
  local width=base+(i<=remainder and 1 or 0)
  local r={x=x,y=m.footer_y,w=width,h=m.footer_h}
  rects[i]=U.button(label,r,i==primary,not enabled or enabled[i]~=false)
  x=x+width+m.gap
 end
 return rects
end
return U
