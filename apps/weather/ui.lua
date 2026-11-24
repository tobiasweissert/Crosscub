-- App-local UI: bounded text, shared touch/button hit targets, no framebuffer.
local U={buttons={},focus=1,notice=''}
local F=dofile('unified_ui.lua')
U.style=F
function U.redraw() U.buttons={};smudge.request_update() end
function U.begin(title,subtitle)
 if not U.editor then U.editorDraw=nil end
 local m=F.metrics();U.w,U.h=m.w,m.h;U.m=smudge.get_metrics()
 U.pad,U.top,U.bottom=m.pad,m.body_top,m.body_bottom
 U.buttons={};smudge.clear();F.header(title,subtitle or '')
 if U.notice~='' then
  local r=smudge.wrapped_text(U.notice,U.w-2*U.pad,'ui10','regular',4)
  for i,line in ipairs(r.lines)do F.text(U.pad,U.top+(i-1)*smudge.line_height('ui10'),line,'ui10')end
  U.top=U.top+#r.lines*smudge.line_height('ui10')+8
 end
end
function U.text(x,y,s,font,bold,align,color)
 smudge.text(math.floor(x),math.floor(y),tostring(s),font or 'ui10',bold and 'bold' or 'regular',align or 'left',color~=false)
end
function U.fit(s,width,font)
 font=font or 'ui10'
 while #s>0 and smudge.text_width(s,font)>width do
  local p=#s;while p>1 and s:byte(p)>=128 and s:byte(p)<192 do p=p-1 end
  s=s:sub(1,p-1)
 end
 return s
end
function U.wrap(s,x,y,width,lines,font,bold)
 font=font or 'ui10'
 local result=smudge.wrapped_text(s,width,font,bold and 'bold' or 'regular',lines)
 for i,line in ipairs(result.lines) do U.text(x,y+(i-1)*smudge.line_height(font),line,font,bold) end
 return y+#result.lines*smudge.line_height(font)
end
-- Inset square ring: focus is independent of the semantic primary/selected fill.
local function focusRing(r,filled)
 if U.focus==#U.buttons then
  smudge.rect(r.x+3,r.y+3,r.w-6,r.h-6,false,2,not filled)
 end
end
function U.button(label,x,y,w,h,action,filled,value)
 x,y,w,h=math.floor(x),math.floor(y),math.floor(w),math.floor(h)
 local r=F.button(label,{x=x,y=y,w=w,h=h},filled or false)
 U.buttons[#U.buttons+1]={r.x,r.y,r.w,r.h,action,label,value}
 focusRing(r,filled)
end
function U.actions(actions,primary)
 local labels={};for i,a in ipairs(actions)do labels[i]=a[1]end
 local rects=F.footer(labels,primary)
 for i,r in ipairs(rects)do local a=actions[i];U.buttons[#U.buttons+1]={r.x,r.y,r.w,r.h,a[2],a[1]};focusRing(r,i==primary)end
end
function U.footer() end
function U.activate(button)
 local action,value=button[5],button[7];U.buttons={}
 if button.immediate then action(value)
 else (U.action or function(f,v)f(v)end)(action,value)end
end
function U.tap(x,y)
 for i=#U.buttons,1,-1 do local b=U.buttons[i]
  if F.hit({x=b[1],y=b[2],w=b[3],h=b[4]},x,y)then U.focus=i;U.activate(b);U.redraw();return true end
 end
 return false
end
function U.key(key)
 local n=#U.buttons;if n==0 then return end
 U.focus=math.max(1,math.min(n,U.focus))
 if key=='up' or key=='left' or key=='page_back' then U.focus=(U.focus-2)%n+1
 elseif key=='down' or key=='right' or key=='page_forward' then U.focus=U.focus%n+1
 elseif key=='confirm' then U.activate(U.buttons[U.focus]) end
 U.redraw()
end
-- UTF-8 byte caps keep saves and keyboard editing bounded on C3.
function U.edit(title,value,limit,done,mode)
 if U.editor then U.closeEditor() end
 U.buttons={};U.digits=nil
 mode=mode or 'text'
 -- Native API is additive: old installed firmware keeps its legacy editor.
 if type(smudge.text_entry_request)=='function' and type(smudge.text_entry_poll)=='function' and type(smudge.text_entry_cancel)=='function' then
  local id=smudge.text_entry_request(title,value or '',limit,mode)
  if not id then U.notice='Native keyboard unavailable. Close and retry.';U.redraw();return false end
  U.editor={title=title,value=value or '',limit=limit,done=done,nativeId=id}
  U.editorDraw=U.editorDraw or dofile('editor.lua');U.focus=1;U.redraw();return true
 end
 collectgarbage('collect')
 U.editorDraw=U.editorDraw or dofile('editor.lua')
 U.editor={title=title,value=value or '',limit=limit,done=done,shift=false,numeric=false};U.focus=1;U.redraw()
end
function U.closeEditor()
 if U.editor and U.editor.nativeId then
  smudge.text_entry_cancel(U.editor.nativeId)
  smudge.text_entry_poll(U.editor.nativeId) -- consume cancellation before another edit
 end
 U.buttons={};U.editor=nil;U.focus=1
end
function U.editDraw() U.editorDraw(U) end
function U.save(S,payload)
 if not S.save(payload) then U.notice='Save failed. Keep open and retry.';return false end
 U.notice='';return true
end
function U.encode(s) return (s:gsub('[^%w%-_%.~]',function(c)return string.format('%%%02X',c:byte())end)) end
function U.number(y,text,height,x)
 U.digits=U.digits or dofile('digits.lua');U.digits(U,y,text,height,x)
end
return U
