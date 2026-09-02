#pragma once

#include <string>

namespace scripting
{

// Scripts name keys ("w", "space", "left"); GLFW wants an int. Returns
// GLFW_KEY_UNKNOWN for a name that is not one, which the input binding reads as
// "never pressed" rather than as an error: a typo in a keymap should cost the
// player one dead binding, not the frame.
int keyFromName(const std::string& name);

} // namespace scripting
