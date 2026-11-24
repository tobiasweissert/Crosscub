-- Weather presentation only: controller/parser/journal remain in their original modules.
local N=dofile('numeric_sans.lua')
local I=dofile('weather_icons.lua')
local V={}
local function value(n)return string.format('%.0f',n)end
local function temp(n)return value(n)..'°'end
local labels={[0]='Clear',[1]='Mostly clear',[2]='Partly cloudy',[3]='Cloudy',[45]='Fog',[48]='Freezing fog',[51]='Light drizzle',[53]='Drizzle',[55]='Heavy drizzle',[56]='Freezing drizzle',[57]='Freezing drizzle',[61]='Light rain',[63]='Rain',[65]='Heavy rain',[66]='Freezing rain',[67]='Freezing rain',[71]='Light snow',[73]='Snow',[75]='Heavy snow',[77]='Snow grains',[80]='Rain showers',[81]='Rain showers',[82]='Heavy showers',[85]='Snow showers',[86]='Heavy snow showers',[95]='Thunderstorm',[96]='Thunder / hail',[99]='Thunder / hail'}
function V.condition(M,c)return labels[c]or M.condition(c)end
local function number(n,x,y,w,h,white,left)
 -- Scale the degree ring with the selected genuine numeric cut, not a tiny glyph.
 local s=value(n);local reserve=math.floor(h*0.35)+10
 local nw,cut=N.measure(s,w-reserve,h)
 local r=math.max(4,math.floor(cut*0.14));local total=nw+2*r+10
 local start=left and x or x+(w-total)//2
 N.draw(s,start,y,nw,h,white)
 local cx=start+nw+r+8;local cy=y+(h-cut)//2+r+3
 smudge.circle(cx,cy,r,true,not white);smudge.circle(cx,cy,math.max(2,math.floor(r*0.58)),true,white or false)
end
local function text(U,x,y,s,font,bold,align,ink)U.text(x,y,s,font or 'ui10',bold,align,ink)end
local function rule(x,y,w)smudge.line(x,y,x+w,y)end
function V.begin(U,location,forecast)
 local m=U.style.metrics();U.w,U.h=m.w,m.h;U.pad,U.top,U.bottom=24,80,m.body_bottom
 U.buttons={};if not U.editor then U.editorDraw=nil end;smudge.clear()
 local stamp=forecast and ('Updated '..forecast.current.time:sub(12,16))or 'Weather'
 local rw=smudge.text_width(stamp,'ui10')
 text(U,24,18,U.style.fit(location.name,U.w-64-rw,'ui12',true),'ui12',true)
 text(U,U.w-24,23,stamp,'ui10',false,'right');rule(0,66,U.w)
end
function V.bottom(U,status,forecast)
 local y=U.h-118
 local message=U.notice~=''and U.notice or status
 text(U,24,y,U.style.fit(message,U.w-48,'small'),'small')
 text(U,24,U.h-91,'Weather: Open-Meteo (CC BY 4.0)','small')
end
function V.draw(U,M,f,detail)
 local w,h=U.w,U.h;local landscape=w>h;local c=f.current
 local cond=V.condition(M,c.weather_code)
 local feels='Feels like '..temp(c.apparent_temperature)..' · Wind '..value(c.wind_speed_10m)..' km/h'
 if detail then
  local x0,ww,hy,dy,rh
  if landscape then
   number(c.temperature_2m,24,86,190,125);I.draw(c.weather_code,215,99,85)
   U.wrap(cond,24,228,280,2,'ui12',true)
   text(U,24,300,'Feels like '..temp(c.apparent_temperature),'ui10')
   text(U,24,330,'Wind '..value(c.wind_speed_10m)..' km/h','ui10')
   x0,ww,hy,dy,rh=330,w-354,82,206,38
  else
   I.draw(c.weather_code,24,98,112);number(c.temperature_2m,151,82,w-175,128)
   text(U,24,228,cond,'ui12',true);text(U,24,268,feels,'ui10')
   rule(0,309,w);x0,ww,hy,dy,rh=24,w-48,322,470,49
  end
  local cw=ww//5
  for i=1,5 do local hour=1+(i-1)*3;local x=x0+(i-1)*cw+cw//2
   text(U,x,hy,f.hourly.time[hour]:sub(12,16),'ui10',false,'center')
   text(U,x,hy+30,temp(f.hourly.temperature_2m[hour]),'ui12',true,'center')
   U.wrap(V.condition(M,f.hourly.weather_code[hour]),x-cw//2+4,hy+64,cw-8,2,'small')
  end
  rule(x0,dy-14,ww)
  for i=2,5 do local yy=dy+(i-2)*rh
   text(U,x0,yy,f.daily.time[i]:sub(6),'ui10',true)
   local room=ww-190
   text(U,x0+82,yy,U.style.fit(V.condition(M,f.daily.weather_code[i]),room,'ui10'),'ui10')
   text(U,x0+ww-54,yy,temp(f.daily.temperature_2m_max[i]),'ui10',true,'right')
   text(U,x0+ww,yy,temp(f.daily.temperature_2m_min[i]),'ui10',false,'right')
   if i<5 then rule(x0,yy+rh-12,ww)end
  end
 else
  local cardX,cardY,cardW,cardH
  local lo,hi=f.hourly.temperature_2m[1],f.hourly.temperature_2m[1]
  for i=2,13 do lo=math.min(lo,f.hourly.temperature_2m[i]);hi=math.max(hi,f.hourly.temperature_2m[i])end
  local summary='Next 12 hours: '..temp(lo)..' to '..temp(hi)..'.'
  if landscape then
   number(c.temperature_2m,24,83,215,150);I.draw(c.weather_code,244,104,90)
   U.wrap(cond,24,253,310,2,'ui12',true)
   text(U,24,325,'Feels like '..temp(c.apparent_temperature),'ui10')
   U.wrap(summary,366,83,w-390,2,'ui10')
   cardX,cardY,cardW,cardH=366,157,w-390,130
  else
   number(c.temperature_2m,24,88,310,180,false,true);I.draw(c.weather_code,w-132,107,108)
   text(U,24,332,cond,'ui12',true)
   U.wrap(summary,24,382,w-48,2,'ui10')
   cardX,cardY,cardW,cardH=16,489,w-32,143
  end
  local cw=(cardW-20)//3
  for i=1,3 do local hour=1+(i-1)*6;local x=cardX+(i-1)*(cw+10);local filled=i==1
   smudge.rounded_rect(x,cardY,cw,cardH,14,filled,2,true)
   text(U,x+cw//2,cardY+12,f.hourly.time[hour]:sub(12,16),'ui10',false,'center',not filled)
   number(f.hourly.temperature_2m[hour],x+8,cardY+44,cw-16,48,filled)
   local label=V.condition(M,f.hourly.weather_code[hour])
   text(U,x+cw//2,cardY+cardH-34,U.style.fit(label,cw-12,'small',true),'small',true,'center',not filled)
  end
  local summaryY=landscape and 313 or 651
  local summaryX=landscape and 366 or 24
  local bottom='High '..temp(f.daily.temperature_2m_max[1])..' · Low '..temp(f.daily.temperature_2m_min[1])..' · Wind '..value(c.wind_speed_10m)..' km/h'
  text(U,summaryX,summaryY,bottom,'small')
 end
end
return V
