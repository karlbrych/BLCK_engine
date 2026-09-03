-- gameplay/config.lua
--
-- Every number worth tuning and every key worth rebinding, in one place. The
-- rest of the scripts read from here and hold no literals of their own, so
-- changing how the game feels never means going hunting through the logic.
--
-- Gameplay numbers are written in metres and seconds. The map is modelled in
-- metres and scaled up on load, so `unitsPerMetre` is the single conversion
-- between the two, applied once when an object is built.

local config = {}

config.unitsPerMetre = 10.0

config.window = {
    title = "BLCK Engine",
    clearColor = { 0.0, 0.0, 0.0 },
}

config.map = {
    path = "assets/models/map/hradec_mapa.glb",
    -- Scanned data sits thousands of units from the origin; this brings it back.
    recenter = true,
}
config.color = {
    main = {0.85,1.0,0.0}
}
-- One entry per shader the scene turns on. `fullscreen` shaders build their own
-- geometry from gl_VertexID and are submitted without a mesh.
config.shaders = {
    world = {
        vertex = "assets/shaders/basic.vert",
        fragment = "assets/shaders/basic.frag",
    },
    sky = {
        vertex = "assets/shaders/universal-fullscreen.vert",
        fragment = "assets/shaders/skybox.frag",
    },
}

config.player = {
    name = "KajaBrych",
    health = 100,

    height    = 1.8,   -- m, full body height
    eyeHeight = 1.62,  -- m, where the camera sits

    walkSpeed        = 4.0,  -- m/s
    sprintMultiplier = 3.0,
    gravity          = 9.81, -- m/s^2
    jumpHeight       = 0.6,  -- m, the apex of a standing jump
    -- Kerbs and stairs up to this high are walked over, anything taller is a
    -- wall. It is also how far the ground may drop away underfoot before the
    -- player stops being on it and starts falling.
    stepHeight    = 0.5,  -- m
    terminalSpeed = 55.0, -- m/s

    color = { 0.85, 0.35, 0.25 },
}

config.camera = {
    fov = 70,        -- wider than an overview: a first-person view wants periphery
    near = 0.18,     -- m -- close, but in proportion, or the depth buffer suffers
    farScale = 2.0,  -- multiples of the map's largest extent
}

-- The strategy view: orthographic, fixed angle, pans and zooms over the map and
-- does not turn. Lengths are fractions of the map's largest horizontal extent
-- rather than metres, so the same numbers frame a village and a continent.
config.rts = {
    yaw = 45,     -- degrees; the map's corner points at the viewer, as an RTS does
    pitch = 55,   -- degrees of downward tilt. Fixed: no rotation yet.

    height    = 0.30, -- of the map visible top to bottom at the starting zoom
    minHeight = 0.04, -- close enough to pick a building out
    maxHeight = 1.10, -- far enough to see the whole map with room around it

    panSpeed  = 0.9,  -- screen-heights per second, so it feels the same at any zoom
    boost     = 2.5,  -- sprint pans faster
    zoomSpeed = 2.0,  -- e-folds per second on the zoom keys
    wheelStep = 0.15, -- e-folds per wheel notch

    edgePan    = true,
    edgeMargin = 6,   -- px of window edge that the pointer pans from
    padding    = 0.15, -- of the map that the view may travel past its edge
}

-- Actions, not keys: the rest of the scripts ask for "jump", never for "space".
-- A value may be one key or a list of them, in which case any of them counts.
config.keys = {
    moveForward = "w",
    moveBack    = "s",
    moveLeft    = "a",
    moveRight   = "d",
    moveUp      = "space",
    moveDown    = "lctrl",
    sprint      = "lshift",
    jump        = "space",

    toggleFly       = "v",
    toggleView      = "f",
    toggleWireframe = "g",
    quit            = "escape",

    -- The strategy camera. Panning is its whole movement, so it takes both the
    -- arrows and WASD; nothing else is driving them while it is up.
    panLeft    = { "a", "left" },
    panRight   = { "d", "right" },
    panForward = { "w", "up" },
    panBack    = { "s", "down" },

    -- The overview camera, which answers to the arrows or to WASD.
    orbitLeft  = { "left", "a" },
    orbitRight = { "right", "d" },
    orbitUp    = { "up", "w" },
    orbitDown  = { "down", "s" },
    zoomIn     = "e",
    zoomOut    = "q",
}

return config
