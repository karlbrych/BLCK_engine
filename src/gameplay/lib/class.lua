-- gameplay/lib/class.lua
--
-- The three lines every class in here was repeating, in one place.
--
--   local Player = class("Player")
--   function Player.new(...) return setmetatable({}, Player) end
--   function Player:update(dt) end
--
-- `__index` pointing back at the table is also how the script manager tells a
-- class from a scene: it refuses to call init/update/draw on a table shaped
-- like this, because those are instance methods meant for objects the scene
-- builds, not frame hooks. Building classes through here keeps that contract
-- without every file having to remember why it matters.

return function(name)
    local class = {}
    class.__index = class
    class.__name = name or "class"
    return class
end
