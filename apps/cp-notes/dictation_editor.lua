-- Lua owns the draft; native keyboard owns the painted text/key geometry.
return function(value,save,validate,settings)
 local E={value=value,cursor=#value+1,focus=1,shift=false,symbols=false,hits={},status='Ready',busy=false,active=false}
 settings=settings or {}
 local received,anchor,liveEnd,manual,detached='',E.cursor,E.cursor,false,false
 local function redraw()smudge.request_update()end
 local function boundary(p)
  p=math.max(1,math.min(#E.value+1,p))
  while p>1 and p<=#E.value and E.value:byte(p)>=128 and E.value:byte(p)<192 do p=p-1 end
  return p
 end
 local function previous(p)return boundary(math.max(1,p-1))end
 local function following(p)
  p=math.min(#E.value+1,p+1)
  while p<=#E.value and E.value:byte(p)>=128 and E.value:byte(p)<192 do p=p+1 end
  return p
 end
 local function range()return math.min(E.cursor,E.selection or E.cursor),math.max(E.cursor,E.selection or E.cursor)end
 local function replace(first,last,text,user)
  local next=E.value:sub(1,first-1)..text..E.value:sub(last)
  if not validate(next) then
   if settings.secret then E.status='Use key characters only; max 512 bytes'
   else E.cancel();E.status='Text full; draft kept' end
   return false
  end
  local function shift(p)
   if p<first then return p end
   if p<=last then return first+#text end
   return p+#text-(last-first)
  end
  if user then
   manual=true
   if last>first and first<=anchor and last>=liveEnd and liveEnd>anchor then detached=true end
  end
  anchor=shift(anchor);liveEnd=shift(liveEnd)
  E.cursor=shift(E.cursor);if E.selection then E.selection=shift(E.selection)end
  E.value=next
  return true
 end
 function E.insert(text)
  local first,last=range()
  if replace(first,last,text,true) then E.cursor=first+#text;E.selection=nil end
  redraw()
 end
 function E.backspace()
  local first,last=range()
  if first==last then first=previous(first)end
  if first<last and replace(first,last,'',true) then E.cursor=first;E.selection=nil end
  redraw()
 end
 function E.toggleSelection()E.selection=not E.selection and E.cursor or nil;redraw()end
 function E.selectAll()E.selection=1;E.cursor=#E.value+1;redraw()end
 function E.move(direction)
  if (direction=='up' or direction=='down') and smudge.keyboard_snapshot then
   local p=smudge.keyboard_snapshot('',E.value,E.cursor,E.shift,E.symbols,1,nil,nil,E.language or -1,E.selection,settings.secret or false,144,direction)
   if math.type(p)=='integer' and p>=1 and p<=#E.value+1 then E.cursor=boundary(p);redraw();return end
  end
  local p=boundary(E.cursor)
  local start=(E.value:sub(1,p-1):match('.*\n()') or 1)
  local finish=E.value:find('\n',p,true) or #E.value+1
  if direction=='left' then p=previous(p)
  elseif direction=='right' then p=following(p)
  elseif direction=='home' then p=start
  elseif direction=='end' then p=finish
  elseif direction=='up' or direction=='down' then
   local column,q=0,start
   while q<p do column=column+1;q=following(q)end
   local target=direction=='up' and (start>1 and (E.value:sub(1,start-2):match('.*\n()') or 1) or 1) or (finish<=#E.value and finish+1 or start)
   p=target
   for _=1,column do if p>#E.value or E.value:sub(p,p)=='\n' then break end;p=following(p)end
  end
  E.cursor=p;redraw()
 end
 function E.start()
  if E.busy or settings.secret then return end
  if not smudge.notes_dictation_start then E.status='Dictation needs updated firmware';redraw();return end
  -- Dictation inserts at the caret; it never implicitly deletes a selection.
  E.selection=nil
  if smudge.notes_dictation_start() then
   E.busy=true;E.active=true;E.status='Connecting';received='';anchor=E.cursor;liveEnd=E.cursor;manual=false;detached=false
  else local _,status=smudge.notes_dictation_poll();E.status=status end
  redraw()
 end
 function E.stop()if smudge.notes_dictation_stop then smudge.notes_dictation_stop()end;E.status='Finishing';redraw()end
 function E.cancel()
  if not settings.secret and smudge.notes_dictation_cancel then smudge.notes_dictation_cancel()end
  E.busy=false;E.active=false
  if settings.secret then E.value='';E.cursor=1;E.selection=nil end
 end
 function E.update()
  if not E.active then return end
  local text,status,busy=smudge.notes_dictation_poll()
  if status=='Cancelled' then E.active=false;E.busy=false;E.status='Stopped; text kept';redraw();return end
  local changed=text~=received or status~=E.status or busy~=E.busy
  E.status=status;E.busy=busy
  if text~=received then
   local first,last,delta
   if not manual and E.value:sub(anchor,anchor+#received-1)==received then first,last,delta=anchor,anchor+#received,text
   elseif not detached and text:sub(1,#received)==received then first,last,delta=liveEnd,liveEnd,text:sub(#received+1)
   else E.status='Final differs; kept your edits' end
   if first then
    local originalAnchor=anchor
    if not replace(first,last,delta,false) then redraw();return end
    anchor=originalAnchor;liveEnd=first+#delta
   end
   received=text
  end
  if not busy then E.active=false end
  if changed then redraw()end
 end
 function E.save()
  if E.busy then E.stop();E.status='Finishing; Save when done';return false end
  if save(E.value) then E.cancel();E.saved=true;return true end
  E.status='Save failed; keep open and retry';redraw();return false
 end
 local function activate(key)
  local action=key.action
  if action=='start' then if E.busy then E.stop()else E.start()end
  elseif action=='save' or action=='done' then E.save()
  elseif action=='cancel' then if settings.close then E.cancel();settings.close()end
  elseif action=='clear' then if settings.clear then E.cancel();settings.clear()end
  elseif action=='newline' then E.insert('\n')
  elseif action=='insert' then E.insert(key.output or '')
  elseif action=='delete' then E.backspace()
  elseif action=='shift' then E.shift=not E.shift
  elseif action=='mode' then E.symbols=not E.symbols
  elseif action=='language' then E.language=E.nextLanguage
  elseif action=='select' then E.toggleSelection()
  elseif action=='all' then E.selectAll()
  elseif action=='cursor' then E.cursorMode=not E.cursorMode
  else E.move(action)end
  redraw()
 end
 function E.draw(title,F)
  local focus=not E.cursorMode and E.focus<=(E.keyCount or 56) and E.focus or 0
  E.hits=smudge.keyboard_snapshot(title,E.value,E.cursor,E.shift,E.symbols,focus,nil,nil,E.language or -1,E.selection,settings.secret or false,144)
  E.keyCount=#E.hits
  E.language=E.hits.layout_index;E.nextLanguage=E.hits.next_layout_index
  local w=smudge.get_bounds();smudge.rect(0,0,w,144,true,false)
  F.text(title,16,8,10)
  local labels=settings.secret and {'Cancel','Save','Clear'} or {'+ line','Save',E.busy and 'Stop' or 'Start'}
  local actions=settings.secret and {'cancel','save','clear'} or {'newline','save','start'}
  local function control(label,action,x,y,width)
   local b=F.box(x,y,width,44);F.rect(b,false,false,1);F.center(label,b,10)
   E.hits[#E.hits+1]={x=b.x,y=b.y,w=b.w,h=b.h,action=action}
   if E.focus==#E.hits then F.corners(b,5,2)end
  end
  for i,label in ipairs(labels)do control(label,actions[i],w-300+(i-1)*100,26,92)end
  local nav=E.navPage and {{'Up','up'},{'Down','down'},{'Home','home'},{'End','end'},{'All','all'},{'More','more'}} or {{'<','left'},{'>','right'},{E.selection and 'Selecting' or 'Select','select'},{'All','all'},{E.cursorMode and 'Keys' or 'Cursor','cursor'},{'More','more'}}
  for i,item in ipairs(nav)do control(item[1],item[2],8+(i-1)*(w-16)//6,76,(w-16)//6-4)end
  F.text(E.status,16,123,10)
 end
 function E.touch(x,y)
  for i,key in ipairs(E.hits)do if x>=key.x and y>=key.y and x<key.x+key.w and y<key.y+key.h then
   E.focus=i;if key.action=='more' then E.navPage=not E.navPage;redraw()else activate(key)end;return
  end end
  local cursor=smudge.keyboard_snapshot('',E.value,E.cursor,E.shift,E.symbols,1,x,y,E.language or -1,E.selection,settings.secret or false,144)
  if cursor==-1 then E.cancel();if settings.close then settings.close()else E.status='Stopped; text kept'end
  elseif math.type(cursor)=='integer' and cursor>=1 and cursor<=#E.value+1 then E.cursor=boundary(cursor)end
  redraw()
 end
 function E.button(key)
  if E.cursorMode and (key=='left' or key=='right' or key=='up' or key=='down') then E.move(key)
  elseif key=='confirm' then
   if E.cursorMode then E.cursorMode=false
   else local hit=E.hits[E.focus];if hit then if hit.action=='more' then E.navPage=not E.navPage else activate(hit)end end end
  elseif key=='up' or key=='left' or key=='page_back' then E.focus=(E.focus-2)%math.max(1,#E.hits)+1
  elseif key=='down' or key=='right' or key=='page_forward' then E.focus=E.focus%math.max(1,#E.hits)+1 end
  redraw()
 end
 return E
end
