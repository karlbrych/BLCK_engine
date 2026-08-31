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

    -- No ground query yet, so the player stands on top of the map's bounding
    -- box rather than on the terrain under its feet.
    self.groundY = select(2, self.map:boundsMax())
    self.player:spawn(0, self.groundY, 0)

    -- A first-person view of a map this size needs clip planes to match: near
    -- in proportion to the body, far past the whole world, and not so far apart
    -- that the depth buffer gives up.
    local extent = math.max(self.map:size())
    self.camera = Camera.new({
        fov = 70, -- wider than the overview: a first-person view wants periphery
        mode = "first",
        near = self.player.size * 0.1,
        far = extent * 2.0,
    })
    self.camera:attach(self.player)
    self.camera:activate()
    self.elapsed = 0.0
    self.spinSpeed = 35.0
    self.frames = 0
    self.titleTimer = 0.0

    Engine.log(string.format("scene: %s -- %d parts, %d triangles, %d textures",
        MAP_PATH, self.map:partCount(), self.map:triangleCount(), self.map:textureCount()))
    Engine.log("scene: WASD walks, mouse looks, space/ctrl up and down, shift sprints, F for the overview, Esc quits")
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

    -- Player first, camera second. The camera plants itself on the player's
    -- position, so moving the body after the eye would leave the view a frame
    -- behind and the whole scene would swim.
    self.player:update(dt, self.camera)
    self.camera:update(dt)

    -- F steps out of the body to look at the map, and back in again.
    local viewDown = Engine.input.key("f")
    if viewDown and not self.viewWasDown then
        if self.camera.mode == "first" then
            self.camera:setMode("orbit")
            self.camera:frame(self.map, { pitch = 25 })
        else
            self.camera:setMode("first")
        end
        Engine.log("scene: camera mode -> " .. self.camera.mode)
    end
    self.viewWasDown = viewDown

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
    -- The player draws itself, and knows to draw nothing when the camera is
    -- looking out of its eyes.
    self.player:draw(self.shader)
end

function scene:shutdown()
    self.player:shutdown()
    Engine.log("scene: shutdown")
end

return scene
