-- Crosscub Numeric Sans v1. Frozen app-local API; glyph assets SIL OFL 1.1.
-- Only selected cut resident. No utf8, native extensions or scalable text dependency.
local N={}
local metrics=dofile('numeric_sans_metrics.lua')
local heights={180,150,128,96,64,48,32,24}
local cachedHeight,cachedData
local function width(text,cut)
 local pen,left,right=0,0,0
 for i=1,#text do
  local g=assert(cut.glyphs[text:sub(i,i)],'unsupported numeric glyph')
  left=math.min(left,pen+g[4]);right=math.max(right,pen+g[4]+g[1]);pen=pen+g[3]
 end
 return right-left,left
end
-- fit ASCII numeric run to a bounded ink box. No truncation/substitution.
function N.measure(text,maxWidth,maxHeight)
 assert(type(text)=='string' and #text>0 and #text<=16,'numeric run length')
 assert(type(maxWidth)=='number' and type(maxHeight)=='number' and maxWidth>=1 and maxHeight>=1,'numeric box')
 for _,height in ipairs(heights)do
  if height<=maxHeight then
   local w,left=width(text,metrics[height])
   if w<=maxWidth then return w,height,left end
  end
 end
 error('numeric run cannot fit')
end
function N.draw(text,x,y,maxWidth,maxHeight,white)
 local w,height,left=N.measure(text,maxWidth,maxHeight)
 local cut=metrics[height]
 if height~=cachedHeight then
  cachedData=nil;cachedHeight=nil
  local data=assert(smudge.read_file('numeric_sans_'..height..'.bin',cut.bytes+1),'missing numeric font asset')
  assert(#data==cut.bytes,'invalid numeric font asset length')
  cachedData=data;cachedHeight=height
 end
 local pen=math.floor(x+(maxWidth-w)/2)-left
 local top=math.floor(y+(maxHeight-height)/2)
 for i=1,#text do
  local g=cut.glyphs[text:sub(i,i)]
  if g[1]>0 and g[2]>0 then smudge.bitmap(pen+g[4],top+g[5],g[1],g[2],cachedData:sub(g[6],g[6]+g[7]-1),white or false)end
  pen=pen+g[3]
 end
 return w,height
end
N.fit=N.measure -- same measured cut selection; no text truncation
return N
