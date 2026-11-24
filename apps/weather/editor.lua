-- Loaded only while entering text; released on Done/Cancel.
return function(U)
 local e=U.editor
 if e.nativeId then
  local state,value=smudge.text_entry_poll(e.nativeId)
  if state~='pending' then
   -- Clear before invoking done: Flashcards can immediately request its back.
   e.nativeId=nil;U.closeEditor()
   if state=='done' then e.done(value) end
   U.redraw();return
  end
  U.begin(e.title,'Opening native keyboard…');return
 end
 -- Compatibility only: firmware without the native API uses the old editor.
 U.begin(e.title,'Back cancels')
 local kw=(U.w-16)//10;local kh=math.min(60,(U.bottom-U.top-90)//5)
 local y=U.bottom-5*(kh+4)
 U.wrap(e.value..'|',U.pad,U.top,U.w-2*U.pad,math.max(1,(y-U.top-8)//smudge.line_height('ui12')))
 local function insert(c) if #e.value+#c<=e.limit then e.value=e.value..c end end
 local rows=e.numeric and {'1234567890','-/:.,?!@&','_+=()%#;'} or {'qwertyuiop','asdfghjkl','zxcvbnm'}
 for row,letters in ipairs(rows) do
  for i=1,#letters do
   local c=letters:sub(i,i);if e.shift then c=c:upper() end
   local x=(U.w-#letters*(kw+1))//2+(i-1)*(kw+1)
   U.button(c,x,y+(row-1)*(kh+4),kw,kh,insert,false,c)
  end
 end
 local labels={e.numeric and 'abc' or '123',e.shift and 'Lower' or 'Shift','Space','Delete'}
 for i,label in ipairs(labels) do
  U.button(label,8+(i-1)*(U.w-16)/4,y+3*(kh+4),(U.w-24)/4,kh,function()
   if i==1 then e.numeric=not e.numeric
   elseif i==2 then e.shift=not e.shift
   elseif i==3 then if #e.value<e.limit then e.value=e.value..' ' end
   end
  end)
 end
 -- Replace Delete action: remove exactly one UTF-8 codepoint, including ASCII.
 U.buttons[#U.buttons][5]=function()
  local p=#e.value;while p>1 and e.value:byte(p)>=128 and e.value:byte(p)<192 do p=p-1 end
  e.value=e.value:sub(1,p-1)
 end
 U.button('Done',8,y+4*(kh+4),U.w-16,kh,function() U.closeEditor();e.done(e.value);U.redraw() end,true)
 smudge.button_hints('Cancel','Key','Prev','Next')
end
