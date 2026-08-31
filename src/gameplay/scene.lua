-- gameplay/scene.lua
--
-- The boot scene. It owns the camera, loads the map model and submits one draw
-- call per frame for everything it wants on screen. Nothing here touches GL --
-- Engine.renderer.submit only describes an object, and C++ decides how and when
-- to draw it.

local Camera = require("camera")
local Player = require("player")

local MAP_PATH = "assets/models/map/hradec_mapa.glb"

local scene = {}

function scene:init()
    Engine.renderer.clearColor(0.0, 0.0, 0.0)

    self.shader = Engine.shader.load("assets/shaders/basic.vert", "assets/shaders/basic.frag")

    -- recenter matters for scanned or geo-referenced data: this map's vertices
    -- sit several hundred units away from the origin, which makes every camera
    -- distance and clip plane below meaningless unless it is moved back.
    self.map = Engine.model.load(MAP_PATH, { recenter = true })
    self.player = Player.new("KajaBrych", 100)
    self.player:init()
    self.camera = Camera.new({ fov = 55, mode = "orbit" })
    self.camera:frame(self.map, { pitch = 25 })
    self.camera:activate()
    self.markerSize = self.map:radius() * 0.05
    self.markerHeight = select(2, self.map:size()) * 0.75 + self.markerSize

    self.elapsed = 0.0
    self.spinSpeed = 35.0
    self.frames = 0
    self.titleTimer = 0.0

    Engine.log(string.format("scene: %s -- %d parts, %d triangles, %d textures",
        MAP_PATH, self.map:partCount(), self.map:triangleCount(), self.map:textureCount()))
    Engine.log("scene: drag to orbit, Q/E zoom, F toggles fly (WASD + space/shift, ctrl boosts), Esc quits")
    -- Engine.log joins its arguments, it does not format them -- string.format
    -- is what turns the placeholders into the values.
    Engine.log(string.format("player: name=%s, health=%d",
        self.player.name, self.player.health))
end

function scene:update(dt)
    self.elapsed = self.elapsed + dt
    self.camera:update(dt)
    -- The player is an object the scene owns, so the scene is what drives it.
    -- Nothing runs player.lua on its own: it returns a class, not a scene.
    self.player:update(dt)

    -- Edge-triggered, so holding F down does not flip modes every frame.
    local flyDown = Engine.input.key("f")
    if flyDown and not self.flyWasDown then
        self.camera:setMode(self.camera.mode == "fly" and "orbit" or "fly")
        Engine.log("scene: camera mode -> " .. self.camera.mode)
    end
    self.flyWasDown = flyDown

    if Engine.input.key("escape") then
        Engine.window.close()
    end

    -- Frame stats in the title bar, refreshed once a second. Reading them here
    -- reports the frame that was just drawn, which is what draw() queued last.
    self.frames = self.frames + 1
    self.titleTimer = self.titleTimer + dt
    if self.titleTimer >= 1.0 then
        local stats = Engine.renderer.stats()
        Engine.window.setTitle(string.format(
            "Zero to Hero -- %.0f fps | %d draws | %d tris | %d shader/%d texture binds",
            self.frames / self.titleTimer, stats.drawCalls, stats.triangles,
            stats.shaderBinds, stats.textureBinds))
        self.frames = 0
        self.titleTimer = 0.0
    end
end

function scene:draw()
    -- One submit for the whole model: C++ expands it into a draw call per part,
    -- each with its own material and texture.
    Engine.renderer.submit({
        model = self.map,
        shader = self.shader,
        position = { 0, 0, 0 },
        scale = 1.0,
    })
    Engine.renderer.submit({
        mesh = self.player.mesh,
        shader = self.shader,
        position = { self.player.position.x, self.markerHeight, self.player.position.z },
        scale = self.markerSize,
    })
end

function scene:shutdown()
    self.player:shutdown()
    Engine.log("scene: shutdown")
end

return scene
