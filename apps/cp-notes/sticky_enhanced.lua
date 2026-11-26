local N=dofile('core.lua');local S=dofile('storage.lua');local F=dofile('graphics.lua')
local doc,lines,index,selected,screen,choice='',{},1,1,'library',1
-- Only a card loaded through load() owns the document and journal sequence.
local loaded=false
local editor
local hits={};local summaries={};local pages={};local viewPage=1;local pageAnchor=0;local closed=false;local original;local offset=0;local notice='';local w,h=480,800;local margin=25;local pageRows=8;local pending=''
local function save()
 if not loaded then return true end
 collectgarbage('collect')
 if not S.save(doc) then notice='Save failed. Keep app open and retry.';return false end
 return true
end
local function load(index_)
 index=index_;selected=1;offset=0;pageAnchor=0;viewPage=1;doc='';lines={};collectgarbage('collect');S.scope(index)
 local restored=S.load(N.validate)
 if restored then doc=restored else
  local raw=smudge.read_file('note'..index..'.md',3073)
  doc=raw and N.validate(raw) or ''
  if raw and not N.validate(raw) then notice='Import rejected: over 3072 bytes/97 lines or NUL.' end
 end
 lines=N.parse(doc,true);loaded=true;screen='note'
end
local function redraw()smudge.request_update()end
local options={'Resume','Clear checked lines','Convert to checklist','Convert to page','Import from note file','Help','Save and close','Edit / dictate'}
local function change(new)
 summaries[index]=nil;pageAnchor=0;viewPage=1;S.last=nil;offset=0;doc=new;lines=nil;collectgarbage('collect');lines=N.parse(doc,true);selected=math.min(selected,#lines);redraw()
end
local function operate()
 if not loaded then screen='library';redraw();return end
 if choice==1 then screen='note'
 elseif choice==2 or choice==5 then pending=choice==2 and 'clear' or 'import';screen='confirm';choice=1
 elseif choice==3 then change(N.tolist(doc,lines));screen='note'
 elseif choice==4 then change(N.strip(doc,lines));screen='note'
 elseif choice==6 then screen='help'
 elseif choice==8 then
  if smudge.keyboard_snapshot then
   editor=dofile('dictation_editor.lua')(doc,function(value)
    if not S.save(value) then return false end
    change(value);editor=nil;screen='note';return true
   end,N.validate);screen='edit'
  else notice='Editing requires updated Sticky firmware' end
 elseif save() then loaded=false;screen='library';choice=index end
 redraw()
end
function on_init()
 closed=false;F.init();local ok,o=pcall(smudge.get_orientation);if ok then original=o end;w,h=smudge.get_bounds();redraw()
end
local function hit(b,kind,value)F.hit(hits,b,kind,value);return b end
local function button(label,b,kind,value,outline)
 F.rect(b,not outline,false,1);F.center(label,F.box(b.x,b.y+1,b.w,b.h),F.fit(label,b.w-20,20),not outline);hit(b,kind,value)
end
local function summary(i)
 if summaries[i] then return summaries[i] end
 S.scope(i);local text=S.load(N.validate);if not text then local raw=smudge.read_file('note'..i..'.md',3073);text=N.validate(raw) and raw or '' end
 local parsed=N.parse(text,true);local item={total=0,done=0,preview=''}
 for _,v in ipairs(parsed) do local row=N.row(v);if row.mark then item.total=item.total+1;if row.checked then item.done=item.done+1 end elseif item.preview=='' then item.preview=text:sub(row.text,row.last) end end
 summaries[i]=item;S.scope(index);return item
end
local function progress(b,done,total)
 F.rect(b,false,false,1);if done>0 and total>0 then F.rect(F.box(b.x,b.y,math.max(3,b.w*done//total),b.h),true) end
end
local function wrap(text,width,size)
 local result,current={},''
 for word in text:gmatch('%S+') do
  if current~='' and F.width(current..' '..word,size)>width then result[#result+1]=current;current='' end
  if F.width(word,size)>width then
   for char in word:gmatch('[%z\1-\127\194-\244][\128-\191]*') do
    if current~='' and F.width(current..char,size)>width then result[#result+1]=current;current='' end
    current=current..char
   end
  else current=current=='' and word or current..' '..word end
 end
 if current~='' then result[#result+1]=current end
 if #result==0 then result[1]='' end;return result
end
local function plan(top,bottom,reserved)
 pages={{}};local y=top;local total,done=0,0;local maxLines=math.max(1,(bottom-top-32)//42)
 for i,v in ipairs(lines) do
  local row=N.row(v);if row.mark then total=total+1;if row.checked then done=done+1 end end
  local text=N.slice(doc:sub(row.text,row.last),i==selected and offset+1 or 1,3072)
  local broken=wrap(text,w-32-(row.mark and 52 or 0),20)
  for start=1,#broken,maxLines do
   local chunk={};for n=start,math.min(#broken,start+maxLines-1) do chunk[#chunk+1]=broken[n] end
   local height=math.max(row.mark and 72 or 42,#chunk*42+(row.mark and 32 or 12))
   if y+height>bottom and #pages[#pages]>0 then pages[#pages+1]={};y=top end
   local block={index=i,lines=chunk,row=row,y=y,h=height};pages[#pages][#pages[#pages]+1]=block;y=y+height
  end
 end
 if #pages>1 and not reserved then return plan(top,bottom-33,true)end
 if #pages==1 and #pages[1]>0 then
  local list=pages[1];local last=list[#list];local tasks=0;for _,block in ipairs(list) do if block.row.mark then tasks=tasks+1 end end
  local grow=tasks>0 and math.min(36,math.max(0,(bottom-last.y-last.h)//tasks)) or 0;local shift=0
  for _,block in ipairs(list) do block.y=block.y+shift;if block.row.mark then block.h=block.h+grow;shift=shift+grow end end
 end
 if pageAnchor~=selected then
  -- Find the first continuation page of a selected long paragraph.
  for page,list in ipairs(pages) do local found=false;for _,block in ipairs(list) do if block.index==selected then found=true;break end end;if found then viewPage=page;break end end
  pageAnchor=selected
 end
 viewPage=math.max(1,math.min(viewPage,#pages));return total,done
end
function on_draw()
 w,h=smudge.get_bounds();
 if editor then editor.draw('Note '..index,F);return end
 hits={};smudge.clear();local title=screen=='library' and 'NOTES' or 'NOTE '..index
 if screen=='library' then F.header(title,w) else F.header('',w);F.text(title,16,26,20,true) end
 if screen~='library' then
  smudge.rect(w-79,5,66,66,true,false);smudge.bitmap(w-62,22,32,32,F.settings,false)
  hit(F.box(w-79,5,66,66),'menu')
 end
 local footer=F.box(16,h-68,w-32,52)
 if screen=='library' then
  local capacity=math.max(1,(h-68-12-119+12)//116);local start=(choice-1)//capacity*capacity+1
  for i=start,math.min(8,start+capacity-1) do
   local item=summary(i);local y=119+(i-start)*116;local badge=F.box(16,y+14,76,76)
   if item.total>0 then F.rect(badge,true);F.center(item.done..'/'..item.total,badge,F.fit(item.done..'/'..item.total,68,10),true)
   else F.rect(badge,false,false,3);for n=0,2 do smudge.rect(34,y+34+n*16,n==2 and 24 or 40,3,true,true) end end
   F.text('Note '..i,104,y+(item.total>0 and 27 or item.preview~='' and 23 or 40),20)
   if item.total>0 then progress(F.box(104,y+72,w-120,14),item.done,item.total)
   elseif item.preview~='' then
    local text=item.preview;while #text>0 and F.width(text..'...',10)>w-120 do text=N.slice(text,1,math.max(0,#text-1)) end
    F.text(text..(text~=item.preview and '...' or ''),104,y+72,10)
   end
   if i==choice then F.corners(F.box(12,y,w-24,104),10,2) end
   hit(F.box(16,y,w-32,104),'library',i)
  end
  F.center(((start-1)//capacity+1)..' / '..((7)//capacity+1),F.box(16,h-101,w-32,21),10)
  button('HOW TO IMPORT',footer,'help')
 elseif screen=='note' then
  local total,done=0,0;for _,v in ipairs(lines) do local row=N.row(v);if row.mark then total=total+1;if row.checked then done=done+1 end end end
  local top=119
  if total>0 then
   local label=done==total and 'ALL DONE' or (total-done)..' LEFT OF '..total;local width=F.width(label,10)+24
   F.text(label,16,112,10);progress(F.box(16+width,111,w-32-width,14),done,total);smudge.rect(16,139,w-32,1,true,true);top=140
  end
  plan(top,h-92)
  for _,block in ipairs(pages[viewPage]) do
   local row=block.row;local x=row.mark and 68 or 16;local textY=block.y+(block.h-#block.lines*42)//2
   if row.mark then
    F.round(F.box(16,textY+1,40,40),4,false,false,3)
    if row.checked then F.round(F.box(25,textY+10,22,22),2,true) end
   end
   for n,line in ipairs(block.lines) do
    F.text(line,x,textY+9+(n-1)*42,20)
    if row.checked and line~='' then smudge.rect(x,textY+(n-1)*42+21,math.min(w-16-x,F.width(line,20)),2,true,true) end
   end
   if selected==block.index then smudge.rect(w-8,block.y,3,math.min(block.h,h-118-block.y),true,true) end
   hit(F.box(16,block.y,w-32,block.h),'line',block.index)
  end
  if #pages>1 then F.center(viewPage..' / '..#pages,F.box(16,h-113,w-32,21),10)end
  if done>0 then
   button('OPTIONS',F.box(16,footer.y,(w-44)//2,52),'menu')
   button('CLEAR DONE',F.box(28+(w-44)//2,footer.y,(w-44)//2,52),'clear',nil,true)
  else button('OPTIONS',footer,'menu') end
 elseif screen=='menu' then
  local pitch=math.min(76,(h-119-100)//#options)
  for i,label in ipairs(options) do local b=F.box(16,119+(i-1)*pitch,w-32,pitch)
   F.text(label:upper(),16,b.y+(b.h-25)//2,F.fit(label,w-32,20));if i<#options then smudge.rect(16,b.y+b.h-1,w-32,1,true,true) end
   if choice==i then F.corners(b,10,2) end;hit(b,'option',i)
  end
  button('BACK',footer,'resume')
 elseif screen=='confirm' then
  F.sheet(pending=='clear' and 'Remove all checked lines? The original import file stays unchanged.' or 'Replace this card with its note file? Your saved edits will be replaced.',F.box(16,119,w-32,h-211))
  button('KEEP IT',F.box(16,footer.y,(w-44)//2,52),'confirm',1)
  button(pending=='clear' and 'CLEAR DONE' or 'IMPORT',F.box(28+(w-44)//2,footer.y,(w-44)//2,52),'confirm',2,true)
 elseif screen=='help' then
  local prose='Copy note1.md through note8.md into this app folder. Each card accepts 3072 bytes and 97 lines. Use - [ ] task or - [x] task for checkboxes. Touch a row to toggle it. Up and Down choose a line; Left and Right scroll long text. Swipe to turn pages. OPTIONS imports a file, converts a card, or clears checked lines with confirmation. Imports stay unchanged; journals hold your edits. Phone editing and sleep notes require host support.'
  local text=wrap(prose,w-32,10);for i,line in ipairs(text) do if 119+(i-1)*26<h-110 then F.text(line,16,119+(i-1)*26,10) end end
  button('BACK',footer,'back')
 end
 if notice~='' then F.center(notice,F.box(16,h-110,w-32,25),10) end
end
function on_button(btn,pressed)
 if not pressed or closed then return end
 if editor then
  if btn=='back' then editor.cancel();editor=nil;screen='note';redraw()else editor.button(btn)end
  return
 end
 if screen=='note' and (btn=='page_back' or btn=='page_forward') then viewPage=math.max(1,math.min(#pages,viewPage+(btn=='page_forward' and 1 or -1)));redraw();return end
 if btn=='page_back' then btn='up' elseif btn=='page_forward' then btn='down' end
 if btn=='back' then
  if screen=='library' then closed=true;smudge.exit()
  elseif screen=='note' then screen='menu';choice=1
  elseif screen=='menu' then if save() then loaded=false;screen='library';choice=index end
  elseif screen=='help' and not loaded then screen='library'
  else screen='menu';choice=1 end
 elseif screen=='library' then
  if btn=='up' then choice=(choice-2)%8+1 elseif btn=='down' then choice=choice%8+1
  elseif btn=='confirm' then notice='';load(choice) end
 elseif screen=='note' then
  if btn=='up' then selected=(selected-2)%#lines+1;offset=0 elseif btn=='down' then selected=selected%#lines+1;offset=0
  elseif btn=='confirm' then
   local line=N.row(lines[selected])
   if line.mark then summaries[index]=nil;S.last=nil;collectgarbage('collect');doc=N.toggle(doc,selected,lines);line.checked=not line.checked;lines[selected]=N.pack(line) end
  elseif btn=='right' or btn=='left' then
   local l=N.row(lines[selected]);offset=math.max(0,math.min(math.max(0,l.last-l.text), offset+(btn=='right' and 20 or -20)))
  end
 elseif screen=='menu' then
  if btn=='up' then choice=(choice-2)%#options+1 elseif btn=='down' then choice=choice%#options+1
  elseif btn=='confirm' then operate() end
 elseif screen=='confirm' then
  if btn=='up' or btn=='down' then choice=3-choice
  elseif btn=='confirm' then
   if choice==2 then
    if pending=='clear' then change(N.clear(doc,lines)) else
     local raw=smudge.read_file('note'..index..'.md',3073)
     if N.validate(raw) then change(raw);notice='' else notice='Import failed: missing, invalid, or too large.' end
    end
   end
   screen='note'
  end
 end
 redraw()
end
function on_touch(x,y,event)
 if event~='tap' or closed or x<0 or y<0 or x>=w or y>=h then return end
 if editor then editor.touch(x,y);return end
 local item=F.at(hits,x,y);if not item then return end
 local action=item.action;local value=item.value
 if action=='library' then choice=value;on_button('confirm',true)
 elseif action=='line' then selected=value;offset=0;on_button('confirm',true)
 elseif action=='option' then choice=value;operate()
 elseif action=='confirm' then choice=value;on_button('confirm',true)
 elseif action=='menu' then on_button('back',true)
 elseif action=='clear' then choice=2;operate()
 elseif action=='resume' then screen=loaded and 'note' or 'library';redraw()
 elseif action=='help' then screen='help';redraw()
 elseif action=='back' then on_button('back',true) end
end
function on_swipe(dir)
 if closed then return end
 if editor then return end
 if screen=='note' then if dir=='up' or dir=='left' then on_button('page_forward',true) elseif dir=='down' or dir=='right' then on_button('page_back',true) end
 elseif screen=='library' then
  local capacity=math.max(1,(h-68-12-119+12)//116)
  choice=math.max(1,math.min(8,choice+((dir=='up' or dir=='left') and capacity or -capacity)));redraw()
 else on_button(dir,true) end
end
function on_update()if editor then editor.update()end end
function on_exit()if editor then editor.cancel();editor=nil end;if loaded then save() end;closed=true;if original then pcall(smudge.set_orientation,original) end end
return {state=function()return doc,lines,screen,index end,hits=function()return hits end,pages=function()return pages,viewPage end}
