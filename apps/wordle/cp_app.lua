local C=dofile('cp_core.lua')
-- Pure-core archive parsing helpers remain tested; the UI reads archive records
-- directly by offset and can release unused closures on the 75 KiB C3 runtime.
C.parseAnswerLine=nil;C.parseDailyJson=nil;C.putAnswer=nil;C.answerFor=nil
C.lastKnownDay=nil;C.isWord=nil;C.mergeKey=nil
collectgarbage()
local J=dofile('cp_journal.lua')
local G={game=nil,menu=false,menuIndex=1,key=1,notice='',day=-1,results='',closed=false}
local L={};local journal,resultJournal,sessionJournal;local dirty=false;local resultsDirty=false;local finishResult
local K=dofile('keypad.lua')
local keys={'Q','W','E','R','T','Y','U','I','O','P','A','S','D','F','G','H','J','K','L','Del','Z','X','C','V','B','N','M','Enter','Clear','Menu'}
local answerPath,wordPath,wordCount
local resultStart=0
local resultCap=smudge.get_memory().lua_max_kb<128 and 1024 or 8192
local statsLocked=false
local function refresh() smudge.request_update() end
local function validWord(w) return w and #w==5 and w:match('^[A-Z]+$') end
local function detect(localPath,sourcePath)
 local s=smudge.read_file(localPath,5,0);if s and #s==5 then return localPath end
 s=smudge.read_file(sourcePath,5,0);if s and #s==5 then return sourcePath end
end
local function sizeWords(path)
 if not path then return 0 end
 -- Bound all probes and avoid loading the dictionary into the Lua heap.
 local lo,hi=0,32000
 while lo<hi do local mid=(lo+hi+1)//2;local w=smudge.read_file(path,5,(mid-1)*5)
  if w and #w==5 then lo=mid else hi=mid-1 end
 end
 return lo
end
local function isWord(w)
 if G.game and w==G.game.answer then return true end
 local path=G.day<0 and 'words.bin' or wordPath;local count=G.day<0 and 3103 or wordCount
 if not path then return false end
 local lo,hi=0,count
 while lo<hi do local mid=(lo+hi)//2;local s=smudge.read_file(path,5,mid*5)
  if not validWord(s) then return false end
  if s==w then return true elseif s<w then lo=mid+1 else hi=mid end
 end
 return false
end
local function today()
 local d=smudge.get_date();if type(d)=='table' and d.year and d.month and d.day then return C.dayIndex(d.year,d.month,d.day) end
 return -1
end
local function answer(day)
 if day<0 or day>8191 or not answerPath then return nil end
 local w=smudge.read_file(answerPath,5,day*5);return validWord(w) and w or nil
end
local function decode(s,day)
 local text,typing,flag=s:match('^([^\n]+)\n([A-Z]*)\n([01])$');if not text or #typing>5 then return nil end
 local g=C.load(day,text);if not g or g.status~='Playing' and #typing>0 then return nil end;g.typing=typing;g.onDay=flag=='1';return g
end
local function save()
 if dirty and G.game then
  if journal:write(C.save(G.game)..'\n'..G.game.typing..'\n'..(G.game.onDay and '1' or '0')) then dirty=false else G.notice='Save failed; earlier game retained' end
 end
end
local function decodeResults(s)
 local offset,at=s:match('^(%d+)\n()');offset=tonumber(offset)
 if not offset or offset>8191 then return nil end
 local values=s:sub(at);if #values>resultCap or offset+#values>8192 then return nil end
 for i=1,#values do if values:byte(i)&127>8 then return nil end end
 return offset,values
