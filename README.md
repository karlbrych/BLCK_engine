# zero_to_hero

Generated with OpenGL Project Generator.

## Selected stack
- Windowing: GLFW
- GL loader: glad2
- Build system: CMake
- OpenGL: 4.6
- Scripting: Lua 5.4 via sol2, scripts in `src/gameplay/`
- Models: glTF 2.0 / GLB, loaded by a built-in reader (no Assimp needed)

## Optional libraries toggled
- GLM: required (the camera and renderer are built on it)
- Dear ImGui: yes
- stb_image: yes (a copy is vendored in `third_party/stb/`; it decodes the
  textures embedded in models)
- Assimp: yes
- fmt: yes
- spdlog: yes

## Build
### CMake
1. cmake --preset default
2. cmake --build --preset default

The executable ends up in build/bin, with `assets/` and `gameplay/` linked next
to it (copied on Windows) so edits to a shader, a model or a script take effect
without a rebuild.
Use the `release` preset instead of `default` for an optimised build.

### Meson
1. meson setup build
2. meson compile -C build

## Gameplay scripting

Lua scripts live in `src/gameplay/` and are linked next to the executable as
`gameplay/` at build time, so editing one hot-reloads in the running app -- no
rebuild, no restart.

A **script** returns a table with any of `init` / `update(dt)` / `draw` /
`shutdown`; `ScriptManager::loadAll()` picks up every such file automatically.
Hooks are called method-style, so `self` is the script's own table. Adding,
editing or deleting a file is noticed while the app runs -- a new scene starts
without a restart, a deleted one gets its `shutdown`.

A **library** stays available through `require("name")` and never joins the
frame loop. A file is a library when any of these is true:

- it returns something other than a table (a function, or nothing at all),
- it is a class -- `Player.__index = Player`. The Lua OOP idiom puts `init`,
  `update` and `draw` on the class too, but those are instance methods for a
  scene to call on objects made with `Player.new()`, not frame hooks; running
  them against the class itself would share one set of state between every
  instance,
- it sets `Module.library = true`, for anything the rules above do not cover.

Errors are caught and printed with a traceback, and the offending script is
disabled until its file changes -- a typo never takes the window down.

- `src/gameplay/camera.lua` -- orbit/fly controls around an `EngineCamera`, plus
  `camera:frame(model)`, which fits a loaded model to the window and sizes the
  clip planes and movement speeds to it
- `src/gameplay/player.lua` -- a player class the scene instantiates and drives
- `src/gameplay/scene.lua` -- the map model, the player and a camera: the scene
  the engine boots into

Drag to orbit, Q/E to zoom, F toggles fly mode (WASD, space/shift for up and
down, ctrl to move faster), Esc to quit.

### The Engine table

Scripts see one global, `Engine`. Nothing else reaches OpenGL.

| Call | Does |
| --- | --- |
| `Engine.mesh.cube/quad/triangle/sphere(...)` | builds a mesh |
| `Engine.model.load(path, {recenter=, scale=, textures=})` | loads a glTF/GLB model, cached by path |
| `Engine.texture.load(path, srgb)` | loads a standalone texture |
| `Engine.shader.load(vert, frag)` | loads and caches a shader |
| `Engine.camera.new{position=, target=, fov=, near=, far=}` | an `EngineCamera` |
| `Engine.renderer.setCamera(cam)` | the camera every shader gets fed |
| `Engine.renderer.submit{mesh=, shader=, position=, rotation=, scale=, color=, texture=, alpha=, doubleSided=}` | queues one draw call |
| `Engine.renderer.submit{model=, shader=, position=, ...}` | queues one call per model part, with its materials |
| `Engine.renderer.clearColor(r, g, b)` / `.stats()` | frame setup and counters |
| `Engine.input.key("w")` / `.mouse()` / `.mouseButton(1)` | input |
| `Engine.window.width/height/aspect/close/setTitle` | window |
| `Engine.time.now()` / `.delta()` | timing |
| `Engine.log(...)` | prints with a `[lua]` prefix |

Vectors are written the way that reads best: `{1, 2, 3}`, `{x=1, y=2, z=3}`, or
a bare number for the uniform case.

A model handle answers `partCount()`, `materialCount()`, `triangleCount()`,
`vertexCount()`, `textureCount()`, `radius()` and `center()` / `size()` /
`boundsMin()` / `boundsMax()`, which return three values each.

### Models

`Engine.model.load` reads glTF 2.0, both `.gltf` (with external or base64 `.bin`
and images) and `.glb`. It walks the node hierarchy, bakes each node's transform
into its part, and reads PBR base colour and base colour textures; images
embedded in the file are decoded through stb_image and uploaded as sRGB.

Not supported: skinning, morph targets, animation, sparse accessors, and
compressed extensions such as `KHR_draco_mesh_compression`. A file that *requires*
an extension is rejected with a message rather than loaded as garbage; anything
else it cannot use is warned about once and skipped.

`recenter = true` is worth reaching for on scanned or geo-referenced data --
`assets/models/map/hradec_mapa.glb` sits several hundred units from the origin,
which makes every camera distance meaningless until it is moved back.

Models and textures are cached in the renderer by path, so a script hot reload
re-uses what is already on the GPU instead of re-parsing a 50 MB file.

### How a frame runs

`Application::loop` clears the frame, calls every script's `update` then `draw`,
and flushes. `draw()` does not touch GL: it only calls `Engine.renderer.submit`,
which fills a `DrawCall` on the C++ side. `Renderer::flush` then sorts the queue
-- opaque before blended, then by shader program, then by texture -- uploads the
active camera's matrices once per program, and issues the draws. A script that
errors mid-frame leaves a half-built queue that is simply thrown away, not a
half-drawn frame.

## C++ layout

- `src/backend/Application.*` -- window, GL context, frame loop
- `src/backend/EngineCamera.*` -- view/projection matrices, orbit/fly primitives
- `src/backend/Renderer.*` -- draw-call queue, shader/model/texture caches, GL state
- `src/backend/Model.*` -- the glTF/GLB reader, including its own JSON parser
- `src/backend/ScriptManager.*` -- the Lua VM, the `Engine` table, hot reload
- `src/backend/Mesh.*`, `src/backend/Shader.*`, `src/backend/Texture.*` -- GL resource wrappers
- `src/gameplay/*.lua` -- the scripts themselves

## Requirements
- A C++ compiler and CMake 3.21 or newer.
- GLFW development files (see third_party/README.md for install commands).
- Python 3 on PATH plus the Jinja2 module (`python -m pip install --user jinja2`):
  the glad2 generator runs during the build.

Optional libraries are linked only if they are found; each one that is defines a
HAVE_* macro (HAVE_GLM, HAVE_FMT, ...) you can test in your code. A missing one
never breaks the build.
