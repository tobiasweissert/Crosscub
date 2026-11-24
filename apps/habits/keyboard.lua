-- Keyboard-only paint. Keep generic buttons and all input owners unchanged.
-- Package-local canonical copy; compatible with firmware predating keyboard_key.
local K={}
function K.button(U,label,x,y,w,h,action,filled,value)
 if not (smudge.get_device and smudge.get_device()=='sticky') then
  return U.button(label,x,y,w,h,action,filled,value)
 end
 x,y,w,h=math.floor(x),math.floor(y),math.floor(w),math.floor(h)
 local selected=U.focus==#U.buttons+1
 if smudge.keyboard_key then
  smudge.keyboard_key(label,x,y,w,h,selected,true)
 else
  -- 2.0.0 fallback: native UI12 regular, thin outline, selected inversion.
  -- No additional inset focus ring, and Done is not permanently selected.
  smudge.rect(x,y,w,h,true,selected)
  smudge.rect(x,y,w,h,false,1,true)
  smudge.text(x+w//2,y+(h-smudge.line_height('ui12'))//2,label,'ui12','regular','center',not selected)
 end
 U.buttons[#U.buttons+1]={x,y,w,h,action,label,value}
end
function K.editor(U,cursorOwned)
 if not (smudge.get_device and smudge.get_device()=='sticky' and smudge.keyboard_snapshot) then return false end
 local e=U.editor
 if not U.keyboardTap then
  U.keyboardTap=U.tap
  U.tap=function(x,y)
   local current=U.editor
   if current and current.nativeTouch then
    local n=smudge.keyboard_snapshot(current.title,current.value,current.cursor,current.shift,current.numeric,1,math.floor(x),math.floor(y),current.language)
    if n==-1 then U.closeEditor();U.redraw();return true
    elseif n>0 then current.cursor=n;U.redraw();return true end
   end
   return U.keyboardTap(x,y)
  end
 end
 e.nativeTouch=true
 local w,h=smudge.get_bounds();U.w,U.h=w,h;U.buttons={}
 if not cursorOwned then e.cursor=#e.value+1 end
 e.cursor=math.min(e.cursor or #e.value+1,#e.value+1)
 local keys=smudge.keyboard_snapshot(e.title,e.value,e.cursor,e.shift,e.numeric,math.min(U.focus,56),nil,nil,e.language)
 e.language=keys.layout_index
 for _,k in ipairs(keys)do
  local action=k.action
  local label=action=='shift' and 'Shift' or action=='done' and 'Done' or action=='delete' and 'Delete' or action=='mode' and (e.numeric and 'abc' or '123') or k.output==' ' and 'Space' or k.label
  local function activate()
   if action=='done' then U.closeEditor();e.done(e.value);U.redraw();return
   elseif action=='shift' then e.shift=not e.shift
   elseif action=='mode' then e.numeric=not e.numeric;e.shift=false
   elseif action=='language' then e.language=keys.next_layout_index;e.shift=false
   elseif action=='delete' then
    if e.cursor>1 then local p=e.cursor-1;while p>1 and e.value:byte(p)>=128 and e.value:byte(p)<192 do p=p-1 end;e.value=e.value:sub(1,p-1)..e.value:sub(e.cursor);e.cursor=p end
   elseif #e.value+#k.output<=e.limit then
    e.value=e.value:sub(1,e.cursor-1)..k.output..e.value:sub(e.cursor);e.cursor=e.cursor+#k.output
    if not e.numeric then e.shift=false end
   end
  end
  U.buttons[#U.buttons+1]={k.x,k.y,k.w,k.h,activate,label}
 end
 return true
end
return K
