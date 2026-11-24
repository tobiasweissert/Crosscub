-- App-local licensed bitmap glyphs; no firmware font scaling assumptions.
local F={}
local metrics=smudge.dofile('font_metrics.lua')
local data={}
function F.width(text,cut)
 local f=assert(metrics[cut]);local width=0
 for i=1,#text do local g=f.glyphs[text:byte(i)] or f.glyphs[63];width=width+g[5] end
 return width
end
function F.draw(text,x,top,cut,align,muted)
 local f=assert(metrics[cut]);local bytes=data[cut]
 if not bytes then
  bytes=smudge.read_file(f.file,f.bytes+1)
  assert(type(bytes)=='string' and #bytes==f.bytes,'Missing or damaged Snake font: '..f.file)
  data[cut]=bytes
 end
 local width=F.width(text,cut)
 if align=='center' then x=x-width//2 elseif align=='right' then x=x-width end
 local ymin=0
 for i=1,#text do local g=f.glyphs[text:byte(i)] or f.glyphs[63];ymin=math.min(ymin,g[4]) end
 local baseline=top-ymin
 for i=1,#text do
  local g=f.glyphs[text:byte(i)] or f.glyphs[63]
  local glyph=bytes:sub(g[6],g[6]+g[7]-1)
  if muted then
   -- Native bitmap(dither=true) uses light gray. Dark secondary type needs
   -- three-quarters ink instead, preserving white padding and glyph holes.
   local stride=(g[1]+7)//8;local masked={}
   for row=0,g[2]-1 do
    local mask=(baseline+g[4]+row)%2==0 and ((math.floor(x+g[3])%2==0) and 170 or 85) or 0
    for col=0,stride-1 do masked[#masked+1]=string.char(glyph:byte(row*stride+col+1) | mask) end
   end
   glyph=table.concat(masked)
  end
  smudge.bitmap(math.floor(x+g[3]),baseline+g[4],g[1],g[2],glyph,false,false)
  x=x+g[5]
 end
end
function F.wrap(text,cut,width)
 local lines,line={},''
 for word in text:gmatch('%S+') do
  local candidate=line=='' and word or line..' '..word
  if line~='' and F.width(candidate,cut)>width then lines[#lines+1]=line;line=word else line=candidate end
 end
 if line~='' then lines[#lines+1]=line end
 return lines
end
return F
