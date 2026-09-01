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

config.shader = {
    vertex = "assets/shaders/basic.vert",
    fragment = "assets/shaders/basic.frag",
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
    overviewPitch = 25,
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

    -- The overview camera, which answers to the arrows or to WASD.
    orbitLeft  = { "left", "a" },
    orbitRight = { "right", "d" },
    orbitUp    = { "up", "w" },
    orbitDown  = { "down", "s" },
    zoomIn     = "e",
    zoomOut    = "q",
}

return config
