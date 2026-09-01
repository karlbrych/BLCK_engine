-- gameplay/lib/devtools.lua
--
-- The things every scene wants while it is being built and none of which are
-- the scene's subject: a frame counter in the window title and a wireframe
-- toggle. Pulled out because they were half of scene:update() and none of its
-- meaning.

local Input = require("lib.input")

local Devtools = { library = true }

local title = "BLCK Engine"
local frames = 0
local elapsed = 0.0
local wireframe = false

function Devtools.init(windowTitle)
    title = windowTitle or title
    frames, elapsed = 0, 0.0
    wireframe = false
    Engine.debug.setWireframe(wireframe)
    if Engine.debug.enabled then
        Engine.log(("devtools: %s toggles wireframe (build with -DBLCK_DEBUG=OFF to compile this out)")
            :format(Input.keyFor("toggleWireframe")))
    end
    return Devtools
end

function Devtools.update(dt)
    if Engine.debug.enabled and Input.pressed("toggleWireframe") then
        wireframe = Engine.debug.toggleWireframe()
        Engine.log("devtools: wireframe -> " .. tostring(wireframe))
    end

    -- Once a second: a title rewritten every frame reports noise, not a rate.
    frames = frames + 1
    elapsed = elapsed + dt
    if elapsed >= 1.0 then
        local stats = Engine.renderer.stats()
        Engine.window.setTitle(string.format(
            "%s -- %.0f fps | %d draws | %d tris | %d shader/%d texture binds",
            title, frames / elapsed, stats.drawCalls, stats.triangles,
            stats.shaderBinds, stats.textureBinds))
        frames, elapsed = 0, 0.0
    end
    return Devtools
end

return Devtools
