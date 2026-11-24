-- Snake for the Crosscub Lua app host. No network or firmware changes.
local Core=smudge.dofile('core.lua')
local Font=smudge.dofile('font.lua')
local game,best,last_tick= nil,0,0
local painted=false
local tap_origin,tap_painted,tap_tick=nil,false,0 -- retained only within one input batch
local w,h=480,800
local tile,tile_size=nil,0
local function valid_best(value)
 if type(value)~='string' or not value:match('^%d+$') or #value>4 then return 0 end
 local n=tonumber(value)
 return n and n>=0 and n<=384 and n%1==0 and math.floor(n) or 0
end
local function save_best()
 best=math.max(best,game and game.score or 0,valid_best(smudge.load('best','')),valid_best(smudge.load('best_backup','')))
 local value=tostring(best)
 for _,key in ipairs({'best_backup','best'}) do
  if smudge.load(key,'')~=value then
   local ok=pcall(smudge.save,key,value)
   if not ok or smudge.load(key,'')~=value then smudge.log('Snake: best score could not be saved') end
  end
 end
end
local function invalidate(full)
 painted=false
 if full then smudge.full_refresh() else smudge.request_update() end
end
local function new_game()
 game=Core.new(16,24)
 -- A real reachable starting target, positioned just above the welcome panel.
 game.food={x=8,y=7}
 last_tick=smudge.millis();smudge.prevent_sleep(false)
end
function on_init()
 best=math.max(valid_best(smudge.load('best','')),valid_best(smudge.load('best_backup','')))
 new_game();invalidate(true)
end
local function steer(direction)
 -- A movement redraw is not a new view: retain fresh steering while it waits.
 -- Keep the fence only for unpainted start/pause/game-over transitions.
 if not painted and game.status~='running' then return end
 if direction~='left' and direction~='right' and direction~='up' and direction~='down' then return end
 if game.status=='over' or game.status=='won' then new_game() end
 local before=game.status
 Core.turn(game,direction)
 if game.status=='running' then
  smudge.prevent_sleep(true)
  if before~='running' then last_tick=smudge.millis();invalidate(false) end
 end
end
function on_swipe(direction)
 if direction~='left' and direction~='right' and direction~='up' and direction~='down' then return end
 -- Firmware may deliver both tap and swipe for one release. Reconcile that
 -- batch before steering: a swipe must not leave its preceding tap's pause
 -- or force a fresh left swipe to start facing right.
 if tap_origin=='running' or tap_origin=='paused' or tap_origin=='ready' then
  game.status=tap_origin
  if tap_origin=='running' then last_tick=tap_tick end
  painted=tap_painted -- preserve any movement frame that was already pending.
 end
 tap_origin=nil
 steer(direction)
end
local function toggle()
 -- Pausing an active run must work even while its next frame is queued.
 if not painted and game.status~='running' then return end
 if game.status=='over' or game.status=='won' then new_game()
 elseif game.status=='ready' then Core.turn(game,'right')
 else Core.pause(game) end
 last_tick=smudge.millis();smudge.prevent_sleep(game.status=='running');invalidate(false)
end
function on_tap(x,y)
 if x<0 or y<0 or x>=w or y>=h then return end
 if not painted and game.status~='running' then return end
 tap_origin,tap_painted,tap_tick=game.status,painted,last_tick
 toggle()
end
function on_button(button,pressed)
 if not pressed then return end
 if button=='back' then
  save_best();smudge.prevent_sleep(false);smudge.exit();return
 end
 if button=='confirm' then toggle()
 elseif button=='page_back' then steer('left')
 elseif button=='page_forward' then steer('right')
 else steer(button) end
end
function on_update()
 tap_origin=nil -- firmware has finished this batch's tap/swipe dispatch.
 if not game or not painted or game.status~='running' then return end
 local now=smudge.millis()
 local elapsed=now-last_tick -- int32 wrapping preserves short elapsed intervals.
 if elapsed<0 then last_tick=now;return end
 if elapsed<Core.interval(game) then return end
 last_tick=now
 Core.step(game)
 if game.status=='over' or game.status=='won' then save_best();smudge.prevent_sleep(false) end
 -- One movement per painted frame; never catch up unseen moves after a slow refresh.
 invalidate(false)
end
local function body_tile(size)
 if tile and tile_size==size then return tile end
 local stride=(size+7)//8;local rows={};local radius=math.max(2,size//5)
 for y=0,size-1 do
  local bytes={}
  for bx=0,stride-1 do
   local byte=255
   for bit=0,7 do
    local x=bx*8+bit
    if x<size then
     local cx=math.max(radius-x,0,x-(size-1-radius))
     local cy=math.max(radius-y,0,y-(size-1-radius))
     local inside=(cx*cx+cy*cy)<=radius*radius
     -- Stable dark-gray ordered dither. Bitmap 0=ink, 1=paper.
     if inside and not(x%2==0 and y%2==0) then byte=byte & (~(128>>bit)) end
    end
   end
   bytes[#bytes+1]=string.char(byte)
  end
  rows[#rows+1]=table.concat(bytes)
 end
 tile=table.concat(rows);tile_size=size;return tile
end
local function modal(title,message)
 local bw=math.min(360,w-40);local bh=174
 local bx=(w-bw)//2;local by=math.floor(h*.484-bh/2)
 smudge.rounded_rect(bx,by,bw,bh,20,true,false)
 smudge.rounded_rect(bx,by,bw,bh,20,false,true,4)
 Font.draw(title,w//2,by+36,'title','center',false)
 local cut='body';local lines=Font.wrap(message,cut,bw-48)
 if #lines>3 then cut='small';lines=Font.wrap(message,cut,bw-48) end
 for i,line in ipairs(lines) do Font.draw(line,w//2,by+90+(i-1)*30,cut,'center',true) end
end
function on_draw()
 tap_origin=nil
 w,h=smudge.get_bounds();smudge.clear()
 local cell=math.floor(math.min(w/game.cols,(h-64)/game.rows))
 local ox=(w-cell*game.cols)//2;local oy=64+(h-64-cell*game.rows)//2
 local size=cell-2
 if game.status~='ready' then
  local sprite=body_tile(size)
  for i,p in ipairs(game.snake) do
   local x,y=ox+p.x*cell+1,oy+p.y*cell+1
   if i==1 then smudge.rounded_rect(x,y,size,size,math.max(2,size//5),true,true)
   else smudge.bitmap(x,y,size,size,sprite,false,false) end
  end
 end
 if game.food then
  local x=ox+game.food.x*cell+cell//2;local y=oy+game.food.y*cell+cell//2
  if game.status=='ready' then x=math.floor(w*.53125);y=math.floor(h*.365) end
  smudge.circle(x,y,math.max(5,cell//2-2),false,true,math.max(2,cell//6))
 end
 Font.draw(tostring(game.score),w-18,21,'score','right',true)
 if game.status=='ready' then modal('Snake','Swipe to start and steer. Tap to pause.')
 elseif game.status=='paused' then modal('Paused','Tap to resume. Back to exit.')
 elseif game.status=='over' or game.status=='won' then
  modal(game.status=='won' and 'You win!' or 'Game over','Score '..game.score..' - Best '..best..'. Tap to try again.')
 end
 painted=true
end
function on_exit()
 if game then save_best() end
 smudge.prevent_sleep(false)
end
