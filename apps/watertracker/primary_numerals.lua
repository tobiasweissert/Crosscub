-- Presentation-only adapter. The frozen licensed glyph component is untouched.
local N = dofile('numeric_sans.lua')
local P = {}
function P.draw(value,x,y,w,h,white)
    local text = tostring(value)
    local ok = pcall(N.measure,text,w,h)
    if ok then
        local drawn = pcall(N.draw,text,x,y,w,h,white == true)
        if drawn then return end
    end
    -- Unsupported/overlong saved display values remain truthful, never truncated
    -- or written back. Native font fallback also keeps a damaged asset readable.
    local font = 'lexend16'
    if smudge.text_width(text,font,true)>w then font='ui12' end
    if smudge.text_width(text,font,true)>w then font='small' end
    smudge.text(x+w//2,y+(h-smudge.line_height(font))//2,text,font,true,'center',not white)
end
return P
