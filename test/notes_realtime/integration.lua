local current='';local providerText,providerStatus,providerBusy='', 'Ready',false
local starts,stops,cancels=0,0,0
smudge.notes_dictation_start=function()starts=starts+1;providerBusy=true;return true end
smudge.notes_dictation_stop=function()stops=stops+1 end
smudge.notes_dictation_cancel=function()cancels=cancels+1;providerBusy=false end
smudge.notes_dictation_poll=function()return providerText,providerStatus,providerBusy end
smudge.keyboard_snapshot=function(title,value,cursor,shift,symbols,focus,x,y)
 current=value
 assert(math.type(cursor)=='integer' and cursor>=1 and cursor<=#value+1,'invalid keyboard snapshot')
 if x then
  if x==16 and y==28 then return -1 end -- native header Back (-2) + Lua's 1-based offset
  if y==120 then return x==100 and 1 or #value+1 end -- native text-field cursor
  return 0 -- native no-hit (-1) + Lua's 1-based offset
 end
 return {{x=10,y=300,w=44,h=44,action='insert',output='!'}}
end
local w,h=smudge.get_bounds()
local api=dofile('sticky_enhanced.lua');on_init();on_draw();on_button('confirm',true);on_draw();on_button('back',true);on_draw()
local function hit(kind,value)
 for _,v in ipairs(api.hits())do if v.action==kind and v.value==value then local b=v.box;on_touch(b.x+b.w//2,b.y+b.h//2,'tap');on_draw();return end end
 error('missing hit '..kind..':'..tostring(value))
end
hit('option',8)
on_touch(w-54,28,'tap');assert(starts==1)
providerText='Hello';providerStatus='Listening';on_update();on_draw();assert(current=='Hello')
on_touch(25,320,'tap');on_draw();assert(current=='Hello!')
providerText='Hello world';on_update();on_draw();assert(current=='Hello! world')
for _,point in ipairs({{57,320},{w-1,h-1},{180,57}})do -- key gap, unused area, non-input header
 on_touch(point[1],point[2],'tap');on_draw();assert(current=='Hello! world')
 local _,_,screen=api.state();assert(screen=='edit')
end
on_touch(w-54,28,'tap');assert(stops==1)
providerBusy=false;providerStatus='Done';on_update();on_draw()
on_touch(w-54,28,'tap');assert(starts==2)
local before=cancels
on_touch(16,28,'tap');on_draw();assert(cancels==before+1 and current=='Hello! world')
local _,_,screen=api.state();assert(screen=='edit') -- Back cancels capture, not the unsaved draft
providerText='';providerBusy=false;providerStatus='Cancelled';on_update();on_draw();assert(current=='Hello! world')
on_touch(25,320,'tap');on_draw();assert(current=='Hello! world!')
on_touch(w-154,28,'tap');on_draw();assert(api.state()=='Hello! world!' and cancels>0)
on_exit()
local again=dofile('sticky_enhanced.lua');on_init();on_button('confirm',true);assert(again.state()=='Hello! world!');on_exit()
local N=dofile('core.lua');local F=dofile('graphics.lua');F.init()
local draft=dofile('dictation_editor.lua')('Unsaved',function(value)assert(value=='Unsaved!');return true end,N.validate)
draft.draw('Note',F);draft.touch(100,120);assert(draft.cursor==1)
for _,point in ipairs({{57,320},{w-1,h-1},{180,57}})do
 draft.touch(point[1],point[2]);draft.draw('Note',F);assert(draft.cursor==1 and draft.value=='Unsaved')
end
draft.touch(200,120);assert(draft.cursor==#draft.value+1)
local snapshot=smudge.keyboard_snapshot
for _,invalid in ipairs({#draft.value+2,1.5,'invalid',false})do
 smudge.keyboard_snapshot=function(...)if select(7,...)~=nil then return invalid end;return snapshot(...)end
 draft.touch(200,200);draft.draw('Note',F);assert(draft.cursor==#draft.value+1 and draft.value=='Unsaved')
end
smudge.keyboard_snapshot=snapshot
draft.start();draft.touch(16,28);draft.draw('Note',F)
assert(not draft.active and not draft.busy and draft.value=='Unsaved' and draft.cursor==#draft.value+1)
draft.insert('!');assert(draft.save())
print('Notes native touch no-hit/gap/unused/header/Back/cursor validation preserves editable saveable drafts OK')
print('Notes installed app: existing card -> editor -> live editable transcript -> explicit Save -> reopen journal OK')
