local stored='old-key';local writes,clears=0,0;local fail=false
smudge.notes_key_status=function()return stored~=nil end
smudge.notes_key_save=function(value)writes=writes+1;assert(value=='sk-canary');if fail then return false end;stored=value;return true end
smudge.notes_key_clear=function()clears=clears+1;stored=nil;return true end
local snapshots=0
smudge.keyboard_snapshot=function(title,value,cursor,shift,symbols,focus,x,y,language,selection,masked,top)
 assert(masked==true and top==144)
 assert(value=='' or value=='sk-canary') -- never preload the persisted value
 snapshots=snapshots+1
 return {{x=10,y=300,w=44,h=44,action='insert',output='sk-canary'},{x=60,y=300,w=44,h=44,action='done'}}
end
local F=dofile('graphics.lua');F.init()
local text=F.text
F.text=function(value,...)
 assert(not value:find('old-key',1,true) and not value:find('sk-canary',1,true),'secret echoed by settings')
 return text(value,...)
end
local closed=false;local K=dofile('key_settings.lua')(function()closed=true end)
local function hit(action)
 K.draw('',F)
 for _,b in ipairs(K.hits)do if b.action==action then K.touch(b.x+2,b.y+2);K.draw('',F);return end end
 error('missing settings action '..action)
end
hit('edit');K.touch(12,302);K.touch(62,302);K.draw('',F)
assert(writes==0 and stored=='old-key') -- keyboard OK only requests confirmation
hit('cancel');assert(writes==0 and stored=='old-key')
hit('edit');K.touch(12,302);K.touch(62,302);hit('confirm')
assert(writes==1 and stored=='sk-canary')
hit('clear');assert(clears==0);hit('cancel');assert(stored=='sk-canary')
hit('clear');hit('confirm');assert(clears==1 and stored==nil)
hit('edit');K.touch(12,302);K.button('back');K.draw('',F);assert(writes==1 and not closed)
fail=true;hit('edit');K.touch(12,302);K.touch(62,302);hit('confirm')
assert(writes==2 and stored==nil and not closed);hit('cancel')
hit('back');assert(closed and snapshots>0)
-- Actual app options route is usable before any key is configured.
local app=dofile('sticky_enhanced.lua');on_init();on_button('confirm',true);on_button('back',true);on_draw()
local found=false
for _,v in ipairs(app.hits())do if v.action=='option' and v.value==9 then
 local b=v.box;local w,h=smudge.get_bounds();assert(b.h>=44 and b.x>=0 and b.y+b.h<h)
 on_touch(b.x+2,b.y+2,'tap');on_draw();found=true;break
end end
assert(found);on_button('back',true);on_exit()
print('Notes key UI masked new input / explicit Save+Clear confirmations / Cancel / app route OK; no provider calls')
