#include <ScriptManager.h>

#include <Renderer.h> // a failed hook discards the frame it was half-way through
#include <scripting/Bindings.h>

#include <algorithm>
#include <iostream>
#include <system_error>
#include <utility>

//zde se nacitaji a spousteji skripty z gameplay/ -- co z nich smi sahnout na
//engine je v scripting/, aby tenhle soubor zustal o zivotnim cyklu skriptu
namespace fs = std::filesystem;

namespace
{
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

    // Everything the scripts may touch, in one call. The context holds
    // pointers rather than values because `delta` changes every frame and the
    // bindings must read the live one.
    scripting::install(*lua, scripting::Context{&activeRenderer, activeWindow, &delta});
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
    //
    // Keyed the way require() was called, which for a file in a subdirectory is
    // the dotted path relative to the root -- require("lib.input") caches under
    // "lib.input", not under "input". Clearing only the stem would leave every
    // module outside the top level frozen at whatever the first load saw, and
    // the reload would look like it had worked.
    sol::table loaded = (*lua)["package"]["loaded"];
    for (const auto& [path, stamp] : watched)
    {
        (void)stamp;
        loaded[path.stem().string()] = sol::lua_nil;

        std::error_code error;
        fs::path relative = fs::relative(path, scriptRoot, error);
        if (error)
        {
            continue;
        }
        relative.replace_extension();
        std::string module = relative.generic_string();
        std::replace(module.begin(), module.end(), '/', '.');
        loaded[module] = sol::lua_nil;

        // package.path also resolves require("thing") to thing/init.lua.
        if (relative.filename() == "init" && relative.has_parent_path())
        {
            std::string package = relative.parent_path().generic_string();
            std::replace(package.begin(), package.end(), '/', '.');
            loaded[package] = sol::lua_nil;
        }
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
