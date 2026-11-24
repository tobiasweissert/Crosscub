-- Stacked Sans bitmap reader. Bounded app-local assets; no font/firmware changes.
local F={}
local metrics=dofile('font_metrics.lua')
local cache={}
function F.measure(kind,text)
    local cut=assert(metrics[kind],'unknown font cut')
    local pen,left,right=0,0,0
    for i=1,#text do
        local g=assert(cut.glyphs[text:sub(i,i)],'unsupported clock glyph')
        if g[1]>0 then
            left=math.min(left,pen+g[4]);right=math.max(right,pen+g[4]+g[1])
        end
        pen=pen+g[3]
    end
    return right-left,cut.height,left
end
function F.draw(kind,text,x,y,w,h)
    local width,height,left=F.measure(kind,text)
    assert(width<=w and height<=h,'clock glyph run exceeds layout')
    local cut=metrics[kind]
    local family=kind:find('digits',1,true) and 'digits' or 'date'
    if not cache[family] or cache[family].kind~=kind then
        cache[family]=nil
        local data=assert(smudge.read_file(cut.file,cut.bytes+1),'missing clock font asset')
        assert(#data==cut.bytes,'invalid clock font asset length')
        cache[family]={kind=kind,data=data}
    end
    local data=cache[family].data
    local pen=math.floor(x+(w-width)/2)-left
    local top=math.floor(y+(h-height)/2)
    for i=1,#text do
        local g=cut.glyphs[text:sub(i,i)]
        if g[1]>0 and g[2]>0 then
            smudge.bitmap(pen+g[4],top+g[5],g[1],g[2],data:sub(g[6],g[6]+g[7]-1),false)
        end
        pen=pen+g[3]
    end
    return width,height
end
return F
