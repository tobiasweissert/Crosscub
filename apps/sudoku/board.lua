-- CrossPlay Sudoku screen geometry and Jersey masks; rules remain in app.lua.
local F=dofile('graphics.lua');local K=dofile('keypad.lua')
local V={level=0,focus=1,help=0,result=false,resultFocus=1}
local levels={'EASY','MEDIUM','HARD','EXPERT'}
local lessons={
 {'THE RULE','EVERY ROW, COLUMN AND BOX HOLDS 1 TO 9.','NO DIGIT TWICE IN ANY OF THEM.','123456789',nil,0},
 {'WRITING','PICK A DIGIT, THEN TAP A CELL TO WRITE IT.','EVERY COPY OF IT IS MARKED.','A.C.....I','A.C.5...I',5},
 {'CLEARING','TAP YOUR OWN DIGIT AGAIN TO CLEAR IT.','TAP A CLUE TO PICK ITS DIGIT UP.','A.C.5...I','A.C.....I',5},
 {'PENCIL','HOLD A CELL TO PENCIL A DIGIT IN.','HOLD AGAIN TO RUB IT OUT.','A.C.....I','A.C.p...I',0},
 {'MISTAKES','A DIGIT THAT CLASHES TURNS BLACK.','HINT NAMES A CELL YOU CAN SOLVE.','A.C.5...I','A.C!!...I',5}}
