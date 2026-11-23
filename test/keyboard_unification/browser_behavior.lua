-- Production Browser route closures, no on_init/cache restore/provider execution.
-- Upvalue seam supplies only neutral route state; all editor and callback code is real.
local raw_dofile=dofile
smudge.get_metrics=function()return {}end
smudge.has_feature=function()return false end
smudge.millis=function()return 1000 end
smudge.http_request=function()error('network forbidden')end
smudge.read_file=function()return nil end
smudge.log=function()end
smudge.exit=function()error('exit forbidden')end
dofile=function(p)if p=='ui.lua'then return U end;return raw_dofile(p)end
raw_dofile('main.lua')
local rows=fixture_upvalue(on_draw,'listActions')
local address=fixture_upvalue(on_draw,'address')
local cache={sources={},entries={},bookmarks={},commit=function()return true end}
fixture_upvalue(rows,'cache',cache)
local W=fixture_upvalue(on_update,'W')
local function flush()
 for i=1,30 do if not W.busy()then return end;on_update()end
 error('editor action did not settle')
end
local function tap(label)
 on_draw()
 for _,b in ipairs(U.buttons)do if b[6]==label then on_tap(b[1]+b[3]//2,b[2]+b[4]//2);flush();return end end
 error('missing Browser key '..label)
end
local function exercise(title,limit,id)
 assert(U.editor.title==title and U.editor.limit==limit)
 on_draw();fixture_capture(id)
 U.editor.value='éa';U.editor.cursor=3;on_draw();tap('Delete');assert(U.editor.value=='a' and U.editor.cursor==1)
 tap('q');assert(U.editor.value=='qa' and U.editor.cursor==2)
 tap('123');tap('@');assert(U.editor.value=='q@a' and U.editor.cursor==3)
 U.editor.value=string.rep('x',limit);U.editor.cursor=limit+1;tap('Space');assert(#U.editor.value==limit)
 U.editor.value='é';U.editor.cursor=3;on_draw()
 -- The real native field hit path moves to a UTF8 boundary without editing.
 local moved=false
 for y=80,150 do if U.tap(0,y)then moved=true;break end end
 assert(moved and U.editor.cursor==1 and U.editor.value=='é')
 on_button('back',false);assert(U.editor)
 on_button('back',true);assert(not U.editor)
 print('CALLER_PASS '..id..' actual_route UTF8_middle_delete punctuation cap field_cursor cancel')
end
address();exercise('Address / search',512,'lua.browser.address-search')
fixture_upvalue(rows,'screen',nil)
for _,r in ipairs(rows())do if r[1]=='Find on page'then r[2]();break end end
exercise('Find on page',100,'lua.browser.find-on-page')
fixture_upvalue(rows,'screen','news');rows()[1][2]()
exercise('HTTPS RSS / Atom feed',512,'lua.browser.feed-url')
local form={fields={{name='Neutral field',value='',hidden=false}}}
fixture_upvalue(rows,'form',form);fixture_upvalue(rows,'screen','form');rows()[1][2]()
exercise('Neutral field',256,'lua.browser.get-form-field')
rows()[1][2]();U.editor.value='Neutral';U.editor.cursor=8;tap('Done');assert(form.fields[1].value=='Neutral')
print('BROWSER_DONE_PASS actual_GET_field_callback')
