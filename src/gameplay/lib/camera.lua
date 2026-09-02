-- gameplay/lib/camera.lua
-- scriptova verze kamery ktera slouzi jako wrapper nad EngineCamera.cpp
--
-- Four modes share one object: "rts" looks down on the map through an
-- orthographic projection and pans and zooms over it, "first" rides a target's
-- head, "orbit" turns around a pivot, "fly" moves freely. Controls come from
-- named actions, so what key does what lives in config.keys like everything
-- else.

local Input = require("lib.input")

local Camera = {}

local Instance = {}
Instance.__index = Instance

-- Right or middle drag pans the map. The left button is deliberately left
-- alone: in an RTS it belongs to whatever is being selected or ordered about.
local PAN_BUTTONS = { 2, 3 }

local function clamp(value, low, high)
    if value < low then
        return low
    end
    if value > high then
        return high
    end
    return value
end

local function anyButton(buttons)
    for _, button in ipairs(buttons) do
        if Engine.input.mouseButton(button) then
            return true
        end
    end
    return false
end

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

    -- Kept because the RTS mode replaces the projection outright: switching
    -- back to a mode that looks out of the world rather than down on it has to
    -- put the perspective one back, and these are what it was built with.
    self.fov = options.fov or 60
    self.near = options.near or 0.1
    self.far = options.far or 500

    --wrapper EngineCamera modulu
    self.handle = Engine.camera.new({
        position = copy3(options.position, { 3, 2, 4 }),
        target = self.pivot,
        fov = self.fov,
        near = self.near,
        far = self.far,
    })

    self.orbitSpeed = options.orbitSpeed or 90
    self.mouseSpeed = options.mouseSpeed or 0.25
    self.moveSpeed = options.moveSpeed or 4
    self.zoomSpeed = options.zoomSpeed or 5
    self.boost = options.boost or 6

    self.mouseX, self.mouseY = Engine.input.mouse()
    self.dragging = false
    self.panDragging = false

    -- Filled in by rts(); until then there is no overhead view to switch to.
    self.rts = nil

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

-- Switching modes is not just a field assignment: first-person captures the
-- cursor and the overhead view swaps the projection, and both have to be undone
-- on the way back out.
function Instance:setMode(mode)
    if mode == self.mode then
        return self
    end
    local previous = self.mode
    self.mode = mode

    if mode == "rts" then
        -- Guarded because new() may set the mode before rts() has said what the
        -- overhead view looks like; the projection is applied there instead.
        if self.rts then
            self:applyRts()
        end
    elseif previous == "rts" then
        self.handle:setPerspective(self.fov, Engine.window.aspect(), self.near, self.far)
    end

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

-- ---------------------------------------------------------------- RTS view
--
-- An orthographic camera hanging at a fixed angle over the map. It pans across
-- the ground and zooms in and out, and it does not turn: with no rotation the
-- only state the view has is the point on the ground it is centred on and how
-- much ground fits on screen, and every control below is one or the other.
--
-- The angle is fixed rather than merely unbound, which is what makes the
-- projection safe to leave orthographic: a parallel projection has no
-- vanishing point, so the scale on screen never depends on how far away the
-- camera hangs, only on the height it is asked to show.
--
-- options:
--   center      {x, y, z} to start over (default the current pivot)
--   bounds      {minX, minZ, maxX, maxZ} the centre may not leave
--   extent      the map's largest horizontal dimension. Every default below
--               that is a length is a fraction of it, so one number fits the
--               camera to a map of any size.
--   yaw, pitch  the fixed angles; pitch is a positive downward tilt
--   height      world units of ground visible top to bottom -- the zoom itself
--   minHeight, maxHeight  how far the zoom may go either way
--   panSpeed    screen-heights per second, so panning covers the same fraction
--               of the view whether zoomed in or out
--   zoomSpeed   e-folds per second on the keys, wheelStep the same per notch:
--               zooming is multiplicative, or a step that feels right up close
--               would take a minute to cross the map from far out
--   edgePan     pan when the pointer rests against the window edge
--   padding     how far past `bounds` the centre may travel, as a fraction of
--               extent, so the map's own corner can be brought to the middle
function Instance:rts(options)
    options = options or {}

    local extent = math.max(options.extent or 100, 0.001)
    local center = copy3(options.center, self.pivot)
    local height = clamp(options.height or extent * 0.35, 0.001, extent * 4)
    local boom = options.boom or extent * 2

    local bounds = options.bounds
    local padding = (options.padding or 0.1) * extent

    self.rts = {
        yaw = options.yaw or 45,
        pitch = clamp(options.pitch or 55, 5, 89),

        focus = { x = center[1], y = center[2], z = center[3] },
        bounds = bounds and {
            minX = math.min(bounds[1], bounds[3]) - padding,
            minZ = math.min(bounds[2], bounds[4]) - padding,
            maxX = math.max(bounds[1], bounds[3]) + padding,
            maxZ = math.max(bounds[2], bounds[4]) + padding,
        } or nil,

        height = height,
        minHeight = options.minHeight or extent * 0.05,
        maxHeight = options.maxHeight or extent * 1.2,

        -- The boom only decides what falls inside the clip planes; it has no
        -- effect on the size of anything, which is the whole point of an
        -- orthographic projection. Far enough back that the tallest thing on
        -- the map is still in front of the camera, and the far plane far enough
        -- that the map's far corner has not fallen out behind it.
        boom = boom,
        near = math.max(extent * 0.001, 0.01),
        far = boom + extent * 2,

        panSpeed = options.panSpeed or 0.9,
        zoomSpeed = options.zoomSpeed or 2.0,
        wheelStep = options.wheelStep or 0.15,
        boost = options.boost or 2.5,

        edgePan = options.edgePan ~= false,
        edgeMargin = options.edgeMargin or 6,
    }
    self.rts.height = clamp(self.rts.height, self.rts.minHeight, self.rts.maxHeight)

    -- setMode applies it, unless the camera is already in this mode -- in which
    -- case nothing has changed for it to notice and the work lands here.
    if self.mode == "rts" then
        self:applyRts()
    else
        self:setMode("rts")
    end
    return self
