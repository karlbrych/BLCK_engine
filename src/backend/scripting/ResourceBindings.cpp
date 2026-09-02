#include <scripting/Bindings.h>

#include <Mesh.h>
#include <Model.h>
#include <Renderer.h>
#include <Shader.h>
#include <Texture.h>
#include <scripting/LuaConvert.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>

// The things a scene is made of -- meshes, models, textures, shaders -- and the
// handles it holds them by. Everything here is reference-counted on the C++
// side and cached by the renderer, so a hot reload that rebuilds every script
// re-uses what is already on the GPU rather than loading a map twice.
namespace scripting
{

void installResources(sol::state& lua, sol::table& engine, const Context& context)
{
    Renderer* r = context.renderer;

    // Vectors cross the boundary as plain numbers or tables rather than as a
    // bound glm type: scripts get to write {0, 1, 0} and stay idiomatic Lua,
    // and they come back as three values, so a script can write
    //   local x, y, z = model:center()
    lua.new_usertype<Mesh>("Mesh", sol::no_constructor,       //
                         "valid", &Mesh::valid,             //
                         "vertexCount", &Mesh::vertexCount, //
                         "indexCount", &Mesh::indexCount,   //
                         "indexed", &Mesh::indexed);

    lua.new_usertype<Texture>("Texture", sol::no_constructor, //
                            "valid", &Texture::valid,       //
                            "width", &Texture::getWidth,    //
                            "height", &Texture::getHeight,  //
                            "channels", &Texture::getChannels);

    // Vectors come back as three values here too, so a script can write
    //   local x, y, z = model:center()
    lua.new_usertype<Model>(
        "Model", sol::no_constructor, //
        "partCount", [](const Model& self) { return self.parts().size(); }, "materialCount",
        [](const Model& self) { return self.materials().size(); }, "triangleCount",
        &Model::triangleCount, "vertexCount", &Model::vertexCount, "textureCount",
        &Model::textureCount, "source", &Model::source, "radius", &Model::radius, "center",
        [](const Model& self)
        {
            const glm::vec3 v = self.center();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "size",
        [](const Model& self)
        {
            const glm::vec3 v = self.size();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "boundsMin",
        [](const Model& self)
        {
            const glm::vec3& v = self.boundsMin();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "boundsMax",
        [](const Model& self)
        {
            const glm::vec3& v = self.boundsMax();
            return std::make_tuple(v.x, v.y, v.z);
        },

        // ---- collision ----
        "hasCollision", &Model::hasCollision,                //
        "collisionTriangleCount", &Model::collisionTriangleCount,

        // Height of the ground under (x, z), or nil where there is none: off
        // the edge of the map, or over a hole in the mesh. fromY caps the
        // search, so a body cannot be pulled up onto a roof it is standing
        // under.
        "groundHeight",
        [](const Model& self, float x, float z,
           sol::optional<float> fromY) -> sol::optional<float>
        {
            const std::optional<float> y =
                fromY ? self.groundHeight(x, z, *fromY) : self.groundHeight(x, z);
            if (!y)
            {
                return sol::nullopt;
            }
            return *y;
        },

        // model:raycast(ox, oy, oz, dx, dy, dz [, maxDistance]) -> hit or nil,
        // where hit is { x, y, z, distance, normal = { x, y, z } }. A table
        // rather than eight return values: a miss is then a plain nil test.
        "raycast",
        [](const Model& self, float ox, float oy, float oz, float dx, float dy, float dz,
           sol::optional<float> maxDistance, sol::this_state state) -> sol::object
        {
            ModelRayHit hit;
            if (!self.raycast(glm::vec3(ox, oy, oz), glm::vec3(dx, dy, dz),
                              maxDistance.value_or(0.0f), &hit))
            {
                return sol::nil;
            }
            sol::state_view L(state);
            sol::table out = L.create_table();
            out["x"] = hit.position.x;
            out["y"] = hit.position.y;
            out["z"] = hit.position.z;
            out["distance"] = hit.distance;
            out["normal"] = L.create_table_with("x", hit.normal.x, "y", hit.normal.y, "z",
                                                hit.normal.z);
            return out;
        });

    lua.new_usertype<Shader>(
        "Shader", sol::no_constructor, //
        "valid", &Shader::valid,       //
        "reload", &Shader::reload,     //
        "setFloat",
        [](Shader& self, const std::string& name, float value) { self.set(name, value); }, "setInt",
        [](Shader& self, const std::string& name, int value) { self.set(name, value); }, "setVec3",
        [](Shader& self, const std::string& name, sol::object value)
        { self.set(name, toVec3(value, glm::vec3(0.0f))); });

    // ---- Engine.mesh -----------------------------------------------------
    sol::table mesh = engine.create_named("mesh");
    mesh["cube"] = [](sol::optional<float> size)
    { return std::make_shared<Mesh>(Mesh::cube(size.value_or(1.0f))); };
    mesh["quad"] = [](sol::optional<float> width, sol::optional<float> height)
    {
        const float w = width.value_or(1.0f);
        return std::make_shared<Mesh>(Mesh::quad(w, height.value_or(w)));
    };
    mesh["triangle"] = [](sol::optional<float> size)
    { return std::make_shared<Mesh>(Mesh::triangle(size.value_or(1.0f))); };
    mesh["sphere"] = [](sol::optional<float> radius, sol::optional<unsigned int> rings,
                        sol::optional<unsigned int> segments)
    {
        return std::make_shared<Mesh>(
            Mesh::sphere(radius.value_or(0.5f), rings.value_or(16), segments.value_or(32)));
    };

    // ---- Engine.model ----------------------------------------------------
    // Loading is synchronous and can take a moment on a large map, which is why
    // it belongs in a script's init() rather than in update(). The renderer
    // caches by path, so a hot reload re-uses what is already on the GPU.
    sol::table model = engine.create_named("model");
    model["load"] = [r](const std::string& path, sol::optional<sol::table> options)
    {
        Model::Options loadOptions;
        if (options)
        {
            loadOptions.recenter = (*options)["recenter"].get_or(loadOptions.recenter);
            loadOptions.scale = (*options)["scale"].get_or(loadOptions.scale);
            loadOptions.generateNormals =
                (*options)["generateNormals"].get_or(loadOptions.generateNormals);
            loadOptions.loadTextures = (*options)["textures"].get_or(loadOptions.loadTextures);
            loadOptions.verbose = (*options)["verbose"].get_or(loadOptions.verbose);
            loadOptions.collision = (*options)["collision"].get_or(loadOptions.collision);
        }
        return r->loadModel(path, loadOptions);
    };

    // ---- Engine.texture --------------------------------------------------
    sol::table texture = engine.create_named("texture");
    texture["load"] = [r](const std::string& path, sol::optional<bool> srgb)
    { return r->loadTexture(path, srgb.value_or(true)); };

    // ---- Engine.shader ---------------------------------------------------
    sol::table shader = engine.create_named("shader");
    shader["load"] = [r](const std::string& vertex, const std::string& fragment)
    { return r->loadShader(vertex, fragment); };
    shader["reloadAll"] = [r]() { return r->reloadShaders(); };
}

} // namespace scripting
