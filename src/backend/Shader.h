#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glad/gl.h>

#ifdef HAVE_GLM
#include <glm/glm.hpp>
#endif

// A linked GL program plus a cache of its uniform locations.
//
// Move-only for the same reason as Mesh: the destructor deletes the program, so
// exactly one Shader may own a given name. Create and destroy it while the GL
// context is current.
//
// Uniforms are set through glProgramUniform*, so setting them does not disturb
// whatever program is currently bound -- you do not have to call use() first.
class Shader
{
public:
    struct Stage
    {
        GLenum type = GL_VERTEX_SHADER;
        std::string source;
        std::string name = "<memory>"; // shown in compile errors
    };

    Shader() = default;

    // Compiles and links the stages, or throws std::runtime_error with the
    // compiler/linker log attached.
    explicit Shader(std::vector<Stage> stages);

    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    static Shader fromFiles(const std::filesystem::path& vertex,
                            const std::filesystem::path& fragment,
                            const std::filesystem::path& geometry = {});
    static Shader fromSource(std::string_view vertex, std::string_view fragment);
    static Shader computeFromFile(const std::filesystem::path& compute);
    // Any stage combination; each stage is inferred from the file extension
    // (.vert/.vs, .frag/.fs, .geom/.gs, .tesc, .tese, .comp).
    static Shader fromPaths(std::span<const std::filesystem::path> paths);
    bool reload();

    void use() const;
    static void unbind();

    // Compute shaders only.
    void dispatch(GLuint groupsX, GLuint groupsY = 1, GLuint groupsZ = 1) const;

    [[nodiscard]] GLuint id() const { return program; }
    [[nodiscard]] bool valid() const { return program != 0; }
    [[nodiscard]] bool fileBacked() const { return !files.empty(); }

    // -1 for a name the linker optimised away; looked up once, then cached.
    // Warns the first time a name is missing, because setting a uniform that is
    // not there is usually a typo.
    [[nodiscard]] GLint location(std::string_view name) const;

    // Whether the program declares (and kept) this uniform, asked without the
    // warning. For uniforms the engine offers rather than requires: a shader is
    // free to use the camera matrices, the inverses, both or neither, and only
    // what it declares gets written.
    [[nodiscard]] bool has(std::string_view name) const;

    void set(std::string_view name, bool value) const;
    void set(std::string_view name, int value) const;
    void set(std::string_view name, unsigned int value) const;
    void set(std::string_view name, float value) const;
    void set(std::string_view name, float x, float y) const;
    void set(std::string_view name, float x, float y, float z) const;
    void set(std::string_view name, float x, float y, float z, float w) const;
    void set(std::string_view name, std::span<const float> values) const;
    void set(std::string_view name, std::span<const int> values) const;

#ifdef HAVE_GLM
    void set(std::string_view name, const glm::vec2& value) const;
    void set(std::string_view name, const glm::vec3& value) const;
    void set(std::string_view name, const glm::vec4& value) const;
    void set(std::string_view name, const glm::ivec2& value) const;
    void set(std::string_view name, const glm::ivec3& value) const;
    void set(std::string_view name, const glm::ivec4& value) const;
    void set(std::string_view name, const glm::mat3& value) const;
    void set(std::string_view name, const glm::mat4& value) const;
#endif

    // Binds the texture to a unit and points the sampler uniform at it.
    void bindTexture(std::string_view name, GLuint unit, GLuint texture) const;
    // Points a uniform block (e.g. "Camera") at a UBO binding point.
    void bindUniformBlock(std::string_view name, GLuint bindingPoint) const;

private:
    // Transparent hash so string_view lookups do not allocate a std::string.
    struct StringHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }
    };

    static GLuint compile(const Stage& stage);
    static GLuint link(const std::vector<Stage>& stages);
    static std::string readFile(const std::filesystem::path& path);

    void destroy() noexcept;

    GLuint program = 0;
    std::vector<std::pair<GLenum, std::filesystem::path>> files;
    mutable std::unordered_map<std::string, GLint, StringHash, std::equal_to<>> uniforms;
};