end

-- Everything the overhead view puts on the handle, in one place, so a hot
-- reload or a mode switch can restore it without remembering an order.
function Instance:applyRts()
    local r = self.rts
    self.handle:setRotation(r.yaw, -r.pitch)
    self.handle:setOrthographic(r.height, Engine.window.aspect(), r.near, r.far)
    self:placeRts()
    return self
end

-- The camera is not where it looks: it hangs one boom behind the centre, along
-- its own (fixed) view direction.
function Instance:placeRts()
    local r = self.rts
    local fx, fy, fz = self.handle:forward()
    self.handle:setPosition(r.focus.x - fx * r.boom, r.focus.y - fy * r.boom,
        r.focus.z - fz * r.boom)
    return self
end

-- The ground point in the middle of the screen. y is kept as it is unless it is
-- given: the view slides across the map, it does not follow the terrain up and
-- down, or crossing a hill would heave the whole screen.
function Instance:setFocus(x, y, z)
    local r = self.rts
    if not r then
        return self
    end

    r.focus.x = x or r.focus.x
    r.focus.y = y or r.focus.y
    r.focus.z = z or r.focus.z

    if r.bounds then
        r.focus.x = clamp(r.focus.x, r.bounds.minX, r.bounds.maxX)
        r.focus.z = clamp(r.focus.z, r.bounds.minZ, r.bounds.maxZ)
    end
    return self:placeRts()
end

function Instance:getFocus()
    local r = self.rts
    if not r then
        return nil
    end
    return r.focus.x, r.focus.y, r.focus.z
end

-- The zoom, as the world height the screen shows. Multiplicative everywhere it
-- is driven from, and clamped here so no caller has to know the limits.
function Instance:setZoom(height)
    local r = self.rts
    if not r then
        return self
    end
    r.height = clamp(height, r.minHeight, r.maxHeight)
    self.handle:setOrthoHeight(r.height)
    return self
end

function Instance:getZoom()
    return self.rts and self.rts.height
end

-- -1, 0 or +1 per axis while the pointer rests against a window edge. Where the
-- pointer is resting only counts while the window is being played: a cursor
-- parked near the edge of a window someone has clicked away from would
-- otherwise pan the map into a corner and hold it there.
function Instance:edgePush()
    local r = self.rts
    if not Engine.window.focused() then
        return 0, 0
    end

    local x, y = Engine.input.mouse()
    local width, height = Engine.window.width(), Engine.window.height()
    if x < 0 or y < 0 or x >= width or y >= height then
        return 0, 0
    end

    local pushX, pushZ = 0, 0
    if x < r.edgeMargin then
        pushX = -1
    elseif x >= width - r.edgeMargin then
        pushX = 1
    end
    -- Screen-up is away from the camera, and the mouse counts y downwards.
    if y < r.edgeMargin then
        pushZ = 1
    elseif y >= height - r.edgeMargin then
        pushZ = -1
    end
    return pushX, pushZ
end

