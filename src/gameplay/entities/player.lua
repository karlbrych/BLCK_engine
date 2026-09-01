-- gameplay/entities/player.lua
--
-- The player: a position, a body, and the movement that drives them. It does
-- not own the look direction -- that belongs to the camera attached to it --
-- but it asks the camera which way forward is, so walking follows the view.
--
-- It knows nothing about the map, only about a ground query the scene hands it,
-- and nothing about keys, only about actions. Both are on purpose: the same
-- class drives a body on any world, with input from anywhere.

local class = require("lib.class")
local Input = require("lib.input")

local Player = class("Player")

-- spec is a table of metres and seconds (see config.player); unitsPerMetre is
-- what the world is measured in. Converting once, here, is why nothing below
-- has to think about the map's scale ever again.
function Player.new(spec, unitsPerMetre)
    spec = spec or {}
    local scale = unitsPerMetre or 1.0

    local self = setmetatable({}, Player)

    self.name = spec.name or "Player"
    self.health = spec.health or 100
    self.color = spec.color or { 0.85, 0.35, 0.25 }
    self.mesh = Engine.mesh.cube(1.0) --zatim jen placeholder kostka

    self.size = (spec.height or 1.8) * scale
    self.eyeHeight = (spec.eyeHeight or (spec.height or 1.8) * 0.9) * scale
    self.walkSpeed = (spec.walkSpeed or 4.0) * scale
    self.sprintMultiplier = spec.sprintMultiplier or 3.0
    self.gravity = (spec.gravity or 9.81) * scale
    self.stepHeight = (spec.stepHeight or 0.5) * scale
    self.terminalSpeed = (spec.terminalSpeed or 55.0) * scale
    -- The speed that just reaches the wanted apex: v = sqrt(2gh).
    self.jumpSpeed = math.sqrt(2 * self.gravity * (spec.jumpHeight or 0.6) * scale)

    self.position = { x = 0, y = 0, z = 0 }
    self.velocity = { x = 0, y = 0, z = 0 }

    -- Set by the scene: function(x, z, fromY) -> ground height, or nil for a
    -- hole. Without one there is nothing to fall onto, so the player flies.
    self.groundQuery = nil
    self.grounded = false
    self.flying = false

    -- Cleared by the camera while it is looking out of this player's eyes.
    self.visible = true
    return self
end

function Player:setGround(query)
    self.groundQuery = query
    return self
end

-- Highest surface at (x, z) at or below fromY, or nil. The cap is what lets the
-- player climb a kerb without ever being yanked up onto a roof they are
-- standing under.
function Player:groundAt(x, z, fromY)
    if not self.groundQuery then
        return nil
    end
    return self.groundQuery(x, z, fromY)
end

-- y is optional: leave it out and the player is dropped onto the ground at
-- (x, z), which is the usual way to place someone on a map.
function Player:spawn(x, y, z)
    self.position.x = x
    self.position.y = y or self:groundAt(x, z) or 0
    self.position.z = z
    self.velocity.y = 0
    self.grounded = self.groundQuery ~= nil
    return self
end

function Player:eyePosition()
    local p = self.position
    return p.x, p.y + self.eyeHeight, p.z
end

function Player:setFlying(flying)
    self.flying = flying
    self.velocity.y = 0
    self.grounded = false
    return self
end

-- One frame of movement. Horizontal first, then vertical, so the ground is
-- sampled under where the feet have just arrived and walking into a slope
-- climbs it in the same frame.
function Player:update(dt, camera)
    if Input.pressed("toggleFly") then
        self:setFlying(not self.flying)
        Engine.log("player: flying -> " .. tostring(self.flying))
    end

    local speed = self.walkSpeed
    if Input.down("sprint") then
        speed = speed * self.sprintMultiplier
    end

    self:walk(dt, camera, speed)

    if self.flying or not self.groundQuery then
        self:hover(dt, speed)
    else
        self:fall(dt)
    end
    return self
end

-- Along the camera's flattened axes, so W walks where you are looking rather
-- than where the world's -Z happens to point. Flattened because looking up must
-- not lift the player off the ground.
--
-- Nothing stops the player walking *into* a wall yet: there is a ground height
-- but no horizontal collision, so a cliff face is climbed rather than bumped
-- into.
function Player:walk(dt, camera, speed)
    local forward = Input.axis("moveBack", "moveForward")
    local right = Input.axis("moveLeft", "moveRight")
    if forward == 0 and right == 0 then
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

    self.position.x = self.position.x + (dx / length) * speed * dt
    self.position.z = self.position.z + (dz / length) * speed * dt
    return self
end

-- Free vertical movement, for flying and for a world with no ground to stand on.
function Player:hover(dt, speed)
    local vertical = Input.axis("moveDown", "moveUp")
    if vertical ~= 0 then
        self.position.y = self.position.y + vertical * speed * dt
    end
    return self
end

-- Gravity, jumping, landing, and staying glued to the surface underfoot. One
-- downward ray answers all of it: cast from a step above the feet, then read
-- differently depending on whether the player is currently standing on
-- something.
function Player:fall(dt)
    local ground = self:groundAt(self.position.x, self.position.z,
        self.position.y + self.stepHeight)

    if self.grounded and Input.pressed("jump") then
        self.velocity.y = self.jumpSpeed
        self.grounded = false
    end

    if self.grounded then
        if ground and self.position.y - ground <= self.stepHeight then
            -- Follow the terrain instead of stepping off every crest and
            -- falling back onto every dip.
            self.position.y = ground
            self.velocity.y = 0
            return self
        end
        self.grounded = false -- walked off an edge, or off the map entirely
    end

    self.velocity.y = math.max(self.velocity.y - self.gravity * dt, -self.terminalSpeed)
    self.position.y = self.position.y + self.velocity.y * dt

    -- Only a descent lands: rising through a floor is a jump under an overhang,
    -- not a landing on it.
    if ground and self.velocity.y <= 0 and self.position.y <= ground then
        self.position.y = ground
        self.velocity.y = 0
        self.grounded = true
    end
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
        color = self.color,
    })
    return self
end

function Player:shutdown()
    --TODO: destruktor hrace
end

return Player
