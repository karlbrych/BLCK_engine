#include <scripting/Bindings.h>

namespace scripting
{

void install(sol::state& lua, const Context& context)
{
    // The order is not load-bearing -- Lua resolves names at call time, so no
    // table here needs another to exist yet -- but it reads as the engine does:
    // what a scene looks through, what it makes, what draws it, what it runs on.
    sol::table engine = lua.create_named_table("Engine");

    installCamera(lua, engine, context);
    installResources(lua, engine, context);
    installRenderer(engine, context);
    installPlatform(lua, engine, context);
}

} // namespace scripting
