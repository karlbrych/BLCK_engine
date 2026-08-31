-- gameplay/camera.lua
--
-- The scripting-side camera. All of the maths lives in EngineCamera.cpp: this
-- module owns one of those objects and wraps it in the behaviour a scene wants
-- -- orbiting a pivot, flying around, reacting to input -- so the C++ class can
-- stay a plain view/projection provider with no opinion about controls.
--
-- The module itself only exposes new(); the instance methods live on a private
-- metatable, which is also why the script manager leaves this file alone
-- instead of treating update() below as a per-frame hook.

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

-- options:
--   position   {x, y, z}          where the camera starts
--   target     {x, y, z}          what it looks at, and the orbit pivot
--   fov        degrees            vertical field of view
--   near, far  world units        clip planes
--   mode       "orbit" | "fly"    which control scheme update() runs
--   moveSpeed  units per second  fly speed; boostKey multiplies it
function Camera.new(options)
    options = options or {}

    local self = setmetatable({}, Instance)

    self.pivot = copy3(options.target, { 0, 0, 0 })
    -- The C++ camera. Everything below is a thin shell over this handle.
    self.handle = Engine.camera.new({
        position = copy3(options.position, { 3, 2, 4 }),
        target = self.pivot,
        fov = options.fov or 60,
        near = options.near or 0.1,
        far = options.far or 500,
    })

    self.mode = options.mode or "orbit"
    self.orbitSpeed = options.orbitSpeed or 90   -- degrees per second, keyboard
    self.mouseSpeed = options.mouseSpeed or 0.25 -- degrees per pixel dragged
    self.moveSpeed = options.moveSpeed or 4      -- world units per second
    self.zoomSpeed = options.zoomSpeed or 5      -- world units per second
    self.boostKey = options.boostKey or "lctrl"  -- held down to move faster
    self.boost = options.boost or 6

    self.mouseX, self.mouseY = Engine.input.mouse()
    self.dragging = false

    return self
end

-- Makes this the camera the renderer feeds to every shader it binds.
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

-- Moves the orbit pivot and turns to face it.
function Instance:setTarget(target)
    self.pivot = copy3(target, self.pivot)
    self.handle:lookAt(self.pivot)
    return self
end

function Instance:setMode(mode)
    self.mode = mode
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

-- Frames a loaded model: pivots on its centre and backs off far enough for the
-- whole bounding box to fit on screen, then sizes the clip planes and the
-- movement speeds to match. Without this a 1000-unit-wide map either fills the
-- screen from the inside or sits entirely behind the far plane.
--
-- The fit measures the box against both the vertical and the horizontal field
-- of view. A bounding sphere would be simpler, but a map is a wide flat plate:
-- its sphere is twice the size of anything actually on screen, and framing on
-- it leaves the map a postage stamp in the middle of the window.
--
-- options:
--   distance    multiplier on the fitted distance (default 1.05, a little margin)
--   yaw, pitch  degrees to approach from (default 45 around, 35 above)
function Instance:frame(model, options)
    options = options or {}

    local cx, cy, cz = model:center()
    local sx, sy, sz = model:size()
    local hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5

    local yaw = math.rad(options.yaw or 45)
    local pitch = math.rad(options.pitch or 35)

    -- Where the camera will sit, as a unit vector from the pivot.
    local dirX = math.cos(pitch) * math.sin(yaw)
    local dirY = math.sin(pitch)
    local dirZ = math.cos(pitch) * math.cos(yaw)

    -- The screen axes at that orientation: right is horizontal in world space,
    -- up is whatever completes the frame.
    local rx, ry, rz = math.cos(yaw), 0, -math.sin(yaw)
    local ux = -math.sin(pitch) * math.sin(yaw)
    local uy = math.cos(pitch)
    local uz = -math.sin(pitch) * math.cos(yaw)

    -- Exact fit, one corner at a time. A corner sitting `depth` in front of the
    -- camera fits when |screenY| <= depth * tan(fov/2), and depth is itself
    -- (distance - how far that corner leans towards the camera) -- so each
    -- corner states the smallest distance that would contain it, and the
    -- largest of those answers is the one to use. Doing it per corner rather
    -- than on the box's overall extents is what keeps a wide flat plate viewed
    -- from a shallow angle filling the window instead of shrinking into it.
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

    -- A near plane too small against a far this large is what makes depth
    -- buffers fight; scale both with the scene instead of pinning them.
    self.handle:setClipPlanes(math.max(distance * 0.001, 0.05), distance * 10)

    local radius = math.max(model:radius(), 0.001)
    self.moveSpeed = radius * 0.5
    self.zoomSpeed = radius * 0.75
    return self
end

-- Mouse delta since the last frame. The frame a drag begins is reported as zero
-- movement, so clicking after moving the cursor does not snap the view.
function Instance:mouseDelta()
    local x, y = Engine.input.mouse()
    local dx, dy = x - self.mouseX, y - self.mouseY
    self.mouseX, self.mouseY = x, y

    local dragging = Engine.input.mouseButton(1)
    local started = dragging and not self.dragging
    self.dragging = dragging

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

    -- Orbit: arrows or A/D/W/S swing around the pivot, Q/E pull in and out.
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
