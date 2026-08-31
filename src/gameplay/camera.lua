-- gameplay/camera.lua
-- scriptova verze kamery ktera slouzi jako wrapper nad EngineCamera.cpp


local Camera = {}

local Instance = {}
Instance.__index = Instance

local function copy3(value, fallback)
    if type(value) ~= "table" then
        return { fallback[1], fallback[2], fallback[3] }
    end
    return {
        value[1] or value.x or fallback[1],
        value[2] or value.y or fallback[2],
        value[3] or value.z or fallback[3],
    }
end
function Camera.new(options)
    options = options or {}

    local self = setmetatable({}, Instance)

    self.pivot = copy3(options.target, { 0, 0, 0 })
    --wrapper EngineCamera modulu
    self.handle = Engine.camera.new({
        position = copy3(options.position, { 3, 2, 4 }),
        target = self.pivot,
        fov = options.fov or 60,
        near = options.near or 0.1,
        far = options.far or 500,
    })

    self.orbitSpeed = options.orbitSpeed or 90
    self.mouseSpeed = options.mouseSpeed or 0.25
    self.moveSpeed = options.moveSpeed or 4
    self.zoomSpeed = options.zoomSpeed or 5
    self.boostKey = options.boostKey or "lctrl"
    self.boost = options.boost or 6

    self.mouseX, self.mouseY = Engine.input.mouse()
    self.dragging = false

    -- Attachment state, filled in by attach().
    self.target = nil
    self.eyeOffset = nil
    self.nearPlane = nil
    self.farPlane = nil

    -- The cursor mode is GLFW state: it outlives a script reload exactly the
    -- way the renderer's does. Assert it here rather than assume a released
    -- cursor, or an edit made while in first-person leaves the pointer trapped.
    self.mode = nil
    self.cursorLocked = false
    self.lookWarm = false
    self:setMode(options.mode or "orbit")
    Engine.input.setCursorLocked(self.cursorLocked)

    return self
end

function Instance:activate()
    Engine.renderer.setCamera(self.handle)
    return self
end

function Instance:setPosition(x, y, z)
    self.handle:setPosition(x, y, z)
    return self
end

function Instance:getPosition()
    return self.handle:getPosition()
end

function Instance:lookAt(x, y, z)
    self.handle:lookAt(x, y, z)
    return self
end

function Instance:setTarget(target)
    self.pivot = copy3(target, self.pivot)
    self.handle:lookAt(self.pivot)
    return self
end

-- Switching in or out of first-person captures or releases the cursor, which is
-- why this is not just a field assignment.
function Instance:setMode(mode)
    if mode == self.mode then
        return self
    end
    self.mode = mode

    local locked = (mode == "first")
    if locked ~= self.cursorLocked then
        self.cursorLocked = locked
        Engine.input.setCursorLocked(locked)
        -- GLFW teleports the pointer when the mode changes; that jump is not a
        -- look input, so the next delta is thrown away.
        self.lookWarm = true
    end

    if locked then
        if self.nearPlane and self.farPlane then
            self.handle:setClipPlanes(self.nearPlane, self.farPlane)
        end
        self:snapToTarget()
    elseif self.target then
        self.target.visible = true
    end
    return self
end

-- Binds the camera to anything carrying a `position` table -- the player, but a
-- vehicle or a spectated entity would do just as well.
--
-- options:
--   eyeHeight   world units above the target's position (default 0.9 * its size)
--   near, far   clip planes to use in first-person, reapplied on every switch
--               back. A first-person view of a 10,000-unit map needs a far plane
--               to match, and the near plane has to stay in proportion or the
--               depth buffer runs out of precision.
function Instance:attach(target, options)
    options = options or {}
    self.target = target
    self.eyeOffset = options.eyeHeight
    -- Default to whatever the camera was built with, so the planes only have to
    -- be stated once and switching back to first-person restores them.
    self.nearPlane = options.near or self.handle:getNear()
    self.farPlane = options.far or self.handle:getFar()

    if self.mode == "first" then
        if self.nearPlane and self.farPlane then
            self.handle:setClipPlanes(self.nearPlane, self.farPlane)
        end
        self:snapToTarget()
    end
    return self
end

function Instance:detach()
    if self.target then
        self.target.visible = true
    end
    self.target = nil
    return self
end

-- Height of the eye above the target's own position.
function Instance:eyeHeight()
    if self.eyeOffset then
        return self.eyeOffset
    end
    local size = self.target and self.target.size or 1.8
    return size * 0.9 -- eyes sit near the top of a body, not at its middle
end

-- Plants the camera in the target's head. Hiding the target is part of the same
-- thought: from inside your own head, your body is not something you can see,
-- and drawing it would fill the screen with the inside of a cube.
function Instance:snapToTarget()
    local target = self.target
    if not target then
        return self
    end
    local p = target.position
    self.handle:setPosition(p.x, p.y + self:eyeHeight(), p.z)
    target.visible = false
    return self
end

-- The look direction flattened onto the ground plane, as x, z. This is what a
-- walking body wants: at pitch 0 forward is (sin yaw, -cos yaw) and right is
-- (cos yaw, sin yaw), so looking up must not tip the player into the sky.
function Instance:forwardFlat()
    local yaw = math.rad(self.handle:getYaw())
    return math.sin(yaw), -math.cos(yaw)
end

function Instance:rightFlat()
    local yaw = math.rad(self.handle:getYaw())
    return math.cos(yaw), math.sin(yaw)
