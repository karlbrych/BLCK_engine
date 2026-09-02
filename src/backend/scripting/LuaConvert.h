#pragma once

#include <glm/glm.hpp>
#include <sol/sol.hpp>

// How a vector crosses the boundary between a script and the engine. Scripts
// write {0, 1, 0}, or {x = 0, y = 1, z = 0}, or a single number for all three,
// and never see a bound glm type -- which is what keeps the Lua side idiomatic
// Lua. Every binding converts through here, so that what one call accepts all
// of them accept.
namespace scripting
{

// first/second/third name the keyed spelling, so a colour can arrive as
// {r = 1, g = 0, b = 0} and a position as {x = 1, y = 0, z = 0}.
glm::vec3 toVec3(const sol::object& value, const glm::vec3& fallback, const char* first = "x",
                 const char* second = "y", const char* third = "z");

// The same, for a named field of a table: readVec3(call, "position", origin).
glm::vec3 readVec3(const sol::table& source, const char* key, const glm::vec3& fallback);

} // namespace scripting
