-- Never reads an existing secret. New input is masked and stays only in this draft.
return function(close)
 local K={hits={},focus=1,status=''}
 local entry,pending,confirm
 local function redraw()smudge.request_update()end
 local function reset()if entry then entry.cancel()end;entry=nil;pending=nil;confirm=nil;K.hits={};K.focus=1;redraw()end
 local function activate(action)
  if action=='back' then reset();close()
  elseif action=='edit' then
   pending=nil;confirm=nil
   entry=dofile('dictation_editor.lua')('',function(value)
    pending=value;confirm='save';entry=nil;K.hits={};K.focus=1;redraw();return true
   end,function(v)return #v<=512 and not v:find('[^A-Za-z0-9_-]')end,{secret=true,close=reset,clear=function()reset();confirm='clear' end})
   entry.status='New key only; existing key is never shown'
  elseif action=='clear' then reset();confirm='clear'
  elseif action=='cancel' then reset()
  elseif action=='confirm' then
   local ok=confirm=='save' and smudge.notes_key_save(pending or '') or (confirm=='clear' and smudge.notes_key_clear())
   if ok then local status=confirm=='save' and 'Key saved' or 'Key cleared';reset();K.status=status
   else K.status='Storage failed. Retry or Cancel.' end
  end
  redraw()
 end
 function K.draw(_,F)
  if entry then entry.draw('OpenAI key',F);return end
  local w,h=smudge.get_bounds();smudge.clear();K.hits={}
  F.header('OPENAI KEY',w)
  local configured=smudge.notes_key_status and smudge.notes_key_status()
  F.text(configured and 'Key configured' or 'No key configured',16,100,20)
  local copy=confirm and (confirm=='save' and 'Save the new key? It replaces the existing key.' or 'Clear the saved OpenAI key? Manual editing still works.') or 'Stored as plaintext on this device SD card. Never displayed or read back to Lua. Dictation sends audio to OpenAI only after Start. Manual typing needs neither a key nor Wi-Fi.'
  for i,line in ipairs(F.wrap(copy,w-32,10))do F.text(line,16,145+(i-1)*26,10)end
  local labels=confirm and {'Cancel',confirm=='save' and 'SAVE KEY' or 'CLEAR KEY'} or {'Enter new key','Clear key','Back'}
  local actions=confirm and {'cancel','confirm'} or {'edit','clear','back'}
  for i,label in ipairs(labels)do
   local width=(w-32-8*(#labels-1))//#labels
   local b=F.box(16+(i-1)*(width+8),h-92,width,52)
   F.rect(b,false,false,1);F.center(label,b,10)
   if K.focus==i then F.corners(b,6,2)end
   K.hits[i]={x=b.x,y=b.y,w=b.w,h=b.h,action=actions[i]}
  end
  F.text(K.status,16,h-127,10)
 end
 function K.touch(x,y)
  if entry then entry.touch(x,y);return end
  for _,b in ipairs(K.hits)do if x>=b.x and y>=b.y and x<b.x+b.w and y<b.y+b.h then activate(b.action);return end end
 end
 function K.button(key)
  if key=='back' then if entry or confirm then reset()else activate('back')end
  elseif entry then entry.button(key)
  elseif key=='confirm' then local b=K.hits[K.focus];if b then activate(b.action)end
  elseif key=='up' or key=='left' or key=='page_back' then K.focus=(K.focus-2)%math.max(1,#K.hits)+1
  elseif key=='down' or key=='right' or key=='page_forward' then K.focus=K.focus%math.max(1,#K.hits)+1 end
  redraw()
 end
 function K.cancel()reset()end
 function K.update()end
 return K
end
