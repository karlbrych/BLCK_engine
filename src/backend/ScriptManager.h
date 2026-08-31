#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

struct GLFWwindow;
class Renderer;

//zde je manager pro skripty, ktere se budou nacitat z adresare gameplay
//scripty se nacitaji z /gameplay folderu
//drzime konvenci, kazdy script returnuje tabulku ktera ma funkce init,update,draw a shutdown
class ScriptManager
{
public:
    explicit ScriptManager(std::filesystem::path scriptRoot = "gameplay");
    ~ScriptManager();

    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    // Opens the standard libraries and installs the Engine table. Call once,
    // after the GL context is current and before loading anything.
    void bind(Renderer& renderer, GLFWwindow* window);

    // Loads <root>/<name>.lua and registers its hooks. Returns false and
    // leaves any previous version in place when the file fails to load.
    bool load(const std::string& name);
    // Registers every hook-carrying .lua directly inside the script root.
    std::size_t loadAll();

    // Runs init() on scripts that have not had it yet.
    void start();
    void update(double deltaSeconds);
    void draw();

    // Reloads the scripts whose files changed on disk since the last check.
    // Returns how many were reloaded.
    std::size_t reloadChanged();
    void reloadAll();

    // Runs shutdown() on every script and tears the VM down. Must run while the
    // GL context is current: the VM holds the meshes, textures, models and
    // shaders scripts made.
    void shutdown();

    [[nodiscard]] const std::filesystem::path& root() const { return scriptRoot; }
    [[nodiscard]] std::size_t scriptCount() const { return scripts.size(); }
    [[nodiscard]] double deltaTime() const { return delta; }
    [[nodiscard]] sol::state& state() { return *lua; }

private:
    struct Script
    {
        std::string name;
        std::filesystem::path path;
        std::filesystem::file_time_type stamp{};
        sol::table module;
        bool started = false;
        bool failed = false;
    };

    void installEngineTable(Renderer& renderer, GLFWwindow* window);
    // Runs one hook under a traceback handler; disables the script on error.
    void call(Script& script, const char* hook, sol::object argument = sol::lua_nil);
    // requested = the caller asked for this file by name, so a file that is not
    // a script is an error worth reporting; loadAll() passes false and skips
    // libraries in silence.
    bool loadInto(Script& script, bool requested);
    Script* find(const std::string& name);
    // True when the module is meant to run in the frame loop rather than be
    // require()d by something that does.
    static bool isFrameScript(const sol::table& module);
    [[nodiscard]] std::filesystem::path pathFor(const std::string& name) const;

    // Every .lua under the root, sorted, hooks or not: a helper module changing
    // has to trigger a reload just as much as a scene does.
    [[nodiscard]] std::vector<std::filesystem::path> scriptFiles() const;
    void rescanWatchList();

    std::unique_ptr<sol::state> lua;
    std::filesystem::path scriptRoot;
    std::vector<Script> scripts;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> watched;
    Renderer* renderer = nullptr;
    GLFWwindow* window = nullptr;
    double delta = 0.0;
};
