#include <scripting/Bindings.h>

#include <Renderer.h>
#include <scripting/KeyNames.h>

#include <iostream>
#include <string>
#include <tuple>

#include <GLFW/glfw3.h>

namespace
{

// GLFW delivers the wheel as events, not as state: a script polling once a
// frame would miss every notch that arrived between two polls, so they are
// added up here and Engine.input.scroll() drains the total. One accumulator for
// the process is enough -- the engine runs exactly one window.
double scrollAccumulator = 0.0;

void onScroll(GLFWwindow*, double, double yOffset)
{
    scrollAccumulator += yOffset;
}

} // namespace

// The machine a script runs on rather than the world it describes: the window,
// the keyboard and mouse, the clock, and the log. This is the only binding file
// that talks to GLFW directly, which is also the shortest description of what
// would have to change to run the engine on a different window backend.
namespace scripting
{

void installPlatform(sol::state& lua, sol::table& engine, const Context& context)
{
    Renderer* r = context.renderer;
    GLFWwindow* w = context.window;

    engine["log"] = [&lua](sol::variadic_args args)
    {
        std::string line;
        const sol::function tostring = lua["tostring"];
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

    // ---- Engine.window ---------------------------------------------------
    sol::table windowTable = engine.create_named("window");
    windowTable["width"] = [r]() { return r->viewportWidth(); };
    windowTable["height"] = [r]() { return r->viewportHeight(); };
    windowTable["aspect"] = [r]() { return r->aspect(); };
    // Whether the window has the keyboard. Anything that reads the pointer's
    // resting place rather than its movement -- edge panning, above all -- has
    // to ask, or a cursor left near the edge of an unfocused window keeps
    // driving the game.
    windowTable["focused"] = [w]() { return glfwGetWindowAttrib(w, GLFW_FOCUSED) == GLFW_TRUE; };
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
    // Notches since the last call, positive away from the user. Reading clears
    // it, so exactly one caller per frame should ask -- lib/input.lua does, and
    // hands the value to everything else along with the rest of the snapshot.
    glfwSetScrollCallback(w, onScroll);
    input["scroll"] = []()
    {
        const double total = scrollAccumulator;
        scrollAccumulator = 0.0;
        return total;
    };
    input["setCursorLocked"] = [w](bool locked)
    { glfwSetInputMode(w, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL); };

    // ---- Engine.time -----------------------------------------------------
    sol::table time = engine.create_named("time");
    time["now"] = []() { return glfwGetTime(); };
    time["delta"] = [delta = context.delta]() { return *delta; };
}

} // namespace scripting
