-- Neutral local games only; real Lua controllers/renderers and bundled art.
smudge.get_memory=function()return {lua_max_kb=1024}end
smudge.millis=function()return 1000 end
smudge.get_date=function()return {year=2026,month=11,day=1}end
smudge.write_file=function()return true end
smudge.save=function()return true end
smudge.load=function()return nil end
smudge.get_orientation=function()return 0 end
smudge.set_orientation=function()end
smudge.log=function()end
smudge.exit=function()error('exit forbidden')end
local function hit(b)on_touch(b.x+b.w//2,b.y+b.h//2,'tap')end
if VARIANT=='pad-wordle-enhanced' or VARIANT=='pad-wordle-compact' then
 local enhanced=VARIANT=='pad-wordle-enhanced'
 local app=dofile(enhanced and 'enhanced.lua'or'cp_app.lua');on_init()
 local state=app.state();state.game=app.core.start(-1,'PLANT');state.day=-1;state.menu=false;state.notice=''
 on_draw();local l=app.layout()
 local function key(id)
  if enhanced then hit(l.keys[id])else hit({x=12+(id-1)%10*l.kw,y=l.ky+(id-1)//10*(l.kh+4),w=l.kw-3,h=l.kh})end
 end
 key(1);assert(state.game.typing=='Q');key(20);assert(state.game.typing=='');key(28);assert(state.game.status=='Playing')
 local marks=app.core.score('PAPAL','PLANT');assert(marks[1]==4)
 state.game.keys.Q=2;on_draw();fixture_capture(VARIANT..'-absent');state.game.keys.Q=3;on_draw();fixture_capture(VARIANT..'-present');state.game.keys.Q=4;on_draw();fixture_capture(VARIANT..'-correct')
 state.game.keys.Q=0;state.notice='';state.key=1;on_draw();l=app.layout()
 PAD=enhanced and l.keys[1]or{x=12,y=l.ky,w=l.kw-3,h=l.kh};PAD_LABEL='Q';PAD_SELECTED=enhanced and 0 or 1
 print('PAD_BEHAVIOR_PASS '..VARIANT..' tap_letter delete short_submit feedback_model')
elseif VARIANT=='pad-sudoku' then
 local V=dofile('board.lua');local w,h=smudge.get_bounds();local l=V.layout(w,h)
 local g={level=0,elapsed=0,value={},given={},notes={},history={},digit=1,notice='',counts={0,0,0,0},best={-1,-1,-1,-1}}
 for i=1,81 do g.value[i]=0;g.given[i]=0;g.notes[i]=0 end
 V.init(g);V.draw(l,g)
 local function touch(b)V.touch(l,g,b.x+b.w//2,b.y+b.h//2,'tap',function()end,function()end,function()end,function()end,function()end)end
 touch(l.keys[9]);assert(g.digit==9);touch(l.keys[1]);assert(g.digit==1)
 V.draw(l,g);PAD=l.keys[1];PAD_LABEL='1';PAD_SELECTED=1
 print('PAD_BEHAVIOR_PASS pad-sudoku actual_digit_touch all_cells_preserved')
else
 local original=VARIANT=='pad-calculator-original'
 local app=dofile(original and 'sticky_enhanced.lua'or'compact.lua');on_init();on_draw()
 local w,h=smudge.get_bounds()
 local function cell(i)
  if original then return app.cells()[i]end
  local left=w>h and 64 or 20;local cw=(w-left-20)//4;local ch=(h-150-60)//6
  return {x=left+(i-1)%4*cw,y=150+(i-1)//4*ch,w=cw,h=ch}
 end
 hit(cell(13));hit(cell(16));hit(cell(14));hit(cell(20))
 local C=dofile('core.lua');local g=original and app.state()or fixture_upvalue(on_draw,'g');assert(C.display(g)=='3')
 hit(cell(1));on_draw();PAD=cell(1);PAD_LABEL='AC';PAD_SELECTED=original and 0 or 1
 print('PAD_BEHAVIOR_PASS '..VARIANT..' actual_touch 1_plus_2_equals_3 clear')
end
