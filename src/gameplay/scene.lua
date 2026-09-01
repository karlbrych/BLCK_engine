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

    self.shader = Engine.shader.load(config.shader.vertex, config.shader.fragment)
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
        mode = "first",
        fov = config.camera.fov,
        near = config.camera.near * config.unitsPerMetre,
        far = extent * config.camera.farScale,
    })
    self.camera:attach(self.player, { eyeHeight = self.player.eyeHeight })
    self.camera:activate()

    Devtools.init(config.window.title)
    self:logControls()
end

function scene:logControls()
    Engine.log(string.format("scene: %s -- %d parts, %d triangles, %d textures",
        config.map.path, self.map:partCount(), self.map:triangleCount(),
        self.map:textureCount()))
    -- Built from the bindings rather than written out, so the help can never
    -- describe keys the game no longer answers to.
    Engine.log(string.format(
        "scene: %s%s%s%s walks, mouse looks, %s jumps, %s sprints, %s flies, %s overview, %s quits",
        Input.keyFor("moveForward"), Input.keyFor("moveLeft"), Input.keyFor("moveBack"),
        Input.keyFor("moveRight"), Input.keyFor("jump"), Input.keyFor("sprint"),
        Input.keyFor("toggleFly"), Input.keyFor("toggleView"), Input.keyFor("quit")))
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
    self.player:update(dt, self.camera)
    if self.player.position.y < self.fallLimit then
        self.player:spawn(self.spawnPoint.x, nil, self.spawnPoint.z)
        Engine.log("scene: fell off the map -- back to the spawn point")
    end
    self.camera:update(dt)

    -- Step out of the body to look at the map, and back in again.
    if Input.pressed("toggleView") then
        if self.camera.mode == "first" then
            self.camera:setMode("orbit")
            self.camera:frame(self.map, { pitch = config.camera.overviewPitch })
        else
            self.camera:setMode("first")
        end
        Engine.log("scene: camera mode -> " .. self.camera.mode)
    end

    Devtools.update(dt)

    if Input.pressed("quit") then
        Engine.window.close()
    end
end

function scene:draw() --vykresluje pomoci submit() objekty
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
