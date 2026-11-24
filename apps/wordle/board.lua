-- Native WordleScreens geometry, Jersey masks and original Lucide key art.
local F=dofile('graphics.lua');local K=dofile('keypad.lua');local V={};local icons={}
local rows={{1,2,3,4,5,6,7,8,9,10},{11,12,13,14,15,16,17,18,19},{21,22,23,24,25,26,27}}
local labels={'Q','W','E','R','T','Y','U','I','O','P','A','S','D','F','G','H','J','K','L','Del','Z','X','C','V','B','N','M','Enter','Clear','Menu'}
function V.init()
 F.init();icons.enter=assert(smudge.read_file('art/tick24.raw',72));icons.erase=assert(smudge.read_file('art/wordle_delete24.raw',72))
end
function V.layout(w,h)
 local landscape=w>h;local tile=landscape and math.min(44,(h-160)//6)or 60
 local side=tile*5+4*8
 local l={w=w,h=h,tile=tile,y=landscape and 95 or 121,x=landscape and 24 or(w-side)//2,footer=h,keys={}}
 local gridBottom=l.y+6*tile+5*8
 local kx=landscape and side+60 or 0;local area=w-kx-(landscape and 16 or 0)
 local kw=(area-32-9*6)//10;local wide=kw*3//2+3;local kh=74;local ky=landscape and 148 or gridBottom+16
 l.ky=ky;l.kh=kh;l.kw=kw
 for row,ids in ipairs(rows)do
  local width=#ids*kw+(#ids-1)*6+(row==3 and 2*(wide+6)or 0)
  local x=kx+(area-width)//2
  if row==3 then l.keys[28]={x=x,y=ky+2*(kh+6),w=wide,h=kh};x=x+wide+6 end
  for _,id in ipairs(ids)do l.keys[id]={x=x,y=ky+(row-1)*(kh+6),w=kw,h=kh};x=x+kw+6 end
  if row==3 then l.keys[20]={x=x,y=ky+2*(kh+6),w=wide,h=kh}end
 end
 l.result={{x=16,y=h-72,w=(w-44)//2,h=56},{x=(w+12)//2,y=h-72,w=(w-44)//2,h=56}}
 return l
end
local function inside(b,x,y)return b and x>=b.x and x<b.x+b.w and y>=b.y and y<b.y+b.h end
function V.touch(l,x,y,hold,action,menu,nextGame)
 if not hold and y<83 then menu();return end
 if V.finished then
  if not hold and inside(l.result[1],x,y)then menu()
  elseif not hold and V.canNext and inside(l.result[2],x,y)and nextGame then nextGame()end
  return
 end
 for id,b in pairs(l.keys)do if inside(b,x,y)then action(hold and id==20 and 29 or id);return end end
end
local function cell(text,b,mark,size)
 local paper=mark==4
 if mark==2 then smudge.rect_dither(b.x,b.y,b.w,b.h,false)
 elseif mark==4 then smudge.rect(b.x,b.y,b.w,b.h,true,true)
 else smudge.rect(b.x,b.y,b.w,b.h,false,mark==3 and 3 or mark==1 and 2 or 1,true)end
 if mark==3 then local dot=b.w>=48 and 10 or 7;smudge.rounded_rect(b.x+b.w-dot-4,b.y+4,dot,dot,dot//2,true,true)end
 if text~=''then F.lineCenter(text,b,size,paper)end
end
function V.draw(l,g,notice,state,action)
 on_long_press=function(x,y)if not state.closed and not state.menu then V.touch(l,x,y,true,action)end end
 smudge.clear();V.finished=g and g.status~='Playing';V.canNext=state.canNext
 local title=V.finished and g.answer or'WORDLE';local right=V.finished and((g.status=='Won'and tostring(#g.rows)or'X')..' / 6')or(state.dateLabel or'PRACTICE')
 smudge.rect(0,0,l.w,76,true,true);F.text(title,16,19,30,true);F.text(right,l.w-16-F.inkWidth(right,10),52,10,true);smudge.rect(0,80,l.w,3,true,true)
 if notice~=''and not notice:find('Legacy practice',1,true)and notice~='Solved!'and not notice:find('Answer: ',1,true)then F.lineCenter(F.fit(notice,l.w-32,10),{x=0,y=95,w=l.w,h=22},10)end
 for row=1,6 do for col=1,5 do
  local word=g and(g.rows[row]or row==#g.rows+1 and g.typing)or'';local letter=word and word:sub(col,col)or''
  cell(letter,{x=l.x+(col-1)*(l.tile+8),y=l.y+(row-1)*(l.tile+8),w=l.tile,h=l.tile},g and g.marks[row]and g.marks[row][col]or(letter~=''and 1 or 0),30)
 end end
 if V.finished then
  local top=l.y+6*l.tile+5*8+16
  F.lineCenter(g.status=='Won'and('SOLVED IN '..#g.rows)or'NOT SOLVED',{x=0,y=top,w=l.w,h=l.result[1].y-top},30)
  for i,b in ipairs(l.result)do
   if i==2 and not V.canNext then smudge.rect_dither(b.x,b.y,b.w,b.h,true)
   else smudge.rect(b.x,b.y,b.w,b.h,i==2,1,true)end
   F.lineCenter(i==1 and'ARCHIVE'or'NEXT',b,20,i==2)
  end
  return
 end
 for row,ids in ipairs(rows)do for _,id in ipairs(ids)do local mark=g and g.keys[labels[id]]or 0;if not K.paint(labels[id],l.keys[id],false,mark)then cell(labels[id],l.keys[id],mark,20)end end end
 for _,id in ipairs({28,20})do
  local b=l.keys[id];local enabled=id~=28 or g and#g.typing==5
  if not K.paint(id==28 and 'OK' or 'Del',b,false,0,enabled,id==20 and 'delete' or 'text')then
  if id==28 and enabled then smudge.rounded_rect(b.x,b.y,b.w,b.h,8,true,true)
  else
   if not enabled then F.roundDither(b.x,b.y,b.w,b.h,8,false)end
   smudge.rounded_rect(b.x,b.y,b.w,b.h,8,false,true,enabled and 2 or 1)
  end
  smudge.bitmap(b.x+(b.w-24)//2,b.y+(b.h-24)//2,24,24,id==28 and icons.enter or icons.erase,id==28 and enabled)
  end
 end
end
function V.drawMenu(l,state)
 smudge.clear();F.header('WORDLE',l.w)
 local m=state.menuModel;local body={x=16,y=119,w=l.w-32,h=l.h-135}
 local wide=l.w>l.h;local width=wide and (l.w-48)//2 or body.w
 F.native(m.date or 'NONE YET',{x=16,y=119,w=l.w-32,h=60},30,'left')
 F.native(m.status,{x=16,y=179,w=l.w-32,h=30},20,'left')
 smudge.rect(16,223,l.w-32,3,true,true)
 local stats=m.stats
 F.native(stats.played..' PLAYED   '..stats.won..' WON   STREAK '..stats.streak,{x=16,y=237,w=l.w-32,h=24},10,'left')
 local chartTop=wide and 265 or 285;local rowH=wide and 26 or 30;local gap=wide and 3 or 8
 local most=1;for i=1,6 do most=math.max(most,stats.wins[i])end
 local barMax=width-28-44-16
 for i=1,6 do local y=chartTop+(i-1)*(rowH+gap)
  F.native(tostring(i),{x=16,y=y,w=28,h=rowH},20,'center',false,1,true)
  local x=52;smudge.rect(x,y,barMax,rowH,false,1,true)
  local n=stats.wins[i];if n>0 then smudge.rect(x,y,(barMax*n+most-1)//most,rowH,true,true)end
  F.native(tostring(n),{x=x+barMax+8,y=y,w=44,h=rowH},20,'center',false,1,true)
 end
 F.native('GUESSES PER WIN',{x=16,y=chartTop+6*(rowH+gap)+4,w=width,h=24},10,'center')
 local labels={'HOW TO PLAY','ARCHIVE',m.upToDate and 'ALL CAUGHT UP'or'GET PUZZLES'}
 local values={'',tostring(m.puzzles),m.upToDate and ''or'SD CARD'}
 l.hero=m.day>=0 and {x=16,y=119,w=l.w-32,h=96}or nil;l.menu={}
 local x=wide and l.w-width-16 or 16
 for i,label in ipairs(labels)do
  local b={x=x,y=l.h-214+(i-1)*66,w=width,h=62,enabled=i~=3 or not m.upToDate};l.menu[i]=b
  smudge.rect(b.x,b.y,b.w,b.h,false,1,true)
  F.native(label,{x=b.x+12,y=b.y,w=b.w-24,h=b.h},20,'left')
  if values[i]~=''then F.native(values[i],{x=b.x+12,y=b.y,w=b.w-24,h=b.h},20,'right')end
 end
end
function V.drawHelp(l)
 smudge.clear();F.header('HOW TO PLAY',l.w)
 local x,y,w=16,95,l.w-32
 F.native('SIX GUESSES FOR ONE WORD',{x=x,y=y,w=w,h=42},20,'left');y=y+48
 F.native('Type a five-letter word and tap the tick. Each letter is then marked.',{x=x,y=y,w=w,h=42},10,'left',false,2);y=y+66
 for _,example in ipairs({{'P',4,'In the word, in this spot'},{'L',3,'In the word, somewhere else'},{'C',2,'Not in the word'}})do
  cell(example[1],{x=x,y=y,w=60,h=60},example[2],30)
  F.native(example[3],{x=x+76,y=y+19,w=w-76,h=21},10,'left');y=y+76
 end
 y=y+8;F.native('A new word every day. Past days are in the archive.',{x=x,y=y,w=w,h=42},10,'left',false,2)
end
local function arrow(b,plus,enabled)
 if not enabled then
  smudge.rect_dither(b.x,b.y,b.w,b.h,false)
  smudge.rect_dither(b.x,b.y,b.w,1,true);smudge.rect_dither(b.x,b.y+b.h-1,b.w,1,true)
  smudge.rect_dither(b.x,b.y+1,1,b.h-2,true);smudge.rect_dither(b.x+b.w-1,b.y+1,1,b.h-2,true)
 else smudge.rect(b.x,b.y,b.w,b.h,false,1,true)end
 F.native(plus and '+'or'-',{x=b.x+4,y=b.y+2,w=b.w-8,h=b.h-4},'s13','center')
end
function V.drawArchive(l,model)
 F.serifInit();smudge.clear();F.header('ARCHIVE',l.w,model.played..' PLAYED',true)
 local months={'JANUARY','FEBRUARY','MARCH','APRIL','MAY','JUNE','JULY','AUGUST','SEPTEMBER','OCTOBER','NOVEMBER','DECEMBER'}
 local wide=l.w>l.h;local rowWidth=wide and 380 or l.w-24
 local c={cells={},month=model.month,year=model.year}
 for i=1,2 do
  local y=95+(i-1)*66;local right=12+rowWidth-12
  smudge.rect(12,y,rowWidth,62,false,1,true)
  F.native(i==1 and 'YEAR'or'MONTH',{x=24,y=y,w=rowWidth-12,h=62},'s13','left')
  local minus={x=right-52-6-150-6-52,y=y+6,w=52,h=50};local plus={x=right-52,y=y+6,w=52,h=50}
  if i==1 then c.prevYear=minus;c.nextYear=plus else c.prevMonth=minus;c.nextMonth=plus end
  F.native(i==1 and tostring(model.year)or months[model.month],{x=right-52-6-150,y=y,w=150,h=62},'s13','center')
  arrow(minus,false,i==1 and model.prevYear or model.prevMonth);arrow(plus,true,i==1 and model.nextYear or model.nextMonth)
 end
 c.today={x=12,y=l.h-68,w=l.w-24,h=52};smudge.rect(c.today.x,c.today.y,c.today.w,c.today.h,true,true)
 F.native('TODAY',{x=16,y=l.h-66,w=l.w-32,h=48},'s13','center',true)
 local left=wide and 412 or 12;local availWidth=wide and l.w-left-12 or l.w-24
 local availY=wide and 95 or 239;local availBottom=l.h-68
 local cellSize=(availWidth-18)//7
 if wide then cellSize=math.min(cellSize,(availBottom-availY-43)//6)end
 local block=28+6*cellSize+15
 local bodyY=availY+(availBottom-availY-block)//2
 if wide then left=left+(availWidth-(7*cellSize+18))//2 end
 local initials={'S','M','T','W','T','F','S'}
 for col=0,6 do F.native(initials[col+1],{x=left+col*(cellSize+3),y=bodyY,w=cellSize,h=22},'s10','center')end
 for i=1,42 do local day=model.cells[i];local b={x=left+(i-1)%7*(cellSize+3),y=bodyY+28+(i-1)//7*(cellSize+3),w=cellSize,h=cellSize,day=day.day,available=day.available,index=day.index};c.cells[i]=b
  if day.day>0 then
   if day.available then smudge.rect(b.x,b.y,b.w,b.h,false,1,true)end
   if day.today then smudge.rect(b.x,b.y,b.w,b.h,false,4,true)end
   F.native(tostring(day.day),{x=b.x,y=b.y+6,w=b.w,h=22},'s10','center')
   if day.played then
    local top=b.y+36;local room=b.y+cellSize-5-top;local mark=math.min(room,cellSize//2)
    if mark>0 then
     local mx,my=b.x+(cellSize-mark)//2,top+(room-mark)//2
     if day.lost then local inset=mark//5;F.nativeLine(mx+inset,my+inset,mx+mark-inset,my+mark-inset,3);F.nativeLine(mx+mark-inset,my+inset,mx+inset,my+mark-inset,3)
     elseif not day.finished then smudge.rounded_rect(mx+3,my+3,mark-6,mark-6,mark//2,false,true,1)
     else
      -- The original four-arm sparkle, rasterized through the production triangle API.
      local cx,cy,r=mx+mark//2,my+mark//2,mark//2;local t=r//3
      F.triangle(cx,cy-r,cx-t,cy,cx+t,cy);F.triangle(cx,cy+r,cx-t,cy,cx+t,cy)
      F.triangle(cx-r,cy,cx,cy-t,cx,cy+t);F.triangle(cx+r,cy,cx,cy-t,cx,cy+t)
     end
    end
   end
  end
 end
 l.calendar=c
end
function V.drawImport(l,message)
 smudge.clear();F.header('GET PUZZLES',l.w)
 F.native('IMPORT THE ORIGINAL ARCHIVE',{x=16,y=119,w=l.w-32,h=50},20,'left')
 F.native('Copy wordle.ans and wordle.words to the app folder or /.crosspoint/.',{x=16,y=185,w=l.w-32,h=84},10,'left',false,4)
 F.native(message or 'The existing games and daily history are kept.',{x=16,y=295,w=l.w-32,h=63},10,'left',false,3)
end
return V
