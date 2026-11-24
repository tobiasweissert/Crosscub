-- Clock for Crosscub Sticky. Device-local civil time; no network or saved state.
local F=dofile('font.lua')
local shown
local weekdays={'Sun','Mon','Tue','Wed','Thu','Fri','Sat'}
local months={'January','February','March','April','May','June','July','August','September','October','November','December'}
local function minute_key(d)
    return string.format('%04d-%02d-%02d %02d:%02d',d.year,d.month,d.day,d.hour,d.min)
end
function on_init()
    shown=minute_key(smudge.get_date())
    smudge.prevent_sleep(true)
    smudge.full_refresh()
end
function on_update()
    local d=smudge.get_date()
    local key=minute_key(d)
    if key~=shown then
        shown=key
        if d.min%15==0 then smudge.full_refresh() else smudge.request_update() end
    end
end
function on_draw()
    local w,h=smudge.get_bounds()
    local small=w<h and w<480 or w>h
    local fw=small and 288 or 480
    local fh=small and 480 or 800
    local ox,oy=(w-fw)//2,(h-fh)//2
    local function box(x,y,bw,bh)
        return ox+math.floor(x*fw/480),oy+math.floor(y*fh/800),math.floor(bw*fw/480),math.floor(bh*fh/800)
    end
    local d=smudge.get_date()
    shown=minute_key(d)
    smudge.clear()
    F.draw(small and 'digits_small' or 'digits',string.format('%02d',d.hour),box(40,98,400,218))
    local x,y,rw,rh=box(70,357,340,4)
    smudge.rect(x,y,rw,rh,true,true)
    F.draw(small and 'digits_small' or 'digits',string.format('%02d',d.min),box(40,408,400,218))
    local date=weekdays[d.wday]..' '..d.day..' '..months[d.month]
    F.draw(small and 'date_small' or 'date',date,box(32,690,416,42))
end
function on_button(key,pressed)
    if pressed and (key=='back' or key=='btn1') then smudge.exit() end
end
function on_exit() smudge.prevent_sleep(false) end
