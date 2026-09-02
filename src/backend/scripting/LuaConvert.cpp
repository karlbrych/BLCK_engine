#include <scripting/LuaConvert.h>

namespace
{
// Positional first, then keyed: {1, 2, 3} and {x = 1, y = 2, z = 3} are both
// how a Lua author would write a vector, and neither should have to be spelled
// the other way round to be understood.
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

} // namespace

namespace scripting
{

glm::vec3 toVec3(const sol::object& value, const glm::vec3& fallback, const char* first,
                 const char* second, const char* third)
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

} // namespace scripting
