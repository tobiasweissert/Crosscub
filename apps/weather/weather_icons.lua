-- Original monochrome vector primitives, copyright 2026 Crosscub contributors.
-- MIT (same license as application). No emoji, remote assets or raster UI.
local I={}
function I.draw(code,x,y,size)
 local function q(n)return math.floor(n*size/100+0.5)end
 local function circle(a,b,r,ink)smudge.circle(x+q(a),y+q(b),q(r),true,ink)end
 local function rect(a,b,w,h,ink)smudge.rect(x+q(a),y+q(b),q(w),q(h),true,ink)end
 local function stroke(a,b,c,d,width)
  local ax,ay,bx,by=x+q(a),y+q(b),x+q(c),y+q(d)
  local r=math.max(1,q(width or 4)//2)
  local steps=math.max(math.abs(bx-ax),math.abs(by-ay),1)
  for j=0,steps,2 do smudge.circle(math.floor(ax+(bx-ax)*j/steps),math.floor(ay+(by-ay)*j/steps),r,true,true)end
 end
 local function sun()
  circle(50,43,20,true);circle(50,43,15,false)
  for j=0,7 do local a=j*math.pi/4
   stroke(50+28*math.cos(a),43+28*math.sin(a),50+37*math.cos(a),43+37*math.sin(a),4)
  end
 end
 local function cloud()
  circle(27,49,18,true);circle(48,34,24,true);circle(72,49,19,true);rect(27,47,45,20,true)
  circle(27,49,13,false);circle(48,34,19,false);circle(72,49,14,false);rect(27,44,45,18,false)
 end
 if code==0 then sun();return end
 if code==1 or code==2 then sun()end
 cloud()
 if code>=45 and code<=48 then
  stroke(14,76,85,76);stroke(24,86,75,86)
 elseif (code>=71 and code<=77)or(code>=85 and code<=86)then
  for _,a in ipairs({27,50,73})do stroke(a,76,a,90,3);stroke(a-6,79,a+6,87,3);stroke(a-6,87,a+6,79,3)end
 elseif code>=95 then
  stroke(53,71,41,84,5);stroke(41,84,55,84,5);stroke(55,84,43,98,5)
 elseif code>=51 then
  for _,a in ipairs({30,51,72})do stroke(a,76,a-6,91,5)end
 end
end
return I
