-- Sticky-only native key family. Geometry, actions and feedback remain game-owned.
local K={}
function K.paint(label,b,selected,mark,enabled,kind)
 if not (smudge.get_device and smudge.get_device()=='sticky' and smudge.keyboard_key)then return false end
 smudge.keyboard_key(label,b.x,b.y,b.w,b.h,selected or mark==4,enabled~=false,kind or 'text')
 -- Feedback overlays are deliberately distinct from ordinary focus. The model's
 -- absent/present/correct ranks and their monotonic merge are not rewritten.
 if mark==2 then
  smudge.rect_dither(b.x+4,b.y+b.h-6,b.w-8,2,false)
 elseif mark==3 then
  smudge.rect(b.x,b.y,b.w,b.h,false,3,true)
  smudge.rect(b.x+b.w-8,b.y+4,4,4,true,true)
 end
 return true
end
return K