end
local function resultPayload() return resultStart..'\n'..G.results end
local function begin(day,fresh)
 save();if dirty then G.notice='Save failed; cannot switch games yet';refresh();return false end;local a
 if day>=0 then
  if statsLocked or (#G.results>0 and math.max(day,resultStart+#G.results-1)-math.min(day,resultStart)+1>resultCap) then G.notice='Daily history needs PSRAM; practice is available';refresh();return false end
  a=answer(day);if not a then G.notice='No answer for this date. Import archive data.';refresh();return false end
  if not wordPath then G.notice='Import wordle.words before daily play';refresh();return false end
 end
 local tag=day<0 and 'practice-' or 'day-'..day..'-';local payload
 journal,payload=J.open('wordle',64,function(s) return decode(s,day)~=nil end,tag)
 if payload and not fresh then G.game=decode(payload,day)
 else
  if day<0 then local n=math.random(0,3102);a=smudge.read_file('words.bin',5,n*5);if not validWord(a) then G.notice='Practice word bank unavailable';return false end end
  G.game=C.start(day,a);dirty=true
 end
 G.day=day;G.menu=false;G.key=1;G.notice=day<0 and 'Legacy practice bank (not daily Wordle)' or ''
 sessionJournal:write(tostring(day));if finishResult then finishResult() end;save();refresh();return true
end
local function geometry()
 local w,h=smudge.get_bounds();local m=smudge.get_metrics();local y=m.top_padding+m.header_height+6
 local tile=math.min(54,(w-44)//5,(h-y-m.button_hints_height-190)//6)
 local kw=(w-24)//10;local kh=math.min(38,kw);local gy=y+6*(tile+4)+8
 L={w=w,h=h,y=y,tile=tile,x=(w-5*(tile+4)+4)//2,kw=kw,kh=kh,ky=gy,footer=h-m.button_hints_height}
 return L
end
function on_init()
 answerPath=detect('wordle.ans','/.crosspoint/wordle.ans');wordPath=detect('wordle.words','/.crosspoint/wordle.words');wordCount=sizeWords(wordPath)
 local prefix='/.smudge/saved_data/wordle/crossplay-results-'
 if resultCap<8192 then for slot=1,2 do local extra=smudge.read_file(prefix..slot..'.dat',1,resultCap+64);if extra and #extra>0 then statsLocked=true end end end
 if not statsLocked then
  local r;resultJournal,r=J.open('wordle',resultCap+8,function(s) return decodeResults(s)~=nil end,'results-')
  if r then resultStart,G.results=decodeResults(r) end
 end
 local day;sessionJournal,day=J.open('wordle',8,function(s) local n=tonumber(s);return n and math.type(n)=='integer' and n>=-1 and n<=8191 end,'session-')
 local old,msg=sessionJournal:preserve('wordle_state',1024)
 math.randomseed(smudge.millis());geometry()
 local selected=day and tonumber(day) or today()
 if not begin(selected,false) then local why=G.notice;begin(-1,false);G.notice=why end
 if old and not day then G.notice=msg end
 if statsLocked then G.notice='Daily history needs PSRAM; practice is available' end
 on_init=nil;collectgarbage()
end
finishResult=function()
 if G.day<0 or G.game.status=='Playing' then return end
 local value=G.game.status=='Won' and #G.game.rows or 7
 if G.game.onDay then value=value|128 end
 if statsLocked then return end
 if #G.results==0 then resultStart=G.day end
 local start=math.min(resultStart,G.day)
 if math.max(resultStart+#G.results-1,G.day)-start+1>resultCap then G.notice='Daily history needs PSRAM; completed game retained';return end
 if G.day<resultStart then G.results=string.rep('\0',resultStart-G.day)..G.results;resultStart=G.day end
 local prior=C.resultOf(G.results,G.day-resultStart)
 if prior==0 or prior&127==8 then
  G.results=C.putResult(G.results,G.day-resultStart,value)
  resultsDirty=true
 end
end
local function keyAction(n)
 local g=G.game;if not g then return end
 if n==30 then G.menu=true;G.menuIndex=1;refresh();return end
 if n==20 then C.erase(g)
 elseif n==28 then local wasPlaying=g.status=='Playing';local result=C.submit(g,isWord);if wasPlaying and g.status~='Playing' then g.onDay=G.day==today() end;G.notice=({Short='Enter five letters',NotAWord='Not in the word list',Won='Solved!',Lost='Answer: '..g.answer})[result] or '';finishResult()
 elseif n==29 then if g.status=='Playing' then g.typing='' end
 elseif #keys[n]==1 then C.type(g,keys[n]) end
 dirty=true;refresh()
end
local function menuAction(n)
 if n==1 then G.menu=false
 elseif n==2 then begin(today(),false)
 elseif n==3 then begin(math.max(0,(G.day<0 and today() or G.day)-1),false)
 elseif n==4 then begin(math.max(0,G.day+1),false)
 elseif n==5 then begin(-1,true)
 elseif n==6 then local s=C.statsFor(G.results,today()-resultStart);G.notice='Daily: '..s.won..'/'..s.played..' won, streak '..s.streak;G.menu=false
 else save();G.closed=true;smudge.exit() end
 refresh()
end
function on_draw()
 local l=geometry();smudge.clear();local subtitle='Practice'
 if G.day>=0 then local y,m,d=C.dateOfDay(G.day);subtitle=string.format('%04d-%02d-%02d',y,m,d) end
 smudge.header('Wordle',subtitle)
 local g=G.game
 for row=1,6 do for col=1,5 do local x=l.x+(col-1)*(l.tile+4);local y=l.y+(row-1)*(l.tile+4);local mark=g and g.marks[row] and g.marks[row][col] or 0
  smudge.rect(x,y,l.tile,l.tile,mark==4,1,true)
  if mark==3 then smudge.rect_dither(x+2,y+2,l.tile-4,l.tile-4,false) end
  local w=g and (g.rows[row] or (row==#g.rows+1 and g.typing)) or '';local letter=w and w:sub(col,col) or ''
  smudge.text(x+l.tile//2,y+(l.tile-24)//2,letter,10,true,'center',mark~=4)
 end end
 for i,label in ipairs(keys) do local row,col=(i-1)//10,(i-1)%10;local x,y=12+col*l.kw,l.ky+row*(l.kh+4)
  local mark=g and g.keys[label] or 0;local selected=G.key==i and not G.menu
  if not K.paint(label,{x=x,y=y,w=l.kw-3,h=l.kh},selected,mark)then
  smudge.rect(x,y,l.kw-3,l.kh,selected or mark==4,1,true)
  if mark==3 and not selected then smudge.rect_dither(x+2,y+2,l.kw-7,l.kh-4,false) end
  smudge.text(x+(l.kw-3)//2,y+(l.kh-20)//2,label,10,true,'center',not selected and mark~=4)
  if mark==2 then smudge.line(x+3,y+l.kh-4,x+l.kw-6,y+l.kh-4,true) end
  end
 end
 smudge.text(l.w//2,l.ky+3*(l.kh+4)+4,G.notice,10,false,'center',true)
 smudge.button_hints('Menu','Select','Up','Down')
 if G.menu then
  local labels={'Resume','Today','Previous date','Next date','New practice','Daily statistics','Exit'}
  local width=math.min(360,l.w-40);local height=#labels*38+16;local x,y=(l.w-width)//2,(l.h-height)//2
  l.mx=x+8;l.my=y+8;l.mw=width-16
  smudge.rect(x,y,width,height,true,false);smudge.rect(x,y,width,height,false,2,true)
  for i,label in ipairs(labels) do local yy=y+8+(i-1)*38;smudge.rect(x+8,yy,width-16,36,G.menuIndex==i,1,true);smudge.text(l.w//2,yy+8,label,10,true,'center',G.menuIndex~=i) end
 end
end
function on_button(b,pressed)
 if not pressed or G.closed then return end
 if G.menu then
  if b=='back' then G.menu=false elseif b=='confirm' then menuAction(G.menuIndex)
  elseif b=='left' or b=='up' or b=='page_back' then G.menuIndex=(G.menuIndex-2)%7+1
  elseif b=='right' or b=='down' or b=='page_forward' then G.menuIndex=G.menuIndex%7+1 end
 else
  if b=='back' then G.menu=true;G.menuIndex=1 elseif b=='confirm' then keyAction(G.key)
  elseif b=='up' or b=='page_back' then G.key=(G.key-2)%30+1 elseif b=='down' or b=='page_forward' then G.key=G.key%30+1
  elseif b=='left' then G.key=(G.key-11)%30+1 elseif b=='right' then G.key=(G.key+9)%30+1 end
 end
 refresh()
end
function on_touch(x,y,event)
 if event~='tap' or G.closed then return end
 if G.menu then
  local n=(y-L.my)//38+1
  if x>=L.mx and x<L.mx+L.mw and n>=1 and n<=7 and (y-L.my)%38<36 then G.menuIndex=n;menuAction(n);return end
 else
  local row,col=(y-L.ky)//(L.kh+4),(x-12)//L.kw
  if row>=0 and row<3 and col>=0 and col<10 and (x-12)%L.kw<L.kw-3 and (y-L.ky)%(L.kh+4)<L.kh then G.key=row*10+col+1;keyAction(G.key);return end
 end
 if y>=L.footer then on_button(({'back','confirm','left','right'})[math.min(4,x*4//L.w+1)],true) end
end
function on_update()
 if not G.closed then save();if resultsDirty then if resultJournal:write(resultPayload()) then resultsDirty=false else G.notice='Result save failed; retrying' end end end
end
function on_exit() save();if resultsDirty then resultJournal:write(resultPayload()) end;G.closed=true end
return {core=C,begin=begin,resultOffset=function() return resultStart end,state=function() return G end,layout=function() return L end}
