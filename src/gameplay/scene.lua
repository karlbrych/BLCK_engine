local Camera = require("camera")
local Player = require("player")

local MAP_PATH = "assets/models/map/hradec_mapa.glb"

--v metrech
local MAP_SCALE = 10.0
local PLAYER_HEIGHT = 1.8 

local scene = {}

function scene:init()
    Engine.renderer.clearColor(0.0, 0.0, 0.0)

    self.shader = Engine.shader.load("assets/shaders/basic.vert", "assets/shaders/basic.frag")
    self.map = Engine.model.load(MAP_PATH, { recenter = true, scale = MAP_SCALE })

    self.player = Player.new("KajaBrych", 100, PLAYER_HEIGHT * MAP_SCALE)
    self.player:init()
    self.groundY = select(2, self.map:boundsMax()) + self.player.size * 0.5

    self.camera = Camera.new({ fov = 55, mode = "orbit" })
    self.camera:frame(self.map, { pitch = 25 })
    self.camera:activate()
    Engine.debug.enabled = true
    self.elapsed = 0.0
    self.spinSpeed = 35.0
    self.frames = 0
    self.titleTimer = 0.0

    Engine.log(string.format("scene: %s -- %d parts, %d triangles, %d textures",
        MAP_PATH, self.map:partCount(), self.map:triangleCount(), self.map:textureCount()))
    Engine.log("scene: drag to orbit, Q/E zoom, F toggles fly (WASD + space/shift, ctrl boosts), Esc quits")
    self.wireframe = false
    Engine.debug.setWireframe(self.wireframe)
    if Engine.debug.enabled then
        Engine.log("scene: G toggles wireframe (build with -DBLCK_DEBUG=OFF to compile this out)")
    end
    Engine.log(string.format("player: name=%s, health=%d",
        self.player.name, self.player.health))
end

function scene:update(dt)
    self.elapsed = self.elapsed + dt
    self.camera:update(dt)
    self.player:update(dt)
    local flyDown = Engine.input.key("f")
    if flyDown and not self.flyWasDown then
        self.camera:setMode(self.camera.mode == "fly" and "orbit" or "fly")
        Engine.log("scene: camera mode -> " .. self.camera.mode)
    end
    self.flyWasDown = flyDown

    local wireDown = Engine.debug.enabled and Engine.input.key("g")
    if wireDown and not self.wireWasDown then
        self.wireframe = Engine.debug.toggleWireframe()
        Engine.log("scene: wireframe -> " .. tostring(self.wireframe))
    end
    self.wireWasDown = wireDown

    if Engine.input.key("escape") then
        Engine.window.close()
    end
    self.frames = self.frames + 1
    self.titleTimer = self.titleTimer + dt
    if self.titleTimer >= 1.0 then
        local stats = Engine.renderer.stats()
        Engine.window.setTitle(string.format(
            "BLCK Engine -- %.0f fps | %d draws | %d tris | %d shader/%d texture binds",
            self.frames / self.titleTimer, stats.drawCalls, stats.triangles,
            stats.shaderBinds, stats.textureBinds))
        self.frames = 0
        self.titleTimer = 0.0
    end
end

function scene:draw() --vykresluje pomoci submit() objekty  
    Engine.renderer.submit({
        model = self.map,
        shader = self.shader,
        position = { 0, 0, 0 },
    })
    Engine.renderer.submit({
        mesh = self.player.mesh,
        shader = self.shader,
        position = { self.player.position.x, self.groundY, self.player.position.z },
        scale = self.player.size,
    })
end

function scene:shutdown()
    self.player:shutdown()
    Engine.log("scene: shutdown")
end

return scene
