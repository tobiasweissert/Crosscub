local U=dofile('ui.lua');collectgarbage('collect')
local M=dofile('model.lua');collectgarbage('collect')
local S=dofile('storage.lua');collectgarbage('collect')
local V=dofile('weather_view.lua');collectgarbage('collect')
local location,forecast,pending,active,cities,detail,closed
local cancel
local status='Refresh to fetch weather'
local function queue(kind,url)
 if active or pending then status='Request in progress';return end
 pending={kind=kind,url=url};status='Queued';U.redraw()
end
local function refresh()
 local url='https://api.open-meteo.com/v1/forecast?latitude='..string.format('%.4f',location.lat)..'&longitude='..string.format('%.4f',location.lon)..'&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m&hourly=temperature_2m,weather_code&forecast_hours=16&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=5&timezone=auto&format=csv'
 queue('forecast',url)
end
local function search()
 if active or pending then status='Wait or press Back to cancel';return end
 U.edit('Find a city','',48,function(s)if s:find('%S')then queue('cities','https://geocoding-api.open-meteo.com/v1/search?name='..U.encode(s)..'&count=3&language=en&format=json')end end)
end
function on_init()
 closed=false;local saved=S.load(M.cache)
 location=saved and saved.location or {lat=47.3769,lon=8.5417,name='Zurich'}
 forecast=saved and saved.forecast;collectgarbage('collect')
 if forecast then status='Cached: '..forecast.current.time end
 U.redraw()
end
function on_update()
 if closed or active or not pending then return end
 local work=pending;pending=nil
 if not smudge.has_feature('https_async')then status='HTTPS unavailable in simulator';U.redraw();return end
 local id,err=smudge.http_request{url=work.url,headers={Accept=work.kind=='forecast'and 'text/csv'or 'application/json'}}
 if id then active={id=id,kind=work.kind};status='Loading...'
 else status='Request failed: '..tostring(err)end;U.redraw()
end
function on_http(e)
 if closed or not active or e.id~=active.id then return end
 local kind=active.kind;active=nil
 if e.error or e.status~=200 then status=e.error=='time_untrusted'and 'Trust verified UTC in App Settings'or('Fetch failed: '..tostring(e.error or e.status));U.redraw();return end
 if kind=='cities'then
  forecast=nil;U.digits=nil;collectgarbage('collect');cities=M.cities(e.body);status=cities and (#cities==0 and 'No matching city' or 'Choose a city')or 'Invalid city response'
 else
  U.digits=nil;collectgarbage('collect')
  local nextForecast=M.forecast(e.body);collectgarbage('collect')
  if nextForecast then
   forecast=nextForecast
   local saved=S.save(M.encodeLocation(location)..'\n'..e.body)
   status=saved and ('Updated '..forecast.current.time:sub(12,16))or 'Updated; cache save failed'
  else status='Invalid forecast; previous data kept'end
 end
 U.focus=1;U.redraw()
end
function on_draw()
 if U.editor then U.editDraw();return end
 V.begin(U,location,forecast)
 if cities and #cities>0 then
  U.text(24,90,'Choose a city','ui12',true)
  for i,city in ipairs(cities)do U.button(city.name,24,145+(i-1)*58,U.w-48,48,function()
   location=city;forecast=nil;cities=nil;U.save(S,M.encodeLocation(location)..'\n');refresh()
  end)end
 else
  if forecast then V.draw(U,M,forecast,detail)
  else
   local y=(U.top+U.bottom)//2-70
   U.text(U.w//2,y,'NO FORECAST','lexend16',true,'center')
   U.wrap('Select Refresh. Wi-Fi and trusted UTC are required.',24,y+55,U.w-48,3,'ui10')
  end
  -- Keep the same focus/touch controls and emergency Back cancellation. Busy
  -- requests use the existing footer slot rather than covering forecast rows.
  U.actions({{(active or pending)and 'Cancel'or 'Refresh',function()
   if active or pending then cancel();status='Cancelled'else refresh()end
  end},{'Location',search},{detail and 'Overview'or 'Details',function()detail=not detail end}},1)
 end
 V.bottom(U,status,forecast)
 U.footer()
end
cancel=function()
 pending=nil;if active then smudge.http_cancel(active.id);active=nil end
end
function on_button(key,pressed)
 if not pressed then return end
 if U.editor then if key=='back'then U.closeEditor();U.redraw()else U.key(key)end;return end
 if key=='back'then
  if active or pending then cancel();status='Cancelled'
  elseif cities then
   cities=nil;S.seq=0;local saved=S.load(M.cache);forecast=saved and saved.forecast
  else closed=true;smudge.exit()end
 else U.key(key)end;U.redraw()
end
function on_tap(x,y)U.tap(x,y)end
function on_swipe(dir)if not U.editor then detail=not detail;U.redraw()end end
function on_exit()closed=true;cancel()end
