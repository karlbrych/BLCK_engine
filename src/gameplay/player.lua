local Player = {}
Player.__index = Player

function Player.new(name,health,size)
    local self = setmetatable({}, Player)
    self.mesh = Engine.mesh.cube(1.0) --zatim jen placeholder kostka
    self.name = name or "Player"
    self.health = health
    self.size = size or 1.8
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
return Player
