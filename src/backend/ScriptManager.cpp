#include <ScriptManager.h>

#include <EngineCamera.h>
#include <Mesh.h>
#include <Model.h>
#include <Renderer.h>
#include <Shader.h>
#include <Texture.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>

#include <GLFW/glfw3.h>

namespace fs = std::filesystem;

namespace
{

// ------------------------------------------------------------ table readers
//
// Scripts write vectors the way that reads best at the call site, so all three
// spellings are accepted: {1, 2, 3}, {x = 1, y = 2, z = 3}, and a bare number
// for the uniform case (mostly scale).

float readField(const sol::table& table, int index, const char* name, float fallback)
{
    if (sol::object value = table[index]; value.is<float>())
    {
        return value.as<float>();
    }
    if (sol::object value = table[name]; value.is<float>())
    {
        return value.as<float>();
    }
    return fallback;
}

glm::vec3 toVec3(const sol::object& value, const glm::vec3& fallback, const char* first = "x",
                 const char* second = "y", const char* third = "z")
{
    if (value.is<float>())
    {
        return glm::vec3(value.as<float>());
    }
    if (!value.is<sol::table>())
    {
        return fallback;
    }

    const sol::table table = value.as<sol::table>();
    return glm::vec3(readField(table, 1, first, fallback.x),
                     readField(table, 2, second, fallback.y),
                     readField(table, 3, third, fallback.z));
}

glm::vec3 readVec3(const sol::table& source, const char* key, const glm::vec3& fallback)
{
    return toVec3(source[key], fallback);
}

GLenum toDrawMode(const sol::object& value)
{
    if (!value.is<std::string>())
    {
        return GL_TRIANGLES;
    }

    const std::string name = value.as<std::string>();
    if (name == "lines")
    {
        return GL_LINES;
    }
    if (name == "line_strip")
    {
        return GL_LINE_STRIP;
    }
    if (name == "line_loop")
    {
        return GL_LINE_LOOP;
    }
    if (name == "points")
    {
        return GL_POINTS;
    }
    if (name == "triangle_strip")
    {
        return GL_TRIANGLE_STRIP;
    }
    return GL_TRIANGLES;
}

// ------------------------------------------------------------ input mapping

// Scripts name keys ("w", "space", "left"); GLFW wants an int. Letters and
// digits map arithmetically because GLFW's codes are ASCII for those ranges.
int keyFromName(const std::string& name)
{
    if (name.size() == 1)
    {
        const unsigned char c = static_cast<unsigned char>(name[0]);
        if (c >= 'a' && c <= 'z')
        {
            return GLFW_KEY_A + (c - 'a');
        }
        if (c >= 'A' && c <= 'Z')
        {
            return GLFW_KEY_A + (c - 'A');
        }
        if (c >= '0' && c <= '9')
        {
            return GLFW_KEY_0 + (c - '0');
        }
    }

    static const std::pair<const char*, int> named[] = {
        {"space", GLFW_KEY_SPACE},
        {"escape", GLFW_KEY_ESCAPE},
        {"enter", GLFW_KEY_ENTER},
        {"tab", GLFW_KEY_TAB},
        {"backspace", GLFW_KEY_BACKSPACE},
        {"left", GLFW_KEY_LEFT},
        {"right", GLFW_KEY_RIGHT},
        {"up", GLFW_KEY_UP},
        {"down", GLFW_KEY_DOWN},
        {"lshift", GLFW_KEY_LEFT_SHIFT},
        {"rshift", GLFW_KEY_RIGHT_SHIFT},
        {"lctrl", GLFW_KEY_LEFT_CONTROL},
        {"rctrl", GLFW_KEY_RIGHT_CONTROL},
        {"lalt", GLFW_KEY_LEFT_ALT},
        {"ralt", GLFW_KEY_RIGHT_ALT},
        {"f1", GLFW_KEY_F1},
        {"f2", GLFW_KEY_F2},
        {"f3", GLFW_KEY_F3},
        {"f4", GLFW_KEY_F4},
        {"f5", GLFW_KEY_F5},
    };

    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [key, code] : named)
    {
        if (lowered == key)
        {
            return code;
        }
    }
    return GLFW_KEY_UNKNOWN;
}