local function clock(seconds)
 if seconds>=3600 then return (seconds//3600)..':'..string.format('%02d:%02d',seconds//60%60,seconds%60) end
 return (seconds//60)..':'..string.format('%02d',seconds%60)
end
local function wrap(text,width,size)
 local lines,line={},''
 for word in text:gmatch('%S+') do local next=line=='' and word or line..' '..word
  if F.width(next,size)>width and line~='' then lines[#lines+1]=line;line=word else line=next end
 end
 if line~='' then lines[#lines+1]=line end;return lines
end
local function frame(x,y,w,h,n,paper)
 smudge.rect(x,y,w,n,true,not paper);smudge.rect(x,y+h-n,w,n,true,not paper)
 smudge.rect(x,y,n,h,true,not paper);smudge.rect(x+w-n,y,n,h,true,not paper)
end
local function button(label,b,selected,size)
 smudge.rect(b.x,b.y,b.w,b.h,selected,selected and true or 1,true)
 F.center(label,b.x,b.y,b.w,b.h,size or 20,selected)
end
local function inside(b,x,y)return b and x>=b.x and x<b.x+b.w and y>=b.y and y<b.y+b.h end
local function corners(x,y,w,h,arm,n,paper)
 for _,dx in ipairs({0,w-arm}) do for _,dy in ipairs({0,h-n}) do smudge.rect(x+dx,y+dy,arm,n,true,not paper) end end
 for _,dx in ipairs({0,w-n}) do for _,dy in ipairs({0,h-arm}) do smudge.rect(x+dx,y+dy,n,arm,true,not paper) end end
end
local function header(w,right,title)
 smudge.rect(0,0,w,76,true,true);F.text(title or 'SUDOKU',16,19,30,true)
 F.text(right,w-16-F.width(right,20),36,20,true);smudge.rect(0,80,w,3,true,true)
end
function V.init(g)F.init();V.level=g.level;V.result=false end
function V.layout(w,h)
 local landscape=w>h
 local size=landscape and math.min(50,(h-129)//9) or math.min(50,(w-30)//9)
 local outer=9*size+18;local left=landscape and 6 or (w-outer)//2
 local l={w=w,h=h,x=left+9,y=104,size=size,outer=outer,left=left,footer=h,buttons={},keys={},menu={}}
 local key=landscape and 56 or 69;local px=landscape and w-key*3-16 or left
 local py=landscape and 95 or h-16-key*3
 for d=1,9 do l.keys[d]={x=px+(d-1)%3*key,y=py+(d-1)//3*key,w=key,h=key} end
 local rx=landscape and left+outer+16 or left+key*3+14
 local ry=landscape and py+key*3+16 or py
 local rw=landscape and w-rx-16 or left+outer-rx
 local rh=landscape and (h-ry-16-12)//3 or 61
 local gap=landscape and 6 or (key*3-rh*3)//2
 l.status={x=rx,y=ry,w=rw,h=rh}
 l.buttons[3]={x=rx,y=ry+rh+gap,w=rw,h=rh}
 l.buttons[2]={x=rx,y=ry+2*(rh+gap),w=rw,h=rh}
 l.buttons[4]={x=0,y=0,w=w,h=76}
 l.play={x=16,y=h-222,w=w-32,h=52}
 l.difficulty={x=16,y=h-158,w=w-32,h=62}
 l.howto={x=16,y=h-92,w=w-32,h=62}
 l.again={x=16,y=h-68,w=w-32,h=52}
 l.grid={x=16,y=h-132,w=w-32,h=52}
 return l
end
local function miniature(l,g,top,bottom,left,width)
 top=top or 186;bottom=bottom or l.play.y-12;left=left or 0;width=width or l.w
 local side=math.min(width-56,math.max(0,bottom-top-24));local cell=side//9;side=cell*9
 local x,y=left+(width-side)//2,top+(bottom-top-side)//2
 corners(x-12,y-12,side+24,side+24,16,3)
 for i=0,9,3 do smudge.rect_dither(x+i*cell-(i==9 and 1 or 0),y,1,side,true);smudge.rect_dither(x,y+i*cell-(i==9 and 1 or 0),side,1,true) end
 if not g.given then return end
 for i=1,81 do local n=g.given[i]~=0 and 2 or g.value[i]~=0 and cell//3
  if n then smudge.rect(x+(i-1)%9*cell+n,y+(i-1)//9*cell+n,cell-2*n,cell-2*n,true,true) end
 end
end
local function drawMenu(l,g)
 header(l.w,levels[V.level+1]);local empty=81
 if g.value then for _,value in ipairs(g.value) do if value~=0 then empty=empty-1 end end end
 F.text(g.solved and 'LAST ONE SOLVED' or g.given and V.level~=g.level and levels[V.level+1]..', STARTING FRESH' or g.given and empty..' LEFT' or 'NOT STARTED',16,123,20)
 local total=0;for _,count in ipairs(g.counts) do total=total+count end
 local best=g.best[V.level+1]
 local record=total..' SOLVED   '..(best>=0 and 'BEST '..clock(best) or 'NO '..levels[V.level+1]..' TIME YET')
 F.text(record,16,157,10)
 miniature(l,g)
 local resume=g.given and not g.solved and V.level==g.level
 button(resume and 'RESUME' or 'NEW PUZZLE',l.play,true)
 local function row(b,label,value,selected)
  smudge.rect(b.x,b.y,b.w,b.h,true,selected);frame(b.x,b.y,b.w,b.h,1)
  F.text(label,b.x+12,b.y+(b.h-25)//2+1,20,selected)
  if value then F.text(value,b.x+b.w-12-F.width(value,20),b.y+(b.h-25)//2+1,20,selected)end
 end
 row(l.difficulty,'DIFFICULTY',levels[V.level+1],V.focus==2)
 row(l.howto,'HOW TO PLAY',nil,V.focus==3)

end
local function face(text,armed,x,y,side)
 local cell=side//3;local grid=cell*3;x=x+(side-grid)//2;y=y+(side-grid)//2;side=grid
 for i=1,9 do local xx,yy=x+(i-1)%3*cell,y+(i-1)//3*cell;local mark=text:sub(i,i)
  local clue=mark>='A' and mark<='I';local clash=mark=='!'
  if clue then smudge.rect_dither(xx,yy,cell,cell,false) elseif clash then smudge.rect(xx,yy,cell,cell,true,true) end
  frame(xx,yy,cell,cell,1)
  local digit=clue and tostring(mark:byte()-64) or clash and tostring(armed) or mark:match('%d')
  if digit then F.center(digit,xx,yy,cell,cell,cell>=64 and 30 or 20,clash)
   if tonumber(digit)==armed then corners(xx,yy,cell,cell,cell//4,3,clash) end
  elseif mark=='p' then for _,d in ipairs({2,6,9}) do local n=(cell-8)//3;F.center(tostring(d),xx+4+(d-1)%3*n,yy+4+(d-1)//3*n,n,n,10) end end
 end
 frame(x-3,y-3,side+6,side+6,3)
end
local function help(l)
 header(l.w,V.help..' OF 5','HOW TO PLAY');local lesson=lessons[V.help]
 F.text(lesson[1],16+(l.w-32-F.width(lesson[1],30))//2,122,30)
 local lines=wrap(lesson[2],l.w-32,20)
 for i,line in ipairs(lines) do F.center(line,16,163+(i-1)*42,l.w-32,42,20) end
 local detailY=l.h-101
 F.text(lesson[3],16+(l.w-32-F.width(lesson[3],10))//2,detailY+4,10)
 button(V.help==5 and 'GOT IT' or 'NEXT',l.again,true)
 local top,bottom=259,detailY-12
 if lesson[5] then
  local side=math.min(l.w-32,(bottom-top-54)//2)
  if l.w>l.h then
   side=math.min((l.w-112)//2,bottom-top);side=side//3*3
   local x=(l.w-2*side-54)//2;face(lesson[4],lesson[6],x,top,side);face(lesson[5],lesson[6],x+side+54,top,side)
   smudge.rect(x+side+10,top+side//2-2,34,4,true,true)
   smudge.line(x+side+34,top+side//2-10,x+side+44,top+side//2,true);smudge.line(x+side+44,top+side//2,x+side+34,top+side//2+10,true)
  else
   local x=(l.w-side)//2;face(lesson[4],lesson[6],x,top,side);face(lesson[5],lesson[6],x,top+side+54,side)
   local cy=top+side+27;smudge.rect(l.w//2-2,cy-27,5,27,true,true)
   for n=0,8 do local half=9-n;smudge.rect(l.w//2-half,cy+n,half*2,1,true,true)end
  end
 else local side=math.min(l.w-32,bottom-top);face(lesson[4],0,(l.w-side)//2,top+(bottom-top-side)//2,side) end
end
local function result(l,g)
 local landscape=l.w>l.h;local textWidth=landscape and l.w//2-48 or l.w-32
 header(l.w,levels[g.level+1],'SOLVED');F.text(clock(g.elapsed),16,136,30)
 local best=g.best[g.level+1]
 local under=g.hinted and (g.hintsUsed<0 and 'HINTED - NO TIME RECORDED' or g.hintsUsed..(g.hintsUsed==1 and ' HINT, SO NO TIME RECORDED' or ' HINTS, SO NO TIME RECORDED')) or g.newBest and 'YOUR BEST '..levels[g.level+1]..' YET' or best>=0 and 'YOUR BEST IS '..clock(best) or 'UNAIDED'
 local shown=under
 if F.width(shown,20)>textWidth then
  while #shown>0 and F.width(shown..'...',20)>textWidth do shown=shown:sub(1,-2)end
  shown=shown..'...'
 end
 F.text(shown,16,200,20)
 smudge.rect(16,233,textWidth,3,true,true)
 local clues=0;for _,value in ipairs(g.given) do if value~=0 then clues=clues+1 end end
 local names={'ONLY DIGIT','ONLY CELL','LOCKED','CELL PAIR','DIGIT PAIR','CELL TRIO','DIGIT TRIO','RECTANGLE','Y CHAIN','SWORDFISH'}
 local detail=clues..' CLUES   HARDEST '..(names[g.hardest] or 'NONE')..'   '..g.counts[g.level+1]..' '..levels[g.level+1]..' SOLVED'
 if F.width(detail,10)>textWidth then while #detail>0 and F.width(detail..'...',10)>textWidth do detail=detail:sub(1,-2)end;detail=detail..'...'end
 F.text(detail,16,246,10)
 if landscape then miniature(l,g,116,l.grid.y-12,l.w//2+8,l.w//2-24)else miniature(l,g,266,l.grid.y-12)end
 button('ANOTHER',l.again,V.resultFocus==1);button('BACK TO THE GRID',l.grid,V.resultFocus==2)
end
local function related(i,j)return (i-1)//9==(j-1)//9 or (i-1)%9==(j-1)%9 or (i-1)//27==(j-1)//27 and ((i-1)%9)//3==((j-1)%9)//3 end
local function drawBoard(l,g)
 header(l.w,levels[g.level+1]);smudge.rect(l.left,95,l.outer,l.outer,true,true);smudge.rect(l.x,l.y,l.size*9,l.size*9,true,false)
 local empty=81
 for i=1,81 do local x,y=l.x+(i-1)%9*l.size,l.y+(i-1)//9*l.size;local value=g.value and g.value[i] or 0
  local clash=false;local taken=0
  if g.value then for j=1,81 do if j~=i and related(i,j) and g.value[j]~=0 then
   taken=taken|(1<<(g.value[j]-1));if value~=0 and g.value[j]==value then clash=true end
  end end end
  if clash then smudge.rect(x,y,l.size,l.size,true,true)
  elseif g.given and g.given[i]~=0 then smudge.rect_dither(x,y,l.size,l.size,false) end
  if value~=0 then
   empty=empty-1;F.center(tostring(value),x,y,l.size,l.size,l.size>=42 and 30 or 20,clash)
   if value==g.digit then corners(x,y,l.size,l.size,l.size>=50 and 13 or math.min(13,l.size//4),3,clash) end
  elseif g.notes then for d=1,9 do if g.notes[i]&(1<<(d-1))&~taken~=0 then
   F.center(tostring(d),x+4+(d-1)%3*((l.size-8)//3),y+4+(d-1)//3*((l.size-8)//3),(l.size-8)//3,(l.size-8)//3,10)
  end end end
  if i==g.pendingHint then
   frame(x+3,y+3,l.size-6,l.size-6,2);corners(x+7,y+7,l.size-14,l.size-14,math.max(4,l.size//5),3)
  elseif i==g.cell and g.notice~='' then frame(x,y,l.size,l.size,4) end
 end
 for i=1,8 do local weight=i%3==0 and 5 or 1;local offset=i*l.size-weight//2
  smudge.rect(l.x+offset,l.y,weight,l.size*9,true,true);smudge.rect(l.x,l.y+offset,l.size*9,weight,true,true)
 end
 for d,b in ipairs(l.keys) do
  local count=0;if g.value then for _,value in ipairs(g.value) do if value==d then count=count+1 end end end
  if not K.paint(tostring(d),b,d==g.digit,0,count<9)then
   if count>=9 then smudge.rect_dither(b.x,b.y,b.w,b.h,false) end
   frame(b.x,b.y,b.w,b.h,d==g.digit and 4 or 1);F.center(tostring(d),b.x,b.y,b.w,b.h,30)
  end
 end
 local label=g.job and 'MAKING ONE' or g.solved and 'SOLVED' or g.notice~='' and string.upper(g.notice) or empty..' LEFT'
 local font=F.width(label,20)>l.status.w-12 and 10 or 20
 while F.width(label,font)>l.status.w-12 and #label>0 do label=label:sub(1,-2) end
 button(label,l.status,g.solved,font)
 local function rail(label,b,disabled)
  if disabled then
   smudge.rect_dither(b.x,b.y,b.w,b.h,false)
   smudge.rect_dither(b.x,b.y,b.w,1,true);smudge.rect_dither(b.x,b.y+b.h-1,b.w,1,true)
   smudge.rect_dither(b.x,b.y,1,b.h,true);smudge.rect_dither(b.x+b.w-1,b.y,1,b.h,true)
   F.center(label,b.x,b.y,b.w,b.h,20)
  else button(label,b,false)end
 end
 rail('UNDO',l.buttons[3],#g.history==0 or g.job);rail('HINT',l.buttons[2],g.solved or g.job)
end
function V.draw(l,g)
 smudge.clear()
 if V.result then result(l,g)
 elseif V.help>0 then help(l)
 elseif g.menu then drawMenu(l,g) else drawBoard(l,g) end
end
local function play(g,menuSelect)
 g.menuIndex=g.given and not g.solved and V.level==g.level and 1 or V.level+2;menuSelect()
end
function V.button(g,b,menuSelect,refresh)
 if V.result then
  if b=='back' then V.result=false elseif b=='confirm' then V.result=false;if V.resultFocus==1 then g.menuIndex=g.level+2;menuSelect() end
  else V.resultFocus=3-V.resultFocus end
 elseif V.help>0 then
  if b=='back' then V.help=0 elseif b=='confirm' or b=='down' or b=='right' then V.help=V.help==5 and 0 or V.help+1 end
 elseif g.menu then
  if b=='confirm' then if V.focus==1 then play(g,menuSelect) elseif V.focus==2 then V.level=(V.level+1)%4 else V.help=1 end
  elseif b=='up' or b=='left' then V.focus=(V.focus-2)%3+1 elseif b=='down' or b=='right' then V.focus=V.focus%3+1 else return false end
 else return false end
 refresh();return true
end
function V.touch(l,g,x,y,event,enter,action,menuSelect,refresh,clearHint)
 if V.result then
  if event=='tap' then
   if inside(l.again,x,y) then V.result=false;g.menuIndex=g.level+2;menuSelect()
   elseif inside(l.grid,x,y) then V.result=false end
   refresh()
  end;return
 end
 if V.help>0 then if event=='tap' then if y<83 then V.help=0 elseif y>=l.h-68 then V.help=V.help==5 and 0 or V.help+1 end;refresh() end;return end
 if g.menu then
  if event~='tap' then return end
  if inside(l.play,x,y) then play(g,menuSelect)
  elseif inside(l.difficulty,x,y) then V.level=(V.level+1)%4;V.focus=2
  elseif inside(l.howto,x,y) then V.help=1 end
  refresh();return
 end
 if event~='tap' and event~='hold' then return end
 local c,r=(x-l.x)//l.size,(y-l.y)//l.size
 if c>=0 and c<9 and r>=0 and r<9 then g.cell=r*9+c+1;enter(g.cell,event=='hold' or g.notesMode);return end
 if event~='tap' then return end
 if g.solved and inside(l.status,x,y) then V.result=true;refresh();return end
 for digit,b in ipairs(l.keys) do if inside(b,x,y) then clearHint();g.digit=digit;refresh();return end end
 for id,b in pairs(l.buttons) do if inside(b,x,y) then action(id);return end end
end
return V
