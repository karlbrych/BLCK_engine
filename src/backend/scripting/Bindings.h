#pragma once

#include <sol/sol.hpp>

struct GLFWwindow;
class Renderer;

// The Engine table: everything a gameplay script is allowed to touch. A script
// never sees a raw GL handle, and the C++ side keeps one obvious place to look
// when the surface has to grow.
//
// Split by subject rather than by kind -- the camera's usertype and its
// factory sit in the same file, and so do the renderer's draw call and its
// debug switches -- so that adding to one part of the API means opening one
// file, and so that no single translation unit grows into the thousand-line
// wall this used to be.
namespace scripting
{

// What the bindings need from the engine. Held as pointers rather than copies
// because `delta` is rewritten every frame: a binding that captured its value
// would hand the scripts a frozen clock.
struct Context
{
    Renderer* renderer = nullptr;
    GLFWwindow* window = nullptr;
    const double* delta = nullptr;
};

// Builds the Engine table and everything under it. Call once, on a state whose
// libraries are already open.
void install(sol::state& lua, const Context& context);

// The pieces, each installing the usertypes and the tables of one subject.
void installCamera(sol::state& lua, sol::table& engine, const Context& context);
void installResources(sol::state& lua, sol::table& engine, const Context& context);
void installRenderer(sol::table& engine, const Context& context);
void installPlatform(sol::state& lua, sol::table& engine, const Context& context);

} // namespace scripting
