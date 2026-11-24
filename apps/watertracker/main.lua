local U = dofile('unified_ui.lua')
local P -- Legacy presentation is loaded only on non-Sticky devices.
local footer, cup_rect = {}, nil
local N -- Load large font metrics only after app initialization.
local quick_actions = {}
local additions = {}
local function clear_undo() additions = {} end
local w, h = 480, 800
if smudge.get_bounds then
    w, h = smudge.get_bounds()
end

local in_settings, show_history = false, false
local settings_sel = 1
local history = {}
local history_page = 0
local function history_capacity() return math.max(1, (h - 64 - 206) // 72) end
local function history_move(delta)
    history_page = math.max(0, math.min(math.max(0, (#history - 1) // history_capacity()), history_page + delta))
end
local today_drank, daily_goal, step_amount = 0, 2000, 250
local units = "ml"
local last_date, last_date_short = "", ""
local streak = 0
local banner_msg, banner_expires = "", 0
local anim_tick, splash_anim = 0, 0

local function show_banner(msg, ms)
    banner_msg = msg
    local now = (smudge.millis and smudge.millis()) or (os.time() * 1000)
    banner_expires = now + (ms or 2200)
end

local UNITS = {"ml", "cups", "oz", "gallons", "L"}
local GOALS = {1000, 1500, 2000, 2500, 3000, 4000}
local STEPS = {100, 200, 250, 500}
local MONTHS = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}

local function fmt(ml, show_unit)
    local u = units
    if u == "cups" then
        local c = ml / 250.0
        local s = (c == math.floor(c)) and string.format("%d", c) or string.format("%.1f", c)
        if show_unit then
            return s .. (c == 1 and " cup" or " cups")
        end
        return s
    elseif u == "oz" then
        local oz = math.floor((ml / 29.57) + 0.5)
        return show_unit and (oz .. " oz") or tostring(oz)
    elseif u == "gallons" then
        local g = ml / 3785.4
        local s = string.format("%.2f", g)
        return show_unit and (s .. " gal") or s
    elseif u == "L" then
        local l = ml / 1000.0
        local s = (l == math.floor(l)) and string.format("%d", l) or string.format("%.1f", l)
        return show_unit and (s .. " L") or s
    else
        return show_unit and (ml .. " ml") or tostring(ml)
    end
end

local function get_date()
    local t = (smudge.get_date and smudge.get_date()) or (smudge.date and smudge.date()) or os.date("*t")
    return string.format("%04d-%02d-%02d", t.year, t.month, t.day),
           string.format("%s %d", MONTHS[t.month] or "Oct", t.day)
end

-- Exact civil-day arithmetic, not float32 epoch seconds (or 2038-limited time_t).
local function day_number(iso)
    local y, m, d = iso:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)$")
    y, m, d = tonumber(y), tonumber(m), tonumber(d)
    if not y or m < 1 or m > 12 or d < 1 or d > 31 then return nil end
    local prior = y - 1
    local n = 365 * prior + prior // 4 - prior // 100 + prior // 400 + d
    local months = {31,28,31,30,31,30,31,31,30,31,30,31}
    for i = 1, m - 1 do n = n + months[i] end
    if m > 2 and y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0) then n = n + 1 end
    return n
end
local function check_rollover()
    local iso, short = get_date()
    if last_date == "" then
        last_date, last_date_short = iso, short
        save_data()
        return
    end
    local today, previous = day_number(iso), day_number(last_date)
    -- A backward clock correction must not destroy or duplicate the saved day.
    if today and previous and today > previous then
        local met = (today_drank >= daily_goal)
        table.insert(history, 1, {
            date = (last_date_short ~= "" and last_date_short or last_date),
            drank = today_drank,
            goal = daily_goal,
            met = met
        })
        while #history > 14 do
            table.remove(history)
        end
        streak = met and today - previous == 1 and (streak + 1) or 0
        today_drank, last_date, last_date_short = 0, iso, short
        clear_undo()
        show_banner("New Day! Water Reset.", 2500)
        save_data()
        if smudge.request_update then smudge.request_update() end
    end
end

function save_data()
    smudge.write_file("water_state_v1.dat", string.format("%s;%d;%s;%d;%d;%d;%s",
        last_date, today_drank, units, daily_goal, step_amount, streak, last_date_short))
    local hp = {}
    for _, r in ipairs(history) do
        table.insert(hp, string.format("%s,%d,%d,%d", r.date, r.drank, r.goal, r.met and 1 or 0))
    end
    smudge.write_file("water_history_v1.dat", table.concat(hp, "|"))
    local undo = {}
    for _, amount in ipairs(additions) do undo[#undo+1] = tostring(amount) end
    smudge.write_file('water_undo_v1.dat',last_date..';'..today_drank..';'..table.concat(undo,','))
end

local function load_data()
    local s = smudge.read_file("water_state_v1.dat")
    if s and s ~= "" then
        local p = {}
        for x in string.gmatch(s, "[^;]+") do table.insert(p, x) end
        if #p >= 6 then
            last_date = p[1]
            today_drank = tonumber(p[2]) or 0
            units = p[3] or "ml"
            daily_goal = tonumber(p[4]) or 2000
            step_amount = tonumber(p[5]) or 250
            streak = tonumber(p[6]) or 0
            last_date_short = p[7] or ""
        end
    end
    local hraw = smudge.read_file("water_history_v1.dat")
    if hraw and hraw ~= "" then
        history = {}
        for entry in string.gmatch(hraw, "[^|]+") do
            local f = {}
            for v in string.gmatch(entry, "[^,]+") do table.insert(f, v) end
            if #f >= 4 then
                table.insert(history, {
                    date = f[1],
                    drank = tonumber(f[2]) or 0,
                    goal = tonumber(f[3]) or 2000,
                    met = (tonumber(f[4]) == 1)
                })
            end
        end
    end
    clear_undo()
    local raw = smudge.read_file('water_undo_v1.dat') or ''
    local date, total, stack = raw:match('^([^;]+);(%d+);(.*)$')
    if date == last_date and tonumber(total) == today_drank then
        local sum = 0
        for value in stack:gmatch('%d+') do
            local amount = tonumber(value)
            if amount > 0 then additions[#additions+1] = amount; sum = sum + amount end
        end
        if sum > today_drank then clear_undo() end
    end
    check_rollover()
end

local function add_water(delta)
    check_rollover()
    local old = today_drank
    today_drank = math.max(0, today_drank + delta)
    if delta > 0 then additions[#additions+1] = today_drank-old
    else clear_undo() end
    splash_anim = 8
    if delta > 0 then
        if old < daily_goal and today_drank >= daily_goal then
            show_banner("Goal Reached! Fantastic!", 3000)
        else
            show_banner("+" .. fmt(delta, true) .. " logged!", 2200)
        end
    else
        show_banner("-" .. fmt(math.abs(delta), true) .. " removed", 2000)
    end
    save_data()
end

local function undo_last()
    check_rollover()
    local amount = table.remove(additions)
    if not amount then return end
    today_drank = math.max(0,today_drank-amount)
    save_data()
end

function on_init()
    if smudge.get_bounds then
        w, h = smudge.get_bounds()
    end
    load_data()
end

function on_update()
    check_rollover()
    anim_tick = anim_tick + 1
    if splash_anim > 0 then
        splash_anim = splash_anim - 1
    end
    if banner_msg ~= "" and banner_expires > 0 then
        local now = (smudge.millis and smudge.millis()) or (os.time() * 1000)
        if now >= banner_expires then
            banner_msg = ""
            banner_expires = 0
            if smudge.request_update then
                smudge.request_update()
            end
        end
    end
end

local function settings_row(i)
    local rh = w>h and 54 or 78
    return 14,78+(i-1)*(rh+8),w-28,rh
end
local function contains(x,y,rx,ry,rw,rh)
    return x>=rx and x<rx+rw and y>=ry and y<ry+rh
end
local function footer_rect(i)
    local r=footer[i];if not r then return -1,-1,0,0 end
    return r.x,r.y,r.w,r.h
end
local function draw_footer()
    local labels=show_history and {'Back','Settings','Prev','Next'}
        or (in_settings and {'Back','Select','Up','Down'}
        or {'Exit','Settings','-'..fmt(step_amount,false),'+'..fmt(step_amount,false)})
    footer=U.footer(labels,not in_settings and not show_history and 4 or nil)
end
local function draw_today_reference()
    N = N or dofile('numeric_sans.lua')
    local land=w>h
    local x=214
    local right=land and 430 or w-24
    local cw=right-x
    U.text(24,14,'Water','ui12',true)
    U.text(w-20,18,'Goal '..string.format('%.1f',daily_goal/1000)..' L','ui10',false,'right')
    smudge.line(0,66,w-1,66,true)
    local gx,gy,gw,gh=74,90,114,h-108
    smudge.rounded_rect(gx,gy,gw,gh,15,false,4,true)
    local gap=8
    local block=(gh-20-7*gap)//8
    local pct=math.max(0,math.min(1,today_drank/math.max(1,daily_goal)))
    for i=1,8 do
        local by=gy+10+(8-i)*(block+gap)
        smudge.rounded_rect(gx+10,by,gw-20,block,8,false,1,true)
        local amount=math.max(0,math.min(1,pct*8-(i-1)))
        if amount>=1 then smudge.rounded_rect(gx+10,by,gw-20,block,8,true,true)
        elseif amount>0 then
            local fh=math.floor((block-4)*amount)
            smudge.rect(gx+12,by+block-2-fh,gw-24,fh,true,true)
        end
    end
    for i=0,4 do
        local value=daily_goal/1000*(4-i)/4
        local label=i==0 and string.format('%.1f L',value) or i==4 and '0' or string.format('%.1f',value)
        U.text(60,gy-14+(gh*i)//4,label,'ui10',false,'right')
    end
    local value=(units=='ml' or units=='L') and string.format('%.2f',today_drank/1000) or fmt(today_drank,false)
    local unit=(units=='ml' or units=='L') and 'L' or units=='gallons' and 'gal' or units
    local unitw=smudge.text_width(unit,'ui12',true)
    local nw=N.measure(value,cw-unitw-6,64)
    N.draw(value,x,100,nw,64)
    U.text(x+nw+4,126,unit,'ui12',true)
    U.text(x,174,tostring(math.floor(today_drank/math.max(1,daily_goal)*100+0.5))..'% of goal','ui12',true)
    U.text(x,206,today_drank>=daily_goal and 'Goal reached!' or fmt(daily_goal-today_drank,true)..' to go','ui10')
    local perblock=fmt(daily_goal%8==0 and daily_goal//8 or daily_goal/8,true)
    U.text(x,246,land and 'Each block is one' or 'Each block is one '..perblock,'ui10')
    U.text(x,274,land and perblock..' glass' or 'glass','ui10')
    local ax=land and 450 or x
    local aw=w-24-ax
    quick_actions={
        {x=ax,y=h-284,w=aw,h=104,amount=250,label='+ Glass'},
        {x=ax,y=h-168,w=(aw-12)//2,h=84,amount=150,label='Cup'},
        {x=ax+(aw+12)//2,y=h-168,w=(aw-12)//2,h=84,amount=500,label='Bottle'},
        {x=ax,y=h-66,w=aw,h=46,label='Undo last'}}
    for i,r in ipairs(quick_actions)do
        if i<4 then
            smudge.rounded_rect(r.x,r.y,r.w,r.h,i==1 and 20 or 16,i==1,2,true)
            U.text(r.x+r.w//2,r.y+(i==1 and 18 or 12),r.label,i==1 and 'ui12' or 'ui10',true,'center',i~=1)
            U.text(r.x+r.w//2,r.y+(i==1 and 56 or 44),fmt(r.amount,true),'ui10',false,'center',i~=1)
        else
            U.text(r.x+r.w//2,r.y+8,r.label,'ui10',false,'center')
            local tw=smudge.text_width(r.label,'ui10',false)
            smudge.line(r.x+(r.w-tw)//2,r.y+34,r.x+(r.w+tw)//2,r.y+34,true)
        end
    end
    -- Header remains a settings target; hardware Confirm keeps its existing role.
    footer={}
end
local function draw_today()
    if smudge.get_device and smudge.get_device()=='sticky' then draw_today_reference();return end
    P = P or dofile('primary_numerals.lua')
    local _,short=get_date();U.header('Water Tracker',short:upper())
    local pct=daily_goal>0 and today_drank/daily_goal or 0
    local summary=string.format('HYDRATION GOAL: %d%% (%s / %s)',math.floor(pct*100),fmt(today_drank,false),fmt(daily_goal,true))
    U.text(w//2,78,U.fit(summary,w-28,'ui10',true),'ui10',true,'center')
    local land=w>h
    cup_rect={x=land and 110 or w//2-74,y=land and 140 or 202,w=148,h=land and 170 or 198}
    U.panel(cup_rect)
    local fill=math.floor((cup_rect.h-8)*math.max(0,math.min(1,pct)))
    if fill>0 then smudge.rect(cup_rect.x+4,cup_rect.y+cup_rect.h-4-fill,cup_rect.w-8,fill,true,true) end
    local ax=land and 184 or w//2
    local value=fmt(today_drank,true):upper()
    P.draw(value,land and 14 or 14,land and 320 or 414,land and 340 or w-28,80)
    local x,y,cw=land and 382 or 14,land and 164 or 574,land and w-396 or w-28
    U.panel({x=x,y=y,w=cw,h=116})
    U.text(x+12,y+8,'DAILY GOAL','ui10')
    U.text(x+cw-12,y+8,'STREAK','ui10',false,'right')
    U.text(x+12,y+34,fmt(daily_goal,true):upper(),'ui12',true)
    U.text(x+cw-12,y+34,tostring(streak)..' DAYS','ui12',true,'right')
    U.text(x+12,y+66,"TODAY'S INTAKE",'ui10')
    U.text(x+cw-12,y+66,tostring(math.floor(pct*100))..'%','ui10',false,'right')
    U.panel({x=x+12,y=y+92,w=cw-24,h=14})
    local bw=math.floor((cw-32)*math.max(0,math.min(1,pct)))
    if bw>0 then smudge.rect(x+16,y+96,bw,6,true,true) end
    local tip=banner_msg~='' and banner_msg or (pct>=1 and 'Goal achieved! Excellent hydration today!' or (pct>=0.5 and 'Keep sipping regularly.' or 'Start your day with a fresh glass of water!'))
    U.text(land and x+cw//2 or w//2,land and 302 or 704,U.fit(tip,land and cw or w-28,'ui10'),'ui10',false,'center')
end
local function draw_history()
    U.header('Water History','PAST DAYS')
    U.text(14,78,'Streak: '..streak..' days','ui12',true)
    if #history==0 then U.text(w//2,180,'No past days recorded yet.','ui10',false,'center') end
    for i=1,math.min(history_capacity(),#history-history_page*history_capacity()) do
        local r=history[history_page*history_capacity()+i];local y=142+(i-1)*72
        U.panel({x=14,y=y,w=w-28,h=64})
        U.text(26,y+6,r.date,'ui10',true)
        U.text(26,y+32,fmt(r.drank,true)..' / '..fmt(r.goal,true),'ui10')
        U.text(w-26,y+20,r.met and 'MET' or tostring(math.floor(r.drank/math.max(1,r.goal)*100))..'%','ui10',true,'right')
    end
end
local function draw_settings()
    U.header('Water Settings','')
    local items={{'Measurement Unit',units:upper()},{'Daily Goal',fmt(daily_goal,true)},
        {'Increment Step',fmt(step_amount,true)},{'Reset Today','Reset'},{'Past Days','History'}}
    for i,it in ipairs(items) do
        local x,y,rw,rh=settings_row(i);local sel=settings_sel==i
        U.panel({x=x,y=y,w=rw,h=rh},sel)
        U.text(x+12,y+(rh-25)//2,it[1],'ui10',true,'left',not sel)
        U.text(x+rw-12,y+(rh-25)//2,it[2],'ui10',true,'right',not sel)
    end
end

function on_draw()
    if smudge.get_bounds then
        w, h = smudge.get_bounds()
    end
    smudge.clear()
    if show_history then
        draw_history()
    elseif in_settings then
        draw_settings()
    else
        draw_today()
    end
    if in_settings or show_history or not (smudge.get_device and smudge.get_device()=='sticky') then draw_footer() end
end

local function cycle_u(dir)
    local idx = 1
    for i, u in ipairs(UNITS) do
        if u == units then idx = i break end
    end
    idx = idx + (dir or 1)
    if idx > #UNITS then idx = 1 elseif idx < 1 then idx = #UNITS end
    units = UNITS[idx]
    save_data()
end

local function cycle_g(dir)
    local ci, md = 1, 999999
    for i, g in ipairs(GOALS) do
        local d = math.abs(g - daily_goal)
        if d < md then md, ci = d, i end
    end
    ci = ci + (dir or 1)
    if ci > #GOALS then ci = 1 elseif ci < 1 then ci = #GOALS end
    daily_goal = GOALS[ci]
    save_data()
end

local function cycle_s(dir)
    local ci, md = 1, 999999
    for i, s in ipairs(STEPS) do
        local d = math.abs(s - step_amount)
        if d < md then md, ci = d, i end
    end
    ci = ci + (dir or 1)
    if ci > #STEPS then ci = 1 elseif ci < 1 then ci = #STEPS end
    step_amount = STEPS[ci]
    save_data()
end

function on_button(btn, pressed)
    if pressed == false then return end
    check_rollover()
    if show_history then
        if btn == "left" or btn == "page_back" or btn == "up" then history_move(-1);return end
        if btn == "right" or btn == "page_forward" or btn == "down" then history_move(1);return end
        if btn == "btn1" or btn == "btn2" or btn == "back" or btn == "confirm" then
            show_history, in_settings = false, true
        end
        return
    end

    if in_settings then
        if btn == "btn1" or btn == "back" then
            in_settings = false
            return
        elseif btn == "btn3" or btn == "left" then
            -- Left button moves UP in menu
            settings_sel = (settings_sel == 1) and 5 or (settings_sel - 1)
            return
        elseif btn == "btn4" or btn == "right" then
            -- Right button moves DOWN in menu
            settings_sel = (settings_sel == 5) and 1 or (settings_sel + 1)
            return
        elseif btn == "up" or btn == "page_back" then
            -- Up button / page back cycles value left
            if settings_sel == 1 then cycle_u(-1)
            elseif settings_sel == 2 then cycle_g(-1)
            elseif settings_sel == 3 then cycle_s(-1) end
            return
        elseif btn == "down" or btn == "page_forward" then
            -- Down button / page forward cycles value right
            if settings_sel == 1 then cycle_u(1)
            elseif settings_sel == 2 then cycle_g(1)
            elseif settings_sel == 3 then cycle_s(1) end
            return
        elseif btn == "btn2" or btn == "confirm" then
            -- Confirm button / select: cycles value or triggers action
            if settings_sel == 1 then cycle_u(1)
            elseif settings_sel == 2 then cycle_g(1)
            elseif settings_sel == 3 then cycle_s(1)
            elseif settings_sel == 4 then
                today_drank = 0
                clear_undo()
                show_banner("Today Water Reset", 2200)
                save_data()
                in_settings = false
            elseif settings_sel == 5 then
                show_history = true
            end
            return
        end
        return
    end

    -- Today screen buttons
    if btn == "btn1" or btn == "back" then
        smudge.exit()
    elseif btn == "btn2" or btn == "confirm" then
        in_settings = true
    elseif btn == "btn3" or btn == "left" or btn == "page_back" then
        add_water(-step_amount)
    elseif btn == "btn4" or btn == "right" or btn == "page_forward" then
        add_water(step_amount)
    elseif btn == "up" then
        add_water(-step_amount)
    elseif btn == "down" then
        add_water(step_amount)
    end
end

function on_touch(action, tx, ty)
    if smudge.get_device and smudge.get_device() == "sticky" then
        action, tx, ty = ty, action, tx
    end
    if action ~= "tap" and action ~= "click" then return end
    check_rollover()
    for i = 1, 4 do
        if contains(tx, ty, footer_rect(i)) then
            on_button(({"back", "confirm", "left", "right"})[i], true)
            return
        end
    end
    if not in_settings and not show_history and smudge.get_device and smudge.get_device()=='sticky' then
        if ty<66 then in_settings=true;return end
        for _,r in ipairs(quick_actions)do
            if U.hit(r,tx,ty) then
                if r.amount then add_water(r.amount) else undo_last() end
                return
            end
        end
        return
    end
    if ty>=h-60 then return end
    if show_history then
        if ty < ((smudge.get_device and smudge.get_device()=='sticky') and 64 or 80) then
            show_history, in_settings = false, true
        end
        return
    end

    if in_settings then
        if ty < ((smudge.get_device and smudge.get_device()=='sticky') and 64 or 80) then
            in_settings = false
            return
        end
        local start_y = 96
        local row_h = 68
        local row_gap = 8
        for i = 1, 5 do
            local rx, ry, rw, rh = settings_row(i)
            if contains(tx, ty, rx, ry, rw, rh) then
                settings_sel = i
                if i == 1 then cycle_u(1)
                elseif i == 2 then cycle_g(1)
                elseif i == 3 then cycle_s(1)
                elseif i == 4 then
                    today_drank = 0
                    clear_undo()
                    show_banner("Today Water Reset", 2200)
                    save_data()
                    in_settings = false
                elseif i == 5 then
                    show_history = true
                end
                return
            end
        end
        return
    end

    -- Only the visible cup remains a quick-add target on Sticky.
    if smudge.get_device and smudge.get_device() == 'sticky' then
        if U.hit(cup_rect,tx,ty) then add_water(step_amount) end
        return
    end
    -- Legacy non-Sticky touch zones
    local cx = w > h and w // 4 or math.floor(w / 2)
    local cup_y = w > h and 155 or 175
    if contains(tx, ty, cx - 100, cup_y - 32, 200, 252) then
        add_water(step_amount)
        return
    end
    if w <= h and ty >= 120 and ty <= 390 and math.abs(tx - cx) <= 120 then
        add_water(step_amount)
        return
    end
    if w <= h and smudge.has_touch and smudge.has_touch() and ty >= 670 and ty <= 740 and math.abs(tx - cx) <= 100 then
        add_water(step_amount)
        return
    end
    if ty <= 80 and tx >= (w - 120) then
        in_settings = true
        return
    end
end

-- Admit Sticky callbacks only against the last completed painted view. Keep
-- legacy action-first dispatch untouched; wrappers preserve the action bodies.
local painted_view
local function input_view()
    return tostring(in_settings)..':'..tostring(show_history)..':'..settings_sel
end
local function sticky_input_ready()
    return not (smudge.get_device and smudge.get_device() == 'sticky')
        or painted_view == input_view()
end
local function retire_changed_view()
    if not sticky_input_ready() then footer = {}; cup_rect = nil; quick_actions={} end
end
local draw_view, button_view, touch_view = on_draw, on_button, on_touch
function on_draw()
    painted_view = nil
    draw_view()
    painted_view = input_view()
end
function on_button(btn, pressed)
    if not sticky_input_ready() then return end
    button_view(btn, pressed)
    retire_changed_view()
end
function on_touch(action, tx, ty)
    if not sticky_input_ready() then return end
    touch_view(action, tx, ty)
    retire_changed_view()
end