fs::file_time_type stampOf(const fs::path& path)
{
    std::error_code error;
    const fs::file_time_type stamp = fs::last_write_time(path, error);
    return error ? fs::file_time_type{} : stamp;
}

} // namespace

// ---------------------------------------------------------------- lifecycle

ScriptManager::ScriptManager(fs::path root) : scriptRoot(std::move(root)) {}

ScriptManager::~ScriptManager()
{
    // shutdown() is the supported path; this only catches the case where an
    // exception unwound past it, and the VM must not outlive the manager.
    scripts.clear();
    lua.reset();
}

fs::path ScriptManager::pathFor(const std::string& name) const
{
    return scriptRoot / (name + ".lua");
}

ScriptManager::Script* ScriptManager::find(const std::string& name)
{
    const auto it = std::find_if(scripts.begin(), scripts.end(),
                                 [&](const Script& script) { return script.name == name; });
    return it == scripts.end() ? nullptr : &*it;
}

bool ScriptManager::isFrameScript(const sol::table& module)
{
    // Opted out by hand. Checked first so it can override everything below.
    if (sol::object library = module["library"]; library.is<bool>() && library.as<bool>())
    {
        return false;
    }

    // `Player.__index = Player` is how a Lua class says it is a class. Its
    // init/update/draw are the *instance* methods a scene calls on objects it
    // made with Player.new(), not hooks for the frame loop to run against the
    // class table itself -- doing that would share one set of state between
    // every instance, which is never what the author meant.
    //
    // Compared by identity rather than with ==: sol's equality pushes both
    // sides onto the Lua stack, and a raw_get_or fallback carries no state to
    // push onto.
    const sol::object index = module.raw_get<sol::object>("__index");
    if (index.is<sol::table>() && index.pointer() == module.pointer())
    {
        return false;
    }

    for (const char* hook : {"init", "update", "draw", "shutdown"})
    {
        if (module[hook].get_type() == sol::type::function)
        {
            return true;
        }
    }
    return false;
}

void ScriptManager::bind(Renderer& activeRenderer, GLFWwindow* activeWindow)
{
    renderer = &activeRenderer;
    window = activeWindow;

    lua = std::make_unique<sol::state>();
    lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::table,
                        sol::lib::math, sol::lib::os, sol::lib::coroutine, sol::lib::debug);

    // require() resolves against gameplay/ so scripts can pull each other in by
    // bare name -- require("camera"), not a path relative to the working dir.
    const std::string root = scriptRoot.generic_string();
    (*lua)["package"]["path"] = root + "/?.lua;" + root + "/?/init.lua";

    installEngineTable(activeRenderer, activeWindow);
}

bool ScriptManager::loadInto(Script& script, bool requested)
{
    const fs::path path = script.path;
    if (!fs::exists(path))
    {
        std::cerr << "ScriptManager: '" << path.string() << "' does not exist\n";
        return false;
    }

    sol::protected_function_result result =
        lua->safe_script_file(path.string(), sol::script_pass_on_error);
    if (!result.valid())
    {
        const sol::error error = result;
        std::cerr << "ScriptManager: failed to load '" << path.string() << "':\n  " << error.what()
                  << '\n';
        return false;
    }

    if (result.get_type() != sol::type::table)
    {
        // A helper that returns a function, or nothing at all, is a perfectly
        // good library -- only complain when this file was asked for by name.
        if (requested)
        {
            std::cerr << "ScriptManager: '" << path.string()
                      << "' must return a table of hooks (init/update/draw/shutdown)\n";
        }
        return false;
    }

    script.module = result;
    script.stamp = stampOf(path);
    script.started = false;
    script.failed = false;
    return true;
}

