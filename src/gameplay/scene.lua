-- gameplay/scene.lua
--
-- The scene is the composition root: it loads the world, builds the things in
-- it, wires them to each other, and then does very little every frame. Anything
-- with rules of its own belongs in a module -- the body in entities/player.lua,
-- the controls in lib/camera.lua, the numbers in config.lua -- so that what is
-- left here reads as the shape of the game rather than its details.

local config = require("config")
local Input = require("lib.input")
local Camera = require("lib.camera")
local Devtools = require("lib.devtools")
local Player = require("entities.player")

local scene = {}

function scene:init()
    Input.bind(config.keys)
    Engine.renderer.clearColor(table.unpack(config.window.clearColor))

    self.shader = Engine.shader.load(config.shaders.world.vertex, config.shaders.world.fragment)
    -- The sky is a fullscreen pass: no geometry, no model matrix. It rebuilds a
    -- view ray per pixel from the camera's inverse matrices, which the renderer
    -- hands to any shader declaring invView/invProjection.
    self.skyShader = Engine.shader.load(config.shaders.sky.vertex, config.shaders.sky.fragment)
    self.map = Engine.model.load(config.map.path, {
        recenter = config.map.recenter,
        scale = config.unitsPerMetre,
    })

    self.player = Player.new(config.player, config.unitsPerMetre)

    -- The player stands on the map itself: one downward ray per frame against
    -- the model's collision triangles. Without them there is nothing to fall
    -- onto, so the player flies instead of walking.
    if self.map:hasCollision() then
        self.player:setGround(function(x, z, fromY)
            return self.map:groundHeight(x, z, fromY)
        end)
    else
        Engine.log("scene: map has no collision geometry -- the player flies instead of walking")
    end

    -- No y: spawn() drops the player onto whatever is under the spawn point.
    self.spawnPoint = { x = 0, z = 0 }
    self.player:spawn(self.spawnPoint.x, nil, self.spawnPoint.z)

    -- Walk off the edge and gravity never stops, so well below the lowest
    -- triangle is taken as having left the map.
    local _, mapBottom = self.map:boundsMin()
    self.fallLimit = mapBottom - self.player.size * 20

    local extent = math.max(self.map:size())
    self.camera = Camera.new({
        fov = config.camera.fov,
        near = config.camera.near * config.unitsPerMetre,
        far = extent * config.camera.farScale,
    })
    -- Attached even though the strategy view does not use it: the attachment is
    -- what makes stepping into first-person a keypress rather than a rebuild.
    self.camera:attach(self.player, { eyeHeight = self.player.eyeHeight })
    self:overhead()
    self.camera:activate()

    Devtools.init(config.window.title)
    self:logControls()
end

-- Puts the camera over the map: centred on it, held inside its edges, and
-- zoomed by the map's own size rather than by a number that would only suit
-- this one. The ground extent, not the height, is what the view is measured
-- against -- a tall building on a small map should not zoom the camera out.
function scene:overhead()
    local sizeX, _, sizeZ = self.map:size()
    local minX, _, minZ = self.map:boundsMin()
    local maxX, _, maxZ = self.map:boundsMax()
    local centerX, centerY, centerZ = self.map:center()

    local ground = self.map:hasCollision() and self.map:groundHeight(centerX, centerZ) or centerY
    local extent = math.max(sizeX, sizeZ)
    local rts = config.rts

    self.camera:rts({
        center = { centerX, ground, centerZ },
        bounds = { minX, minZ, maxX, maxZ },
        extent = extent,

        yaw = rts.yaw,
        pitch = rts.pitch,

        height = extent * rts.height,
        minHeight = extent * rts.minHeight,
        maxHeight = extent * rts.maxHeight,

        panSpeed = rts.panSpeed,
        boost = rts.boost,
        zoomSpeed = rts.zoomSpeed,
        wheelStep = rts.wheelStep,

        edgePan = rts.edgePan,
        edgeMargin = rts.edgeMargin,
        padding = rts.padding,
    })
    return self
end

function scene:logControls()
    Engine.log(string.format("scene: %s -- %d parts, %d triangles, %d textures",
        config.map.path, self.map:partCount(), self.map:triangleCount(),
        self.map:textureCount()))
    -- Built from the bindings rather than written out, so the help can never
    -- describe keys the game no longer answers to.
    Engine.log(string.format(
        "scene: %s%s%s%s or the screen edge pans, wheel or %s/%s zooms, right-drag grabs the map",
        Input.keyFor("panForward"), Input.keyFor("panLeft"), Input.keyFor("panBack"),
        Input.keyFor("panRight"), Input.keyFor("zoomIn"), Input.keyFor("zoomOut")))
    Engine.log(string.format(
        "scene: %s drops into the body (mouse looks, %s jumps, %s sprints, %s flies), %s quits",
        Input.keyFor("toggleView"), Input.keyFor("jump"), Input.keyFor("sprint"),
        Input.keyFor("toggleFly"), Input.keyFor("quit")))
    Engine.log(string.format("player: name=%s, health=%d, standing at y=%.2f on %d collision triangles",
        self.player.name, self.player.health, self.player.position.y,
        self.map:collisionTriangleCount()))
end

function scene:update(dt)
    -- First, so every script below reads one consistent snapshot of the frame.
    Input.update()

    -- Player before camera: the camera plants itself on the player's position,
    -- so moving the body after the eye would leave the view a frame behind and
    -- the whole scene would swim.
    --
    -- Only inside the body do the movement keys belong to it -- from overhead
    -- they pan the camera, and driving both with one W would walk the player
    -- off across the map every time the view moved. Gravity is not input, so it
    -- keeps running either way and the body stays on the ground it is standing
    -- on.
    if self.camera.mode == "first" then
        self.player:update(dt, self.camera)
    elseif self.map:hasCollision() then
        self.player:fall(dt)
    end
    if self.player.position.y < self.fallLimit then
        self.player:spawn(self.spawnPoint.x, nil, self.spawnPoint.z)
        Engine.log("scene: fell off the map -- back to the spawn point")
    end
    self.camera:update(dt)

    -- Step into the body to look around from inside it, and back out to the
    -- map. The strategy view keeps where it was looking, so coming back out
    -- lands where you left rather than at the middle of the map again.
    if Input.pressed("toggleView") then
        self.camera:setMode(self.camera.mode == "first" and "rts" or "first")
        Engine.log("scene: camera mode -> " .. self.camera.mode)
    end

    Devtools.update(dt)

    if Input.pressed("quit") then
        Engine.window.close()
    end
end

function scene:draw() --vykresluje pomoci submit() objekty
    -- Submitted first for readability only: the background pass is drawn before
    -- the world whatever order the calls arrive in.
    Engine.renderer.submit({
        shader = self.skyShader,
        fullscreen = true,
    })

    Engine.renderer.submit({
        model = self.map,
        shader = self.shader,
        -- At the origin, and it has to stay there: the ground query reads the
        -- model's own coordinates, so a map drawn anywhere else would be walked
        -- on where it is not drawn.
        position = { 0, 0, 0 },
    })
    -- The player draws itself, and knows to draw nothing when the camera is
    -- looking out of its eyes.
    self.player:draw(self.shader)
end

function scene:shutdown()
    self.player:shutdown()
    Engine.log("scene: shutdown")
end

return scene
