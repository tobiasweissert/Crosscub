local started,stopped,cancelled=0,0,0
local text,status,busy='', 'Ready',false
smudge.notes_dictation_start=function()started=started+1;busy=true;text='';return true end
smudge.notes_dictation_stop=function()stopped=stopped+1;status='Finishing' end
smudge.notes_dictation_cancel=function()cancelled=cancelled+1;busy=false;text='' end
smudge.notes_dictation_poll=function()return text,status,busy end
smudge.keyboard_snapshot=function(title,value,cursor)
 assert(value and cursor>=1 and cursor<=#value+1)
 return {{x=10,y=300,w=44,h=44,action='insert',output='x'},{x=60,y=300,w=44,h=44,action='done'}}
end
local N=dofile('core.lua');local E=dofile('dictation_editor.lua')
local saved
local editor=E('Old ',function(value)saved=value;return true end,N.validate)
editor.start();assert(started==1)
text='Hello';status='Listening';editor.update();assert(editor.value=='Old Hello' and saved==nil)
editor.insert('!');text='Hello world';editor.update();assert(editor.value=='Old Hello! world')
editor.stop();assert(stopped==1)
text='Hello, world!';status='Done';busy=false;editor.update()
assert(editor.value=='Old Hello! world') -- completion must not overwrite manual edits
assert(editor.save() and saved=='Old Hello! world')
local e=E('',function(v)saved=v;return true end,N.validate);e.start();text='Hel';busy=true;e.update();text='Hello!';status='Done';busy=false;e.update();assert(e.value=='Hello!')
e.cursor=1;e.insert('ü');assert(e.value=='üHello!');e.backspace();assert(e.value=='Hello!')
local full=E(string.rep('a',3072),function()error('unexpected save')end,N.validate);full.start();text='overflow';busy=true;full.update();assert(#full.value==3072 and cancelled>0)
local locked=E('',function()return true end,N.validate);locked.start();text='Before lock';busy=true;status='Listening';locked.update()
text='';status='Cancelled';busy=false;locked.update();assert(locked.value=='Before lock')
e.cancel();assert(cancelled>1)
print('Notes live editable draft/manual edit/UTF-8/capacity/save/no-review controller OK')