bool ScriptManager::load(const std::string& name)
{
    if (!lua)
    {
        throw std::runtime_error("ScriptManager: bind() must run before load()");
    }

    Script fresh;
    fresh.name = name;
    fresh.path = pathFor(name);
    if (!loadInto(fresh, true))
    {
        return false;
    }

    if (Script* existing = find(name))
    {
        // Replacing in place keeps the update/draw order stable across reloads.
        *existing = std::move(fresh);
    }
    else
    {
        scripts.push_back(std::move(fresh));
    }

    rescanWatchList();
    return true;
}

std::size_t ScriptManager::loadAll()
{
    if (!fs::is_directory(scriptRoot))
    {
        std::cerr << "ScriptManager: script directory '" << scriptRoot.string()
                  << "' not found -- no gameplay scripts loaded\n";
        return 0;
    }

    // Alphabetical, so the hook order does not depend on the filesystem.
    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(scriptRoot))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".lua")
        {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    std::size_t loaded = 0;
    for (const fs::path& file : files)
    {
        const std::string name = file.stem().string();

        Script candidate;
        candidate.name = name;
        candidate.path = file;
        if (!loadInto(candidate, false))
        {
            continue;
        }

        // A library like camera.lua or player.lua returns a class, not a scene.
        // It stays available through require() but never joins the frame loop.
        if (!isFrameScript(candidate.module))
        {
            continue;
        }

        if (Script* existing = find(name))
        {
            *existing = std::move(candidate);
        }
        else
        {
            scripts.push_back(std::move(candidate));
        }
        ++loaded;
    }

    rescanWatchList();
    return loaded;
}

// ---------------------------------------------------------------- hooks

void ScriptManager::call(Script& script, const char* hook, sol::object argument)
{
    if (script.failed || !script.module.valid())
    {
        return;
    }

    sol::object entry = script.module[hook];
    if (entry.get_type() != sol::type::function)
    {
        return;
    }

    sol::protected_function function = entry;
    function.set_error_handler(sol::reference((*lua)["debug"]["traceback"]));

    // Hooks are called method-style, so a script can keep its state on self.
    const sol::protected_function_result result =
        argument.valid() ? function(script.module, argument) : function(script.module);
    if (!result.valid())
    {
        const sol::error error = result;
        std::cerr << "ScriptManager: " << script.name << '.' << hook << "() failed:\n  "
                  << error.what() << "\n  (script disabled until the file changes)\n";
        script.failed = true;
        if (renderer)
        {
            // The half-built frame this script was assembling is meaningless now.
            renderer->discard();
        }
    }
}

void ScriptManager::start()
{
    for (Script& script : scripts)
    {
        if (!script.started && !script.failed)
        {
            script.started = true;
            call(script, "init");
        }
    }
}

void ScriptManager::update(double deltaSeconds)
{
    delta = deltaSeconds;
    start(); // a script loaded mid-run gets its init before its first update

    for (Script& script : scripts)
    {
        call(script, "update", sol::make_object(*lua, deltaSeconds));
    }
}

void ScriptManager::draw()
{
    for (Script& script : scripts)
    {
        call(script, "draw");
    }
}

// ---------------------------------------------------------------- reloading

std::vector<std::filesystem::path> ScriptManager::scriptFiles() const
{
    std::vector<fs::path> files;
    if (!fs::is_directory(scriptRoot))
    {
        return files;
    }

    std::error_code error;
    for (fs::recursive_directory_iterator it(scriptRoot, error), end; it != end;
         it.increment(error))
    {
        if (error)
        {
            break;
        }
        if (it->is_regular_file() && it->path().extension() == ".lua")
        {
            files.push_back(it->path());
        }
    }

    // Sorted so the comparison in reloadChanged() is a plain element-wise walk
    // and does not depend on the order the filesystem hands entries back.
    std::sort(files.begin(), files.end());
    return files;
}

void ScriptManager::rescanWatchList()
{
    watched.clear();
    for (const fs::path& file : scriptFiles())
    {
        watched.emplace_back(file, stampOf(file));
    }
}

