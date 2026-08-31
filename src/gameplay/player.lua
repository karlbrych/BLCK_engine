-- gameplay/player.lua
--
-- The player: a position, a body, and the movement that drives them. It does
-- not own the look direction -- that belongs to the camera attached to it --
-- but it asks the camera which way forward is, so walking follows the view.
--
-- Returned as a class, so the script manager leaves it alone: the scene that
-- owns the object is what calls init/update/draw on it.

local Player = {}
Player.__index = Player

local function axis(negative, positive)
    local value = 0
    if Engine.input.key(negative) then value = value - 1 end
    if Engine.input.key(positive) then value = value + 1 end
    return value
end

-- size is the body's full height in world units. Everything else scales off it,
-- so the same player works on a map measured in metres or in centimetres.
function Player.new(name, health, size)
    local self = setmetatable({}, Player)
    self.mesh = Engine.mesh.cube(1.0) --zatim jen placeholder kostka
    self.name = name or "Player"
    self.health = health
    self.size = size or 1.8
    self.position = { x = 0, y = 0, z = 0 }

    -- Roughly 2.2 body-heights per second, which is a walk at human scale and
    -- stays a walk whatever the map's units turn out to be.
    self.walkSpeed = self.size * 2.2
    self.sprintMultiplier = 3.0
    self.sprintKey = "lshift"
    self.upKey = "space"
    self.downKey = "lctrl"

    -- Cleared by the camera while it is looking out of this player's eyes.
    self.visible = true
    return self
end

function Player:init()
    self.velocity = { x = 0, y = 0, z = 0 }
    return self
end

function Player:spawn(x, y, z)
    self.position.x, self.position.y, self.position.z = x, y, z
    return self
end

function Player:eyePosition(eyeHeight)
    local p = self.position
    return p.x, p.y + (eyeHeight or self.size * 0.9), p.z
end

-- Moves along the camera's flattened axes, so W walks where you are looking
-- rather than where the world's -Z happens to point. Looking up must not lift
-- the player off the ground, which is why the flat basis exists at all.
--
-- There is no ground query in the engine yet, so nothing holds the player to
-- the terrain: it walks a level plane, and space/ctrl move that plane up and
-- down. Once a raycast against the map exists, the vertical axis here is what
-- gravity and a ground height replace.
function Player:update(dt, camera)
    local right = axis("a", "d")
    local forward = axis("s", "w")
    local vertical = axis(self.downKey, self.upKey)

    local speed = self.walkSpeed
    if Engine.input.key(self.sprintKey) then
        speed = speed * self.sprintMultiplier
    end

    if vertical ~= 0 then
        self.position.y = self.position.y + vertical * speed * dt
    end
    if right == 0 and forward == 0 then
        return self
    end

    local fx, fz = 0, -1
    local rx, rz = 1, 0
    if camera then
        fx, fz = camera:forwardFlat()
        rx, rz = camera:rightFlat()
    end

    local dx = fx * forward + rx * right
    local dz = fz * forward + rz * right

    -- Normalised, or holding two keys would walk faster on the diagonal.
    local length = math.sqrt(dx * dx + dz * dz)
    if length < 1e-6 then
        return self
    end
    dx, dz = dx / length, dz / length

    self.position.x = self.position.x + dx * speed * dt
    self.position.z = self.position.z + dz * speed * dt
    return self
end

function Player:draw(shader)
    if not self.visible then
        return self -- inside its own head: there is nothing to see
    end
    Engine.renderer.submit({
        mesh = self.mesh,
        shader = shader,
        position = { self.position.x, self.position.y + self.size * 0.5, self.position.z },
        scale = self.size,
        color = { 0.85, 0.35, 0.25 },
    })
    return self
end

function Player:shutdown()
    --TODO: destruktor hrace
end

return Player
