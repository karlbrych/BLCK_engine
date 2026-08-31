#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glad/gl.h>

#ifdef HAVE_GLM
#include <glm/glm.hpp>
#endif

#ifdef HAVE_GLM
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
#else
struct Vec2
{
    float x = 0.0f, y = 0.0f;
};
struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
#endif


struct Vertex
{
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
};

static_assert(sizeof(Vertex) == 32, "Vertex must stay tightly packed for the GPU layout");

struct VertexAttribute
{
    GLuint location = 0;
    GLint components = 3;            
    GLuint offset = 0;              
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
};

class Mesh
{
public:
    Mesh() = default;


    explicit Mesh(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices = {},
                  bool dynamic = false);

    Mesh(std::span<const std::byte> vertexData, GLsizei stride,
         std::span<const VertexAttribute> attributes, std::span<const std::uint32_t> indices = {},
         bool dynamic = false);

    Mesh(std::span<const float> vertexData, GLsizei stride,
         std::span<const VertexAttribute> attributes, std::span<const std::uint32_t> indices = {},
         bool dynamic = false);

    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void bind() const;
    void draw(GLenum mode = GL_TRIANGLES) const;
    void drawInstanced(GLsizei instanceCount, GLenum mode = GL_TRIANGLES) const;

    // Only valid on a mesh built with dynamic = true. Throws otherwise, and
    // throws if the update would run past the end of the buffer.
    void updateVertices(std::span<const std::byte> data, GLintptr byteOffset = 0);
    void updateVertices(std::span<const Vertex> vertices, GLintptr byteOffset = 0);

    [[nodiscard]] bool valid() const { return vao != 0; }
    [[nodiscard]] GLuint id() const { return vao; }
    [[nodiscard]] GLsizei vertexCount() const { return vertices; }
    [[nodiscard]] GLsizei indexCount() const { return indices; }
    [[nodiscard]] bool indexed() const { return ebo != 0; }

    // ---- primitives, all built around the standard Vertex layout ----
    static Mesh triangle(float size = 1.0f);
    static Mesh quad(float width = 1.0f, float height = 1.0f);
    static Mesh cube(float size = 1.0f);
    static Mesh sphere(float radius = 0.5f, unsigned int rings = 16, unsigned int segments = 32);

private:
    void create(const void* vertexData, GLsizeiptr vertexBytes, GLsizei stride,
                std::span<const VertexAttribute> attributes, std::span<const std::uint32_t> idx,
                bool dynamic);
    void destroy() noexcept;

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei vertices = 0;
    GLsizei indices = 0;
    GLsizeiptr vertexBytes = 0;
    bool dynamicStorage = false;
};
