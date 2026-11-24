-- Original CrossPlay TOYBOX pad; calculation and journals remain in their tested modules.
local K=dofile('keypad.lua')
local C=dofile('core.lua');local S=dofile('storage.lua');local F=dofile('graphics.lua')
local g=C.new();local selected=1;local keyboard=false;local menu=false;local notice='';local closed=false
local keys={'AC','BS','%','/','7','8','9','*','4','5','6','-','1','2','3','+','+/-','0','.','='}
local labels={'AC','DEL','%','÷','7','8','9','×','4','5','6','−','1','2','3','+','+/-','0','.','='}
local w,h,cells,original=480,800,{},nil
local function geometry()
 w,h=smudge.get_bounds();cells={};local top=97+136+12;local cw=(w-32-36)//4;local ch=(h-16-top-48)//5
 for i=1,20 do cells[i]=F.box(16+(i-1)%4*(cw+12),top+(i-1)//4*(ch+12),cw,ch) end
end
local function save()if not S.save(C.encode(g)) then notice='SAVE FAILED - RETRY';return false end;return true end
local function redraw()smudge.request_update()end
local function press()C.press(g,keys[selected]);notice='' end
function on_init()
 closed=false;g=S.load(C.decode) or C.new();F.init();local ok,o=pcall(smudge.get_orientation);if ok then original=o end
 geometry();redraw()
end
function on_draw()
 geometry();smudge.clear();F.header('CALCULATOR',w)
 if menu then
  F.center('SAVE AND CLOSE',F.box(16,130,w-32,70),28)
  F.center('BACK TO RESUME',F.box(16,218,w-32,60),20)
  F.center('CLEAR ENTRY',F.box(16,320,w-32,70),28)
 else
  if g.op~='' then
   local p=C.display({err='',live=false,operand='',acc=g.acc})..' '..(({['*']='×',['/']='÷',['-']='−'})[g.op] or '+')
   -- The pending band is reserved even when empty, so the pad never moves.
   local size=F.fit(p,w-32,20);F.text(p,w-16-F.width(F.clean(p),size),105,size)
  end
  local value=C.display(g);local size=28
  if g.err=='' then for _,cut in ipairs({56,44,34,26}) do if F.width(F.clean(value),cut)<=w-42 then size=cut;break end end end
  F.text(value,w-21-F.width(F.clean(value),size),139+(91-F.cap[size])//2,size)
  smudge.rect(16,230,w-32,3,true,true)
  for i,b in ipairs(cells) do
   if not K.paint(labels[i],b,keyboard and i==selected,0)then
   local filled=i==20;F.rect(b,filled,false,i%4==0 and 6 or 2)
   local cut=#F.clean(labels[i])>2 and 20 or 28
   -- UTF-8 math symbols are one glyph even though their byte strings are longer.
   if i%4==0 then cut=28 end
   F.center(labels[i],b,cut,filled)
   if keyboard and i==selected then F.rect(F.box(b.x+8,b.y+8,b.w-16,b.h-16),false,false,2) end
   end
  end
 end
 if notice~='' then F.center(notice,F.box(16,h-45,w-32,30),14) end
end
function on_button(btn,pressed)
 if not pressed or closed then return end
 if btn=='page_back' then btn='up' elseif btn=='page_forward' then btn='down' end
 if btn=='back' then if menu then menu=false elseif save() then closed=true;smudge.exit() end
 elseif menu then if btn=='confirm' and save() then closed=true;smudge.exit() elseif btn=='down' then C.press(g,'CE');menu=false end
 elseif btn=='confirm' then press()
 elseif btn=='left' then selected=(selected-2)%20+1;keyboard=true
 elseif btn=='right' then selected=selected%20+1;keyboard=true
 elseif btn=='up' then selected=(selected-5)%20+1;keyboard=true
 elseif btn=='down' then selected=(selected+3)%20+1;keyboard=true end
 redraw()
end
function on_touch(x,y,event)
 if event~='tap' or closed or x<0 or y<0 or x>=w or y>=h then return end
 if menu then
  if y>=130 and y<200 then on_button('confirm',true) elseif y>=218 and y<278 then menu=false;redraw() elseif y>=320 and y<390 then C.press(g,'CE');menu=false;redraw() end;return
 end
 for i,b in ipairs(cells) do if x>=b.x and x<b.x+b.w and y>=b.y and y<b.y+b.h then selected=i;keyboard=false;press();redraw();return end end
end
function on_long_press(x,y)if not closed and y<233 then menu=true;redraw() end end
function on_swipe(dir)on_button(dir,true)end
function on_update()end
function on_exit()save();closed=true;if original then pcall(smudge.set_orientation,original) end end
return {state=function()return g end,cells=function()return cells end,core=C}
