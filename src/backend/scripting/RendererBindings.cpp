#include <scripting/Bindings.h>

#include <EngineCamera.h>
#include <Mesh.h>
#include <Model.h>
#include <Renderer.h>
#include <Shader.h>
#include <Texture.h>
#include <scripting/LuaConvert.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

// An unknown name falls back rather than throwing, the way toDrawMode does: a
// typo in a pass name should not take the frame down.
DrawCall::Pass toPass(const sol::object& value, DrawCall::Pass fallback)
{
    if (!value.is<std::string>())
    {
        return fallback;
    }

    const std::string name = value.as<std::string>();
    if (name == "background")
    {
        return DrawCall::Pass::Background;
    }
    if (name == "overlay")
    {
        return DrawCall::Pass::Overlay;
    }
    if (name == "world")
    {
        return DrawCall::Pass::World;
    }
    return fallback;
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

} // namespace

// What a script says about a frame: one submit() per thing to draw, plus the
// switches that change how the whole frame comes out. Nothing here draws
// anything -- the calls queue up and the renderer sorts and issues them once
// every script has had its say.
namespace scripting
{

void installRenderer(sol::table& engine, const Context& context)
{
    Renderer* r = context.renderer;

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
        // A fullscreen pass brings its own geometry from gl_VertexID, so it is
        // the one kind of submit that needs neither a mesh nor a model.
        const bool fullscreen = call["fullscreen"].get_or(false);
        // Reported as a Lua error rather than quietly dropped: a submit() with a
        // typo in it is a bug in the scene, and silence would hide it.
        sol::optional<std::shared_ptr<Model>> model = call["model"];
        sol::optional<std::shared_ptr<Mesh>> mesh = call["mesh"];
        sol::optional<std::shared_ptr<Shader>> shader = call["shader"];
        if (!fullscreen && (!model || !*model) && (!mesh || !*mesh))
        {
            throw std::runtime_error(
                "Engine.renderer.submit: needs a 'mesh' or a 'model' (or fullscreen = true)");
        }
        if (!shader || !*shader)
        {
            throw std::runtime_error("Engine.renderer.submit: 'shader' must be a shader");
        }
        out.shader = *shader;
        out.fullscreen = fullscreen;
        // A fullscreen pass is a background unless it says otherwise: that is
        // what a sky is, and an effect meant to sit on top can ask for one.
        out.pass = toPass(call["pass"],
                          fullscreen ? DrawCall::Pass::Background : DrawCall::Pass::World);
        if (fullscreen)
        {
            r->submit(std::move(out));
            return 1;
        }
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

    // ---- Engine.debug ----------------------------------------------------
    // `enabled` is false in a build configured with -DBLCK_DEBUG=OFF, and the
    // calls below turn into no-ops. A script should branch on it rather than
    // assume the tools are there.
    sol::table debugTable = engine.create_named("debug");
    debugTable["enabled"] = Renderer::debugToolsAvailable();
    debugTable["setWireframe"] = [r](bool enabled) { r->setWireframe(enabled); };
    debugTable["wireframe"] = [r]() { return r->wireframe(); };
    debugTable["toggleWireframe"] = [r]()
    {
        r->setWireframe(!r->wireframe());
        return r->wireframe();
    };
    debugTable["setWireframeColor"] = [r](sol::object color)
    { r->setWireframeColor(toVec3(color, glm::vec3(0.35f, 1.0f, 0.55f), "r", "g", "b")); };
}

} // namespace scripting
