-- Native keyboard paint/input geometry; this controller owns the existing card draft.
return function(value,save,validate)
 local E={value=value,cursor=#value+1,focus=1,shift=false,symbols=false,hits={},status='Ready',busy=false,active=false}
 local received,anchor,manual='',E.cursor,false
 local function redraw()smudge.request_update()end
 local function replace(first,last,text)
  local next=E.value:sub(1,first-1)..text..E.value:sub(last)
  if not validate(next) then E.cancel();E.status='Note full; text kept';return false end
  E.value=next;E.cursor=first+#text;return true
 end
 function E.insert(text)manual=true;replace(E.cursor,E.cursor,text);redraw()end
 function E.backspace()
  if E.cursor>1 then
   manual=true;local p=E.cursor-1;while p>1 and E.value:byte(p)>=128 and E.value:byte(p)<192 do p=p-1 end
   replace(p,E.cursor,'')
  end;redraw()
 end
 function E.start()
  if E.busy then return end
  if not smudge.notes_dictation_start then E.status='Dictation needs updated firmware';redraw();return end
  if smudge.notes_dictation_start() then
   E.busy=true;E.active=true;E.status='Connecting';received='';anchor=E.cursor;manual=false
  else local _,status=smudge.notes_dictation_poll();E.status=status end
  redraw()
 end
 function E.stop()if smudge.notes_dictation_stop then smudge.notes_dictation_stop()end;E.status='Finishing';redraw()end
 function E.cancel()
  if smudge.notes_dictation_cancel then smudge.notes_dictation_cancel()end
  E.busy=false;E.active=false
 end
 function E.update()
  if not E.active then return end
  local text,status,busy=smudge.notes_dictation_poll()
  if status=='Cancelled' then E.active=false;E.busy=false;E.status='Stopped; text kept';redraw();return end
  local changed=text~=received or status~=E.status or busy~=E.busy
  E.status=status;E.busy=busy
  if text~=received then
   if not manual and E.value:sub(anchor,anchor+#received-1)==received then
    if not replace(anchor,anchor+#received,text) then redraw();return end
   elseif text:sub(1,#received)==received then
    if not replace(E.cursor,E.cursor,text:sub(#received+1)) then redraw();return end
   else E.status='Final differs; kept your edits' end
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
  elseif action=='newline' then E.insert('\n')
  elseif action=='insert' then E.insert(key.output or '')
  elseif action=='delete' then E.backspace()
  elseif action=='shift' then E.shift=not E.shift
  elseif action=='mode' then E.symbols=not E.symbols
  elseif action=='language' then E.language=E.nextLanguage end
  redraw()
 end
 function E.draw(title,F)
  E.hits=smudge.keyboard_snapshot(title,E.value,E.cursor,E.shift,E.symbols,E.focus,nil,nil,E.language or -1)
  E.language=E.hits.layout_index;E.nextLanguage=E.hits.next_layout_index
  local w=smudge.get_bounds();smudge.rect(0,0,w,80,true,false)
  F.text(title,16,16,10)
  local labels={'+ line','Save',E.busy and 'Stop' or 'Start'}
  local actions={'newline','save','start'}
  for i,label in ipairs(labels)do
   local b=F.box(w-300+(i-1)*100,6,92,44);F.rect(b,false,false,1);F.center(label,b,10)
   E.hits[#E.hits+1]={x=b.x,y=b.y,w=b.w,h=b.h,action=actions[i]}
  end
  F.text(E.status,16,57,10)
 end
 function E.touch(x,y)
  for _,key in ipairs(E.hits)do if x>=key.x and y>=key.y and x<key.x+key.w and y<key.y+key.h then activate(key);return end end
  local cursor=smudge.keyboard_snapshot('',E.value,E.cursor,E.shift,E.symbols,E.focus,x,y,E.language or -1)
  if cursor==-1 then E.cancel();E.status='Stopped; text kept'
  elseif math.type(cursor)=='integer' and cursor>=1 and cursor<=#E.value+1 then E.cursor=cursor end
  redraw()
 end
 function E.button(key)
  if key=='confirm' then local hit=E.hits[E.focus];if hit then activate(hit)end
  elseif key=='up' or key=='left' or key=='page_back' then E.focus=(E.focus-2)%math.max(1,#E.hits)+1
  elseif key=='down' or key=='right' or key=='page_forward' then E.focus=E.focus%math.max(1,#E.hits)+1 end;redraw()
 end
 return E
end