end

function Instance:getYaw()
    return self.handle:getYaw()
end

function Instance:setClipPlanes(near, far)
    self.handle:setClipPlanes(near, far)
    return self
end

function Instance:setFov(degrees)
    self.handle:setFov(degrees)
    return self
end

function Instance:orbit(deltaYaw, deltaPitch)
    self.handle:orbit(self.pivot, deltaYaw, deltaPitch)
    return self
end

function Instance:dolly(amount)
    self.handle:dolly(self.pivot, amount)
    return self
end

function Instance:frame(model, options)
    options = options or {}

    local cx, cy, cz = model:center()
    local sx, sy, sz = model:size()
    local hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5

    local yaw = math.rad(options.yaw or 45)
    local pitch = math.rad(options.pitch or 35)

    local dirX = math.cos(pitch) * math.sin(yaw)
    local dirY = math.sin(pitch)
    local dirZ = math.cos(pitch) * math.cos(yaw)

    local rx, ry, rz = math.cos(yaw), 0, -math.sin(yaw)
    local ux = -math.sin(pitch) * math.sin(yaw)
    local uy = math.cos(pitch)
    local uz = -math.sin(pitch) * math.cos(yaw)

    local tanHalfFov = math.tan(math.rad(self.handle:getFov()) * 0.5)
    local aspect = math.max(Engine.window.aspect(), 0.1)

    local distance = 0.001
    for corner = 0, 7 do
        local ox = (corner % 2 == 0) and -hx or hx
        local oy = (math.floor(corner / 2) % 2 == 0) and -hy or hy
        local oz = (math.floor(corner / 4) % 2 == 0) and -hz or hz

        local screenX = ox * rx + oy * ry + oz * rz
        local screenY = ox * ux + oy * uy + oz * uz
        local lean = ox * dirX + oy * dirY + oz * dirZ

        local needed = math.max(math.abs(screenY) / tanHalfFov,
            math.abs(screenX) / (tanHalfFov * aspect)) + lean
        distance = math.max(distance, needed)
    end
    distance = distance * (options.distance or 1.05)

    self:setTarget({ cx, cy, cz })
    self.handle:setPosition(cx + dirX * distance, cy + dirY * distance, cz + dirZ * distance)
    self.handle:lookAt(cx, cy, cz)
    self.handle:setClipPlanes(math.max(distance * 0.001, 0.05), distance * 10)

    local radius = math.max(model:radius(), 0.001)
    self.moveSpeed = radius * 0.5
    self.zoomSpeed = radius * 0.75
    return self
end

function Instance:mouseDelta()
    local x, y = Engine.input.mouse()
    local dx, dy = x - self.mouseX, y - self.mouseY
    self.mouseX, self.mouseY = x, y

    local dragging = Engine.input.mouseButton(1)
    local started = dragging and not self.dragging
    self.dragging = dragging

    -- One frame is swallowed after the cursor is captured or released: GLFW
    -- moves the pointer on the switch, and that jump is not the player looking.
    if self.lookWarm then
        self.lookWarm = false
        return 0, 0, dragging
    end

    -- Captured (first-person): every movement is a look. Otherwise a look costs
    -- a held button, and the frame a drag begins reports zero, so clicking after
    -- moving the cursor does not snap the view.
    if self.cursorLocked then
        return dx, dy, dragging
    end
    if not dragging or started then
        return 0, 0, dragging
    end
    return dx, dy, dragging
end

local function axis(negative, positive)
    local value = 0
    if Engine.input.key(negative) then value = value - 1 end
    if Engine.input.key(positive) then value = value + 1 end
    return value
end

function Instance:update(dt)
    local dx, dy, dragging = self:mouseDelta()

    -- First-person: look only. Where the body goes is the body's business, and
    -- it has already moved by the time this runs -- the scene updates the
    -- player first, so the eye lands on this frame's position, not last one's.
    if self.mode == "first" then
        if dx ~= 0 or dy ~= 0 then
            self.handle:rotate(dx * self.mouseSpeed, -dy * self.mouseSpeed)
        end
        self:snapToTarget()
        return
    end

    if self.mode == "fly" then
        if dragging then
            self.handle:rotate(dx * self.mouseSpeed, -dy * self.mouseSpeed)
        end
        local right = axis("a", "d")
        local up = axis("lshift", "space")
        local forward = axis("s", "w")
        if right ~= 0 or up ~= 0 or forward ~= 0 then
            local speed = self.moveSpeed * dt
            if Engine.input.key(self.boostKey) then
                speed = speed * self.boost
            end
            self.handle:moveLocal(right * speed, up * speed, forward * speed)
        end
        return
    end
    local yaw = axis("left", "right") + axis("a", "d")
    local pitch = axis("down", "up") + axis("s", "w")

    yaw = yaw * self.orbitSpeed * dt
    pitch = pitch * self.orbitSpeed * dt

    if dragging then
        yaw = yaw + dx * self.mouseSpeed
        pitch = pitch - dy * self.mouseSpeed
    end

    if yaw ~= 0 or pitch ~= 0 then
        self.handle:orbit(self.pivot, yaw, pitch)
    end

    local zoom = axis("e", "q")
    if zoom ~= 0 then
        local speed = self.zoomSpeed * dt
        if Engine.input.key(self.boostKey) then
            speed = speed * self.boost
        end
        self.handle:dolly(self.pivot, zoom * speed)
    end
end

return Camera
