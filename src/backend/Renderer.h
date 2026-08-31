#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <EngineCamera.h>
#include <Mesh.h>
#include <Model.h>
#include <Shader.h>
#include <Texture.h>

// One queued piece of geometry. Scripts fill this in and hand it over; nothing
// touches GL until flush() runs.
//
// The mesh and shader are held by shared_ptr on purpose: a script can drop its
// last reference to a mesh halfway through building a frame, and the queue must
// still be safe to draw.
struct DrawCall
{
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Shader> shader;
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // euler angles in degrees, applied Y * X * Z
    glm::vec3 scale{1.0f};
    glm::vec3 color{0.85f, 0.35f, 0.25f};
    float alpha = 1.0f;
    // Sampled and multiplied into color when present; nullptr means flat colour.
    std::shared_ptr<Texture> texture;
    GLenum mode = GL_TRIANGLES;
    GLsizei instances = 1;
    // Materials imported from a model carry both of these; a script's own draw
    // calls keep the defaults unless it says otherwise.
    bool doubleSided = false;
    bool blend = false;
    // Prepended to the position/rotation/scale transform. Model parts use it to
    // carry the node transform they were exported with.
    glm::mat4 preTransform{1.0f};

    [[nodiscard]] glm::mat4 modelMatrix() const;
};

// Collects draw calls for a frame and replays them against the active camera.
//
// The split matters for scripting: a Lua error midway through a frame leaves a
// half-built queue that simply gets discarded, instead of a half-drawn frame
// with GL state left in whatever the script was in the middle of setting.
class Renderer
{
public:
    struct Stats
    {
        std::size_t drawCalls = 0;
        std::size_t triangles = 0;
        std::size_t shaderBinds = 0;
        std::size_t textureBinds = 0;
    };

    // Sets up the depth test and sRGB write path. Needs a current GL context.
    void init();

    void setViewport(int width, int height);
    [[nodiscard]] int viewportWidth() const { return width; }
    [[nodiscard]] int viewportHeight() const { return height; }
    [[nodiscard]] float aspect() const;

    void setClearColor(float r, float g, float b, float a = 1.0f);
    void setCamera(std::shared_ptr<EngineCamera> value);
    [[nodiscard]] const std::shared_ptr<EngineCamera>& camera() const { return activeCamera; }

    // Clears the queue and the framebuffer, ready for this frame's submissions.
    void beginFrame();
    void submit(DrawCall call);
    // Expands a model into one draw call per part, inheriting the prototype's
    // transform and shader and overriding colour/texture from each material.
    // Returns how many calls were queued.
    std::size_t submitModel(const std::shared_ptr<Model>& model, const DrawCall& prototype);
    // Executes and empties the queue.
    void flush();
    // Throws away the queue without drawing it -- used when a script errors out.
    void discard();

    // Releases every GL object the renderer owns. Must run while the context is
    // still current, so the destructor cannot be relied on for it.
    void shutdown();

    [[nodiscard]] const Stats& lastFrameStats() const { return stats; }
    [[nodiscard]] std::size_t queuedCalls() const { return queue.size(); }

    // Cached so a scene does not have to reload the same shader every reload.
    std::shared_ptr<Shader> loadShader(const std::string& vertexPath,
                                       const std::string& fragmentPath);
    // Rebuilds every cached file-backed shader; a failed one keeps its old program.
    int reloadShaders();

    // Models and textures are cached by path for the same reason shaders are,
    // and it matters far more here: a script hot reload must not re-parse a
    // 50 MB GLB and re-upload every one of its textures.
    std::shared_ptr<Model> loadModel(const std::string& path, const Model::Options& options = {});
    std::shared_ptr<Texture> loadTexture(const std::string& path, bool srgb = true);
    // Drops cached models and textures nothing else still references.
    void trimCaches();

    // ---- debug rendering -------------------------------------------------
    // Draws every queued call as lines instead of filled triangles, in one flat
    // colour: a texture stretched over a wireframe is unreadable, and the point
    // of looking at one is to see the topology.
    //
    // The setters are compiled out unless BLCK_DEBUG is defined, so in a build
    // without it the flag can never become true and flush() takes the ordinary
    // path every time. Scripts can ask which build they are in through
    // debugToolsAvailable(), rather than calling into a silent no-op.
    void setWireframe(bool enabled);
    void setWireframeColor(const glm::vec3& color);
    [[nodiscard]] bool wireframe() const { return wireframeEnabled; }
    [[nodiscard]] static bool debugToolsAvailable();

private:
    std::vector<DrawCall> queue;
    std::shared_ptr<EngineCamera> activeCamera;
    std::vector<std::pair<std::string, std::shared_ptr<Shader>>> shaderCache;
    std::vector<std::pair<std::string, std::shared_ptr<Model>>> modelCache;
    std::vector<std::pair<std::string, std::shared_ptr<Texture>>> textureCache;
    // Bound wherever a draw call has no texture of its own, so the fragment
    // shader can always sample instead of branching on a uniform.
    std::shared_ptr<Texture> defaultTexture;

    glm::vec4 clearColor{0.05f, 0.06f, 0.09f, 1.0f};
    bool wireframeEnabled = false;
    glm::vec3 wireframeColor{0.35f, 1.0f, 0.55f};
    int width = 0;
    int height = 0;
    Stats stats;
};
