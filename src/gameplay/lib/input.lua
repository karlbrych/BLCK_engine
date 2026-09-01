-- gameplay/lib/input.lua
--
-- Input as actions rather than keys. A script asks whether "jump" is down, not
-- whether "space" is; which key that means lives in config.keys and nowhere
-- else, so rebinding is one line and no logic has to move.
--
-- It also owns edge detection. "Was this pressed *this* frame" needs last
-- frame's state to compare against, and every script that wanted it used to
-- keep its own `somethingWasDown` field and update it by hand -- four copies of
-- the same three lines, each its own chance to forget the bookkeeping. Here it
-- is done once, for every action, in update().

local Input = { library = true }

local binds = {}
local down = {}
local previous = {}

local function readKey(bound)
    if bound == nil then
        return false
    end
    if type(bound) == "table" then
        for _, key in ipairs(bound) do
            if Engine.input.key(key) then
                return true
            end
        end
        return false
    end
    return Engine.input.key(bound)
end

-- Called once from the scene's init with config.keys. Seeds both frames of
-- state, so a key already held while the scene loads does not read as a fresh
-- press on the first frame.
function Input.bind(keymap)
    binds = keymap or {}
    down, previous = {}, {}
    for action in pairs(binds) do
        local state = readKey(binds[action])
        down[action] = state
        previous[action] = state
    end
    return Input
end

-- Once per frame, before anything reads. The whole frame then sees one
-- consistent snapshot rather than whatever the keyboard happened to be doing
-- at the moment each script got around to asking.
function Input.update()
    for action, bound in pairs(binds) do
        previous[action] = down[action]
        down[action] = readKey(bound)
    end
    return Input
end

function Input.down(action)
    return down[action] == true
end

-- The frame the key goes down, and only that frame: what a toggle wants.
function Input.pressed(action)
    return down[action] == true and previous[action] ~= true
end

function Input.released(action)
    return down[action] ~= true and previous[action] == true
end

-- -1, 0 or +1. Holding both is a standstill, which is what a player expects.
function Input.axis(negative, positive)
    local value = 0
    if Input.down(negative) then value = value - 1 end
    if Input.down(positive) then value = value + 1 end
    return value
end

-- For help text, so what is printed cannot drift from what is bound.
function Input.keyFor(action)
    local bound = binds[action]
    if type(bound) == "table" then
        return table.concat(bound, "/")
    end
    return bound or "unbound"
end

return Input