std::size_t ScriptManager::reloadChanged()
{
    if (!lua)
    {
        return 0;
    }

    // The set of files matters as much as their timestamps: a script dropped
    // into the directory has no watch entry yet, so mtimes alone would never
    // notice it and it would need a restart to run.
    const std::vector<fs::path> current = scriptFiles();
    bool changed = current.size() != watched.size();
    for (std::size_t i = 0; !changed && i < current.size(); ++i)
    {
        changed = current[i] != watched[i].first || stampOf(current[i]) != watched[i].second;
    }
    if (!changed)
    {
        return 0;
    }

    const std::size_t before = scripts.size();
    reloadAll();
    std::cout << "ScriptManager: reloaded " << scripts.size() << " script(s)"
              << (scripts.size() != before ? " (set changed)" : "") << '\n';
    return scripts.size();
}

void ScriptManager::reloadAll()
{
    if (!lua)
    {
        return;
    }

    // Modules cache themselves in package.loaded, so an edited camera.lua would
    // otherwise keep serving the version require() saw first.
    sol::table loaded = (*lua)["package"]["loaded"];
    for (const auto& [path, stamp] : watched)
    {
        (void)stamp;
        loaded[path.stem().string()] = sol::lua_nil;
    }

    // A script that is already disabled gets no shutdown: whatever broke it
    // would most likely break here too, on the way out. call() checks that.
    for (Script& script : scripts)
    {
        call(script, "shutdown");
    }

    // Rescanning rather than reloading in place, so a file added, deleted, or
    // turned from a library into a scene since the last pass is picked up
    // without a restart -- which is the whole point of hot reload.
    scripts.clear();
    loadAll();
    start();
}

void ScriptManager::shutdown()
{
    for (Script& script : scripts)
    {
        call(script, "shutdown");
    }
    scripts.clear();
    watched.clear();
    // Drops every mesh and shader the scripts made, while the context is current.
    lua.reset();
    renderer = nullptr;
    window = nullptr;
}

// ---------------------------------------------------------------- Engine API
//
// Everything a gameplay script is allowed to touch lives on one global table,
// so a script never sees a raw GL handle and the C++ side keeps one obvious
// place to look when the surface has to grow.

