local Player = {}
Player.__index = Player

function Player.new(name,health)
    local self = setmetatable({}, Player)
    self.mesh = Engine.mesh.cube(1.0)
    self.name = name or "Player"
    self.health = health
    self.position = {x=0, y=0, z=0}
    return self
end

function Player:init()
    --TODO: konstruktor hrace
end
function Player:update(dt)
    --TODO: funkncnost pohybu a kamery hrace
end
function Player:draw()
    --TODO drawing hrace
end
function Player:shutdown()
    --TODO: destruktor hrace
end

-- Returning the class is what makes require("player") hand back Player rather
-- than `true`. Because Player.__index is Player, the script manager reads this
-- as a class to instantiate and leaves the methods below to the scene, instead
-- of running them once a frame against the class table itself.
return Player
