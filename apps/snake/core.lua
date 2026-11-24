-- Original bounded Snake rules; no display, clock or persistence dependencies.
local M = {}

local function spawn(g)
    local occupied = {}
    for _, p in ipairs(g.snake) do occupied[p.y * g.cols + p.x] = true end
    local free = g.cols * g.rows - #g.snake
    if free == 0 then g.food = nil; g.status = 'won'; return end
    local choice = g.random(free)
    for y = 0, g.rows - 1 do
        for x = 0, g.cols - 1 do
            if not occupied[y * g.cols + x] then
                choice = choice - 1
                if choice == 0 then g.food = {x = x, y = y}; return end
            end
        end
    end
end

function M.new(cols, rows, random)
    cols = math.max(3, math.min(32, math.floor(cols)))
    rows = math.max(1, math.min(32, math.floor(rows)))
    local g = {cols = cols, rows = rows, snake = {}, direction = 'right',
        pending = 'right', status = 'ready', score = 0, random = random or math.random}
    local x, y = math.max(2, math.floor(cols / 2)), math.floor(rows * 0.62)
    for i = 0, math.min(3, cols - 1) - 1 do
        g.snake[#g.snake + 1] = {x = x - i, y = y}
    end
    spawn(g)
    return g
end

local directions = {right = {1, 0}, left = {-1, 0}, up = {0, -1}, down = {0, 1}}
local opposite = {right = 'left', left = 'right', up = 'down', down = 'up'}

function M.turn(g, dir)
    if not directions[dir] or (g.status ~= 'ready' and g.status ~= 'running' and g.status ~= 'paused') then return end
    if g.status == 'ready' and dir == 'left' then
        local x = math.min(g.snake[1].x, g.cols - #g.snake)
        local y = g.snake[1].y
        for i, p in ipairs(g.snake) do p.x = x + i - 1; p.y = y end
        for _, p in ipairs(g.snake) do
            if g.food and p.x == g.food.x and p.y == g.food.y then spawn(g); break end
        end
        -- On narrow boards slide the starting head left to retain body length.
        g.direction = 'left'
        g.pending = 'left'
        g.queued = true
    end
    g.status = 'running'
    if not g.queued and dir ~= opposite[g.direction] and dir ~= g.direction then
        g.pending = dir
        g.queued = true
    elseif not g.queued and g.direction == dir then
        g.pending = dir
    end
end

function M.step(g)
    if g.status ~= 'running' then return end
    g.direction = g.pending
    g.queued = false
    local delta = directions[g.direction]
    local x, y = g.snake[1].x + delta[1], g.snake[1].y + delta[2]
    if x < 0 or x >= g.cols or y < 0 or y >= g.rows then g.status = 'over'; return end
    local eating = g.food and x == g.food.x and y == g.food.y
    local last = #g.snake
    if not eating then last = last - 1 end
    for i = 1, last do
        local p = g.snake[i]
        if p.x == x and p.y == y then g.status = 'over'; return end
    end
    table.insert(g.snake, 1, {x = x, y = y})
    if eating then
        g.score = g.score + 1
        spawn(g)
    else
        table.remove(g.snake)
    end
end

function M.pause(g)
    if g.status == 'running' then g.status = 'paused'
    elseif g.status == 'paused' then g.status = 'running' end
end

function M.interval(g)
    return math.max(220, 350 - g.score * 5)
end

return M
