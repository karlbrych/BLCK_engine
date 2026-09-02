#include <scripting/KeyNames.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include <GLFW/glfw3.h>

namespace scripting
{

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

} // namespace scripting
