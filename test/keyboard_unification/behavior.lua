-- Genuine production callers with isolated neutral storage, never provider work.
local raw_dofile=dofile
local files={}
smudge.read_file=function(name) return files[name] end
smudge.write_file=function(name,bytes) if failSave then return false end;files[name]=bytes;return true end
smudge.log=function()end
smudge.get_date=function()return {year=2026,month=11,day=1,wday=1,hour=12,min=0,sec=0}end
smudge.millis=function()return 1000 end
smudge.prevent_sleep=function()end
smudge.exit=function()error('unexpected app exit')end
smudge.http_request=function()error('provider forbidden')end
smudge.has_feature=function()return false end
-- Keep exact production main/editor functions, exposing the same UI object to
-- the fixture without rewriting their callers or replacing storage codecs.
dofile=function(path) if path=='ui.lua' then return U end;return raw_dofile(path)end
raw_dofile('main.lua');on_init()
local function tap(label,prefix)
 on_draw()
 for _,b in ipairs(U.buttons) do
  if b[6]==label or (prefix and b[6]:sub(1,#label)==label)then
   on_tap(b[1]+b[3]//2,b[2]+b[4]//2);return
  end
 end
 error('missing actual caller/control '..label)
end
local function opened(title,limit)
 assert(U.editor and U.editor.title==title and U.editor.limit==limit)
 local ids={['Front - question']='lua.flashcards.front-question',['Back - answer']='lua.flashcards.back-answer',['Deck name']='lua.flashcards.deck-name',['New habit']='lua.habits.new-habit',['Habit name']='lua.habits.rename-habit',['Focus task']='lua.pomodoro.focus-task',['Find a city']='lua.weather.city-search'}
 on_draw();fixture_capture(ids[title])
 -- Real editing actions, capped byte insertion and UTF8 endpoint deletion.
 U.editor.value='é';on_draw();tap('Delete');assert(U.editor.value=='')
 tap('Shift');tap('Q');assert(U.editor.value=='Q')
 tap('123');tap('@');assert(U.editor.value=='Q@')
 U.editor.value=string.rep('x',limit);on_draw();tap('Space');assert(#U.editor.value==limit)
 tap('Delete');assert(#U.editor.value==limit-1)
 U.editor.value='Neutral';on_draw()
end
local function proof(id)print('CALLER_PASS '..id)end
if APP=='flashcards' then
 on_button('up',true);tap('Add card');opened('Front - question',160)
 tap('Done');assert(U.editor and U.editor.title=='Back - answer');proof('lua.flashcards.front-question')
 opened('Back - answer',160);failSave=true;tap('Done');assert(U.notice:find('Save failed',1,true));proof('lua.flashcards.back-answer')
 failSave=false;tap('Rename deck');opened('Deck name',32);tap('Done');assert(not U.editor);proof('lua.flashcards.deck-name')
elseif APP=='habits' then
 on_button('right',true);opened('New habit',40);failSave=true;tap('Done');assert(U.notice:find('Save failed',1,true));proof('lua.habits.new-habit')
 failSave=false;on_button('left',true);tap('Rename selected');opened('Habit name',40);tap('Done');assert(not U.editor);proof('lua.habits.rename-habit')
elseif APP=='pomodoro' then
 on_button('left',true);tap('Task: ',true);opened('Focus task',48);failSave=true;tap('Done');assert(U.notice:find('Save failed',1,true));proof('lua.pomodoro.focus-task')
elseif APP=='weather' then
 tap('Location');opened('Find a city',48);on_button('back',true);assert(not U.editor)
 tap('Location');opened('Find a city',48);tap('Done');assert(not U.editor);proof('lua.weather.city-search')
end
-- Cancel never invokes a done callback; release does not activate another key.
local called=false
U.edit('Cancel probe','é',12,function()called=true end);on_draw()
on_button('back',false);assert(U.editor and not called)
on_button('back',true);assert(not U.editor and not called)
print('GENERIC_BEHAVIOR_PASS '..APP)