function Instance:updateRts(dt)
    local r = self.rts
    local dx, dy = self:pointerDelta()

    local dragging = anyButton(PAN_BUTTONS)
    local started = dragging and not self.panDragging
    self.panDragging = dragging

    -- Zoom before panning, so a pan in the same frame moves by what the screen
    -- is about to show rather than by what it showed last frame.
    local exponent = Input.axis("zoomOut", "zoomIn") * r.zoomSpeed * dt
        + Input.scroll() * r.wheelStep
    if exponent ~= 0 then
        self:setZoom(r.height * math.exp(-exponent))
    end

    -- Ground axes, from the fixed yaw: what "up the screen" and "right across
    -- the screen" mean as directions on the map.
    local fx, fz = self:forwardFlat()
    local rx, rz = self:rightFlat()

    local moveX, moveZ = 0, 0

    local right = Input.axis("panLeft", "panRight")
    local forward = Input.axis("panBack", "panForward")
    if r.edgePan and not dragging then
        local pushX, pushZ = self:edgePush()
        right = clamp(right + pushX, -1, 1)
        forward = clamp(forward + pushZ, -1, 1)
    end

    if right ~= 0 or forward ~= 0 then
        -- Speed in screen-heights rather than world units: the map crosses the
        -- view at the same rate however far out the camera is zoomed, which is
        -- the only way panning stays usable at both ends of the zoom range.
        local speed = r.panSpeed * r.height * dt
        if Input.down("sprint") then
            speed = speed * r.boost
        end
        -- Normalised, or a diagonal pan would outrun a straight one.
        local length = math.sqrt(right * right + forward * forward)
        moveX = (rx * right + fx * forward) / length * speed
        moveZ = (rz * right + fz * forward) / length * speed
    end

    -- Drag-panning grabs the map: the ground under the pointer stays under the
    -- pointer. The frame a drag begins is skipped, so clicking after moving the
    -- mouse does not fling the view.
    if dragging and not started and (dx ~= 0 or dy ~= 0) then
        local perPixel = r.height / math.max(Engine.window.height(), 1)
        -- One pixel up the screen is more than one pixel of ground: the view is
        -- tilted, and dividing by the sine of the tilt is what undoes the
        -- foreshortening. Floored, so an almost-horizontal camera cannot ask
        -- for an infinite drag.
        local tilt = math.max(math.sin(math.rad(r.pitch)), 0.15)
        local screenRight = -dx * perPixel
        local screenForward = dy * perPixel / tilt

        moveX = moveX + rx * screenRight + fx * screenForward
        moveZ = moveZ + rz * screenRight + fz * screenForward
    end

    if moveX ~= 0 or moveZ ~= 0 then
        self:setFocus(r.focus.x + moveX, nil, r.focus.z + moveZ)
    end
    return self
end

-- Raw pointer movement since the last frame, in pixels. One frame is swallowed
-- after the cursor is captured or released: GLFW moves the pointer on the
-- switch, and that jump is not the player looking.
function Instance:pointerDelta()
    local x, y = Engine.input.mouse()
    local dx, dy = x - self.mouseX, y - self.mouseY
    self.mouseX, self.mouseY = x, y

    if self.lookWarm then
        self.lookWarm = false
        return 0, 0
    end
    return dx, dy
end

function Instance:mouseDelta()
    local dx, dy = self:pointerDelta()

    local dragging = Engine.input.mouseButton(1)
    local started = dragging and not self.dragging
    self.dragging = dragging

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

function Instance:update(dt)
    if self.mode == "rts" then
        return self:updateRts(dt)
    end

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
        local right = Input.axis("moveLeft", "moveRight")
        local up = Input.axis("moveDown", "moveUp")
        local forward = Input.axis("moveBack", "moveForward")
        if right ~= 0 or up ~= 0 or forward ~= 0 then
            local speed = self.moveSpeed * dt
            if Input.down("sprint") then
                speed = speed * self.boost
            end
            self.handle:moveLocal(right * speed, up * speed, forward * speed)
        end
        return
    end
    local yaw = Input.axis("orbitLeft", "orbitRight")
    local pitch = Input.axis("orbitDown", "orbitUp")

    yaw = yaw * self.orbitSpeed * dt
    pitch = pitch * self.orbitSpeed * dt

    if dragging then
        yaw = yaw + dx * self.mouseSpeed
        pitch = pitch - dy * self.mouseSpeed
    end

    if yaw ~= 0 or pitch ~= 0 then
        self.handle:orbit(self.pivot, yaw, pitch)
    end

    local zoom = Input.axis("zoomOut", "zoomIn")
    if zoom ~= 0 then
        local speed = self.zoomSpeed * dt
        if Input.down("sprint") then
            speed = speed * self.boost
        end
        self.handle:dolly(self.pivot, zoom * speed)
    end
end

return Camera