void ScriptManager::installEngineTable(Renderer& activeRenderer, GLFWwindow* activeWindow)
{
    sol::state& L = *lua;
    Renderer* r = &activeRenderer;
    GLFWwindow* w = activeWindow;

    // ---- usertypes -------------------------------------------------------
    // Vectors cross the boundary as plain numbers or tables rather than as a
    // bound glm type: scripts get to write {0, 1, 0} and stay idiomatic Lua.

    L.new_usertype<Mesh>("Mesh", sol::no_constructor,       //
                         "valid", &Mesh::valid,             //
                         "vertexCount", &Mesh::vertexCount, //
                         "indexCount", &Mesh::indexCount,   //
                         "indexed", &Mesh::indexed);

    L.new_usertype<Texture>("Texture", sol::no_constructor, //
                            "valid", &Texture::valid,       //
                            "width", &Texture::getWidth,    //
                            "height", &Texture::getHeight,  //
                            "channels", &Texture::getChannels);

    // Vectors come back as three values here too, so a script can write
    //   local x, y, z = model:center()
    L.new_usertype<Model>(
        "Model", sol::no_constructor, //
        "partCount", [](const Model& self) { return self.parts().size(); }, "materialCount",
        [](const Model& self) { return self.materials().size(); }, "triangleCount",
        &Model::triangleCount, "vertexCount", &Model::vertexCount, "textureCount",
        &Model::textureCount, "source", &Model::source, "radius", &Model::radius, "center",
        [](const Model& self)
        {
            const glm::vec3 v = self.center();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "size",
        [](const Model& self)
        {
            const glm::vec3 v = self.size();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "boundsMin",
        [](const Model& self)
        {
            const glm::vec3& v = self.boundsMin();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "boundsMax",
        [](const Model& self)
        {
            const glm::vec3& v = self.boundsMax();
            return std::make_tuple(v.x, v.y, v.z);
        });

    L.new_usertype<Shader>(
        "Shader", sol::no_constructor, //
        "valid", &Shader::valid,       //
        "reload", &Shader::reload,     //
        "setFloat",
        [](Shader& self, const std::string& name, float value) { self.set(name, value); }, "setInt",
        [](Shader& self, const std::string& name, int value) { self.set(name, value); }, "setVec3",
        [](Shader& self, const std::string& name, sol::object value)
        { self.set(name, toVec3(value, glm::vec3(0.0f))); });

    L.new_usertype<EngineCamera>(
        "EngineCamera", sol::no_constructor,

        // -- placement
        "setPosition",
        [](EngineCamera& self, sol::object x, sol::optional<float> y, sol::optional<float> z)
        {
            self.setPosition(y && z ? glm::vec3(x.as<float>(), *y, *z)
                                    : toVec3(x, self.getPosition()));
        },
        "move", [](EngineCamera& self, float x, float y, float z)
        { self.move(glm::vec3(x, y, z)); }, "moveLocal", &EngineCamera::moveLocal,

        // -- orientation
        "setRotation", [](EngineCamera& self, float yaw, float pitch, sol::optional<float> roll)
        { self.setRotation(yaw, pitch, roll.value_or(self.getRoll())); }, "rotate",
        [](EngineCamera& self, float yaw, float pitch, sol::optional<float> roll)
        { self.rotate(yaw, pitch, roll.value_or(0.0f)); }, "lookAt",
        [](EngineCamera& self, sol::object x, sol::optional<float> y, sol::optional<float> z)
        { self.lookAt(y && z ? glm::vec3(x.as<float>(), *y, *z) : toVec3(x, glm::vec3(0.0f))); },
        "orbit", [](EngineCamera& self, sol::object pivot, float deltaYaw, float deltaPitch)
        { self.orbit(toVec3(pivot, glm::vec3(0.0f)), deltaYaw, deltaPitch); }, "dolly",
        [](EngineCamera& self, sol::object pivot, float amount)
        { self.dolly(toVec3(pivot, glm::vec3(0.0f)), amount); }, "setPitchLimit",
        &EngineCamera::setPitchLimit,

        // -- projection
        "setPerspective", &EngineCamera::setPerspective,   //
        "setOrthographic", &EngineCamera::setOrthographic, //
        "setFov", &EngineCamera::setFov,                   //
        "setOrthoHeight", &EngineCamera::setOrthoHeight,   //
        "setAspect", &EngineCamera::setAspect,             //
        "setViewport", &EngineCamera::setViewport,         //
        "setClipPlanes", &EngineCamera::setClipPlanes,     //
        "setAutoAspect", &EngineCamera::setAutoAspect,

        // -- queries; vectors come back as three values, so a script can write
        //    local x, y, z = camera:getPosition()
        "getPosition",
        [](const EngineCamera& self)
        {
            const glm::vec3& p = self.getPosition();
            return std::make_tuple(p.x, p.y, p.z);
        },
        "forward",
        [](const EngineCamera& self)
        {
            const glm::vec3 v = self.forward();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "right",
        [](const EngineCamera& self)
        {
            const glm::vec3 v = self.right();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "up",
        [](const EngineCamera& self)
        {
            const glm::vec3 v = self.up();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "getYaw", &EngineCamera::getYaw,       //
        "getPitch", &EngineCamera::getPitch,   //
        "getRoll", &EngineCamera::getRoll,     //
        "getFov", &EngineCamera::getFov,       //
        "getAspect", &EngineCamera::getAspect, //
        "getNear", &EngineCamera::getNear,     //
        "getFar", &EngineCamera::getFar);

    sol::table engine = L.create_named_table("Engine");

    engine["log"] = [&L](sol::variadic_args args)
    {
        std::string line;
        const sol::function tostring = L["tostring"];
        for (auto argument : args)
        {
            if (!line.empty())
            {
                line += '\t';
            }
            line += tostring(argument).get<std::string>();
        }
        std::cout << "[lua] " << line << '\n';
    };

    // ---- Engine.camera ---------------------------------------------------
    sol::table camera = engine.create_named("camera");
    camera["new"] = [r](sol::optional<sol::table> options)
    {
        auto instance = std::make_shared<EngineCamera>();

        const glm::vec3 position = options
                                       ? readVec3(*options, "position", glm::vec3(0.0f, 0.0f, 3.0f))
                                       : glm::vec3(0.0f, 0.0f, 3.0f);
        instance->setPosition(position);

        const float aspect = r->aspect();
        float fov = 60.0f;
        float nearZ = 0.1f;
        float farZ = 500.0f;
        if (options)
        {
            fov = (*options)["fov"].get_or(fov);
            nearZ = (*options)["near"].get_or(nearZ);
            farZ = (*options)["far"].get_or(farZ);
        }
        instance->setPerspective(fov, aspect, nearZ, farZ);

        if (options)
        {
            // target wins over yaw/pitch: it is the spelling a scene reaches for.
            if (sol::object target = (*options)["target"]; target.valid())
            {
                instance->lookAt(toVec3(target, glm::vec3(0.0f)));
            }
            else if (sol::object yaw = (*options)["yaw"]; yaw.valid())
            {
                instance->setRotation(yaw.as<float>(), (*options)["pitch"].get_or(0.0f));
            }
        }
        return instance;
    };

    // ---- Engine.mesh -----------------------------------------------------
    sol::table mesh = engine.create_named("mesh");
    mesh["cube"] = [](sol::optional<float> size)
    { return std::make_shared<Mesh>(Mesh::cube(size.value_or(1.0f))); };
    mesh["quad"] = [](sol::optional<float> width, sol::optional<float> height)
    {
        const float w = width.value_or(1.0f);
        return std::make_shared<Mesh>(Mesh::quad(w, height.value_or(w)));
    };
    mesh["triangle"] = [](sol::optional<float> size)
    { return std::make_shared<Mesh>(Mesh::triangle(size.value_or(1.0f))); };
    mesh["sphere"] = [](sol::optional<float> radius, sol::optional<unsigned int> rings,
                        sol::optional<unsigned int> segments)
    {
        return std::make_shared<Mesh>(
            Mesh::sphere(radius.value_or(0.5f), rings.value_or(16), segments.value_or(32)));
    };

    // ---- Engine.model ----------------------------------------------------
    // Loading is synchronous and can take a moment on a large map, which is why
    // it belongs in a script's init() rather than in update(). The renderer
    // caches by path, so a hot reload re-uses what is already on the GPU.
    sol::table model = engine.create_named("model");
    model["load"] = [r](const std::string& path, sol::optional<sol::table> options)
    {
        Model::Options loadOptions;
        if (options)
        {
            loadOptions.recenter = (*options)["recenter"].get_or(loadOptions.recenter);
            loadOptions.scale = (*options)["scale"].get_or(loadOptions.scale);
            loadOptions.generateNormals =
                (*options)["generateNormals"].get_or(loadOptions.generateNormals);
            loadOptions.loadTextures = (*options)["textures"].get_or(loadOptions.loadTextures);
            loadOptions.verbose = (*options)["verbose"].get_or(loadOptions.verbose);
        }
        return r->loadModel(path, loadOptions);
    };

    // ---- Engine.texture --------------------------------------------------
    sol::table texture = engine.create_named("texture");
    texture["load"] = [r](const std::string& path, sol::optional<bool> srgb)
    { return r->loadTexture(path, srgb.value_or(true)); };

    // ---- Engine.shader ---------------------------------------------------
    sol::table shader = engine.create_named("shader");
    shader["load"] = [r](const std::string& vertex, const std::string& fragment)
    { return r->loadShader(vertex, fragment); };
    shader["reloadAll"] = [r]() { return r->reloadShaders(); };

    // ---- Engine.renderer -------------------------------------------------
    sol::table rendererTable = engine.create_named("renderer");
    rendererTable["setCamera"] = [r](std::shared_ptr<EngineCamera> value)
    { r->setCamera(std::move(value)); };
    rendererTable["clearColor"] = [r](float red, float green, float blue, sol::optional<float> a)
    { r->setClearColor(red, green, blue, a.value_or(1.0f)); };
    // The draw call itself: scripts describe one object, C++ queues it and draws
    // the whole frame once every script has had its say.
    rendererTable["submit"] = [r](sol::table call)
    {
        DrawCall out;
        // Reported as a Lua error rather than quietly dropped: a submit() with a
        // typo in it is a bug in the scene, and silence would hide it.
        sol::optional<std::shared_ptr<Model>> model = call["model"];
        sol::optional<std::shared_ptr<Mesh>> mesh = call["mesh"];
        sol::optional<std::shared_ptr<Shader>> shader = call["shader"];
        if ((!model || !*model) && (!mesh || !*mesh))
        {
            throw std::runtime_error("Engine.renderer.submit: needs a 'mesh' or a 'model'");
        }
        if (!shader || !*shader)
        {
            throw std::runtime_error("Engine.renderer.submit: 'shader' must be a shader");
        }
        out.shader = *shader;
        out.position = readVec3(call, "position", glm::vec3(0.0f));
        out.rotation = readVec3(call, "rotation", glm::vec3(0.0f));
        out.scale = toVec3(call["scale"], glm::vec3(1.0f));
        out.mode = toDrawMode(call["mode"]);
        out.instances = call["instances"].get_or(1);
        out.alpha = call["alpha"].get_or(1.0f);
        out.doubleSided = call["doubleSided"].get_or(false);
        out.blend = call["blend"].get_or(false) || out.alpha < 1.0f;
        if (sol::optional<std::shared_ptr<Texture>> texture = call["texture"]; texture && *texture)
        {
            out.texture = *texture;
        }

        if (model && *model)
        {
            // A model brings its own colours, so the default tint here is white
            // rather than the flat orange a bare mesh falls back to.
            out.color = toVec3(call["color"], glm::vec3(1.0f), "r", "g", "b");
            return static_cast<int>(r->submitModel(*model, out));
        }

        out.mesh = *mesh;
        out.color = toVec3(call["color"], glm::vec3(0.85f, 0.35f, 0.25f), "r", "g", "b");
        r->submit(std::move(out));
        return 1;
    };
    rendererTable["stats"] = [r](sol::this_state state)
    {
        const Renderer::Stats& stats = r->lastFrameStats();
        return sol::state_view(state).create_table_with(
            "drawCalls", stats.drawCalls, "triangles", stats.triangles, "shaderBinds",
            stats.shaderBinds, "textureBinds", stats.textureBinds);
    };

    // ---- Engine.window ---------------------------------------------------
    sol::table windowTable = engine.create_named("window");
    windowTable["width"] = [r]() { return r->viewportWidth(); };
    windowTable["height"] = [r]() { return r->viewportHeight(); };
    windowTable["aspect"] = [r]() { return r->aspect(); };
    windowTable["close"] = [w]() { glfwSetWindowShouldClose(w, GLFW_TRUE); };
    windowTable["setTitle"] = [w](const std::string& title)
    { glfwSetWindowTitle(w, title.c_str()); };

    // ---- Engine.input ----------------------------------------------------
    sol::table input = engine.create_named("input");
    input["key"] = [w](const std::string& name)
    {
        const int code = keyFromName(name);
        return code != GLFW_KEY_UNKNOWN && glfwGetKey(w, code) == GLFW_PRESS;
    };
    input["mouse"] = [w]()
    {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(w, &x, &y);
        return std::make_tuple(x, y);
    };
    input["mouseButton"] = [w](int button)
    {
        return glfwGetMouseButton(w, button - 1) == GLFW_PRESS; // 1-based, Lua style
    };
    input["setCursorLocked"] = [w](bool locked)
    { glfwSetInputMode(w, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL); };

    // ---- Engine.time -----------------------------------------------------
    sol::table time = engine.create_named("time");
    time["now"] = []() { return glfwGetTime(); };
    time["delta"] = [this]() { return delta; };
}
