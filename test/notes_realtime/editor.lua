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
local selection=E('Aü🙂Z',function()return true end,N.validate)
selection.move('left');assert(selection.cursor==8)
selection.toggleSelection();selection.move('left');assert(selection.cursor==4 and selection.selection==8)
selection.insert('é');assert(selection.value=='AüéZ' and selection.cursor==6 and not selection.selection)
selection.selectAll();selection.backspace();assert(selection.value=='' and selection.cursor==1)
selection.insert('a\nbü\nc');selection.move('home');assert(selection.cursor==7)
selection.move('up');assert(selection.cursor==3)
selection.move('down');assert(selection.cursor==7)
-- Moving/selecting while live text arrives must not move the insertion anchor.
local live=E('prefix suffix',function()return true end,N.validate)
live.cursor=8;live.start();text='Hi';status='Listening';busy=true;live.update()
live.move('home');live.insert('X');local cursor=live.cursor
text='Hi there';live.update();assert(live.value=='Xprefix Hi theresuffix' and live.cursor==cursor,live.value)
live.selectAll();text='Hi there!';live.update();assert(live.selection==1 and live.cursor==#live.value+1)
live.insert('manual');text='Rewritten final';busy=false;status='Done';live.update();assert(live.value=='manual')
local secret=E('abc',function()return true end,function(v)return #v<=512 and not v:find('[^A-Za-z0-9_-]')end,{secret=true})
secret.insert(' ');assert(secret.value=='abc');secret.cancel();assert(secret.value=='' and secret.cursor==1)
print('Notes UTF-8 cursor/selection/replacement and live insertion anchors OK')
print('Notes live editable draft/manual edit/UTF-8/capacity/save/no-review controller OK')
