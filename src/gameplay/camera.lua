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

    self.mode = options.mode or "orbit"
    self.orbitSpeed = options.orbitSpeed or 90
    self.mouseSpeed = options.mouseSpeed or 0.25
    self.moveSpeed = options.moveSpeed or 4
    self.zoomSpeed = options.zoomSpeed or 5
    self.boostKey = options.boostKey or "lctrl"
    self.boost = options.boost or 6

    self.mouseX, self.mouseY = Engine.input.mouse()
    self.dragging = false

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
