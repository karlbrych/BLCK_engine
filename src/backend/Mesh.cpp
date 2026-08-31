#include <Mesh.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

constexpr float kPi = 3.14159265358979323846f;


constexpr std::array<VertexAttribute, 3> kVertexLayout{{
    {0, 3, 0, GL_FLOAT, GL_FALSE},  // position
    {1, 3, 12, GL_FLOAT, GL_FALSE}, // normal
    {2, 2, 24, GL_FLOAT, GL_FALSE}, // uv
}};

// Small builder so the primitive tables read as plain numbers.
Vertex vtx(float px, float py, float pz, float nx, float ny, float nz, float u, float v)
{
    Vertex out;
    out.position = Vec3{px, py, pz};
    out.normal = Vec3{nx, ny, nz};
    out.uv = Vec2{u, v};
    return out;
}

// Integer attributes must go through glVertexArrayAttribIFormat: the float path
// would silently convert them and the shader would read garbage.
bool isIntegerType(GLenum type, GLboolean normalized)
{
    if (normalized == GL_TRUE)
    {
        return false;
    }
    switch (type)
    {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
    case GL_INT:
    case GL_UNSIGNED_INT:
        return true;
    default:
        return false;
    }
}

} // namespace

Mesh::Mesh(std::span<const Vertex> verts, std::span<const std::uint32_t> idx, bool dynamic)
{
    create(verts.data(), static_cast<GLsizeiptr>(verts.size_bytes()),
           static_cast<GLsizei>(sizeof(Vertex)), kVertexLayout, idx, dynamic);
}

Mesh::Mesh(std::span<const std::byte> vertexData, GLsizei stride,
           std::span<const VertexAttribute> attributes, std::span<const std::uint32_t> idx,
           bool dynamic)
{
    create(vertexData.data(), static_cast<GLsizeiptr>(vertexData.size_bytes()), stride, attributes,
           idx, dynamic);
}

Mesh::Mesh(std::span<const float> vertexData, GLsizei stride,
           std::span<const VertexAttribute> attributes, std::span<const std::uint32_t> idx,
           bool dynamic)
{
    create(vertexData.data(), static_cast<GLsizeiptr>(vertexData.size_bytes()), stride, attributes,
           idx, dynamic);
}

void Mesh::create(const void* vertexData, GLsizeiptr byteSize, GLsizei stride,
                  std::span<const VertexAttribute> attributes, std::span<const std::uint32_t> idx,
                  bool dynamic)
{
    if (byteSize <= 0)
    {
        throw std::runtime_error("Mesh: no vertex data");
    }
    if (stride <= 0)
    {
        throw std::runtime_error("Mesh: vertex stride must be positive");
    }
    if (byteSize % stride != 0)
    {
        throw std::runtime_error("Mesh: vertex data size is not a multiple of the stride");
    }
    if (attributes.empty())
    {
        throw std::runtime_error("Mesh: vertex layout has no attributes");
    }

    vertices = static_cast<GLsizei>(byteSize / stride);
    indices = static_cast<GLsizei>(idx.size());
    vertexBytes = byteSize;
    dynamicStorage = dynamic;

    const GLbitfield flags = dynamic ? GL_DYNAMIC_STORAGE_BIT : 0;

    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, byteSize, vertexData, flags);

    glCreateVertexArrays(1, &vao);
    // Everything comes from one interleaved buffer, so binding index 0 is enough.
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, stride);

    for (const VertexAttribute& attribute : attributes)
    {
        glEnableVertexArrayAttrib(vao, attribute.location);
        if (isIntegerType(attribute.type, attribute.normalized))
        {
            glVertexArrayAttribIFormat(vao, attribute.location, attribute.components,
                                       attribute.type, attribute.offset);
        }
        else
        {
            glVertexArrayAttribFormat(vao, attribute.location, attribute.components, attribute.type,
                                      attribute.normalized, attribute.offset);
        }
        glVertexArrayAttribBinding(vao, attribute.location, 0);
    }

    if (!idx.empty())
    {
        glCreateBuffers(1, &ebo);
        glNamedBufferStorage(ebo, static_cast<GLsizeiptr>(idx.size_bytes()), idx.data(), flags);
        glVertexArrayElementBuffer(vao, ebo);
    }
}

void Mesh::destroy() noexcept
{
    // glDelete* ignores name 0, so a moved-from mesh cleans up to nothing.
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);
    vao = vbo = ebo = 0;
    vertices = indices = 0;
    vertexBytes = 0;
}

Mesh::~Mesh()
{
    destroy();
}

Mesh::Mesh(Mesh&& other) noexcept
    : vao(std::exchange(other.vao, 0)), vbo(std::exchange(other.vbo, 0)),
      ebo(std::exchange(other.ebo, 0)), vertices(std::exchange(other.vertices, 0)),
      indices(std::exchange(other.indices, 0)), vertexBytes(std::exchange(other.vertexBytes, 0)),
      dynamicStorage(other.dynamicStorage)
{
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        vao = std::exchange(other.vao, 0);
        vbo = std::exchange(other.vbo, 0);
        ebo = std::exchange(other.ebo, 0);
        vertices = std::exchange(other.vertices, 0);
        indices = std::exchange(other.indices, 0);
        vertexBytes = std::exchange(other.vertexBytes, 0);
        dynamicStorage = other.dynamicStorage;
    }
    return *this;
}

void Mesh::bind() const
{
    glBindVertexArray(vao);
}

void Mesh::draw(GLenum mode) const
{
    drawInstanced(1, mode);
}

void Mesh::drawInstanced(GLsizei instanceCount, GLenum mode) const
{
    if (vao == 0 || instanceCount <= 0)
    {
        return;
    }

    glBindVertexArray(vao);
    if (ebo != 0)
    {
        glDrawElementsInstanced(mode, indices, GL_UNSIGNED_INT, nullptr, instanceCount);
    }
    else
    {
        glDrawArraysInstanced(mode, 0, vertices, instanceCount);
    }
}

void Mesh::updateVertices(std::span<const std::byte> data, GLintptr byteOffset)
{
    if (!dynamicStorage)
    {
        throw std::runtime_error("Mesh: updateVertices needs a mesh built with dynamic = true");
    }
    const GLsizeiptr size = static_cast<GLsizeiptr>(data.size_bytes());
    if (byteOffset < 0 || size > vertexBytes - byteOffset)
    {
        throw std::runtime_error("Mesh: updateVertices would write past the end of the buffer");
    }
    if (size == 0)
    {
        return;
    }
    glNamedBufferSubData(vbo, byteOffset, size, data.data());
}

void Mesh::updateVertices(std::span<const Vertex> verts, GLintptr byteOffset)
{
    updateVertices(std::as_bytes(verts), byteOffset);
}

// ------------------------------------------------------------------ primitives

Mesh Mesh::triangle(float size)
{
    const float h = size * 0.5f;
    const std::array<Vertex, 3> verts{{
        vtx(-h, -h, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        vtx(h, -h, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f),
        vtx(0.0f, h, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f),
    }};
    return Mesh(std::span<const Vertex>(verts));
}

Mesh Mesh::quad(float width, float height)
{
    const float w = width * 0.5f;
    const float h = height * 0.5f;
    const std::array<Vertex, 4> verts{{
        vtx(-w, -h, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        vtx(w, -h, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f),
        vtx(w, h, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f),
        vtx(-w, h, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f),
    }};
    const std::array<std::uint32_t, 6> idx{0, 1, 2, 2, 3, 0};
    return Mesh(std::span<const Vertex>(verts), std::span<const std::uint32_t>(idx));
}

Mesh Mesh::cube(float size)
{
    // Each face gets its own four vertices: a shared corner cannot carry three
    // different normals, and flat shading needs the per-face normal.
    struct Face
    {
        float n[3];
        float right[3];
        float up[3];
    };

    // right x up == normal for every face, which keeps the winding CCW outwards.
    constexpr std::array<Face, 6> faces{{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};
    constexpr std::array<std::array<float, 2>, 4> corners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

    const float h = size * 0.5f;
    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    verts.reserve(24);
    idx.reserve(36);

    for (const Face& face : faces)
    {
        const auto base = static_cast<std::uint32_t>(verts.size());
        for (const auto& corner : corners)
        {
            const float su = corner[0] * 2.0f - 1.0f;
            const float sv = corner[1] * 2.0f - 1.0f;
            verts.push_back(vtx(h * (face.n[0] + su * face.right[0] + sv * face.up[0]),
                                h * (face.n[1] + su * face.right[1] + sv * face.up[1]),
                                h * (face.n[2] + su * face.right[2] + sv * face.up[2]), face.n[0],
                                face.n[1], face.n[2], corner[0], corner[1]));
        }
        for (std::uint32_t offset : {0u, 1u, 2u, 2u, 3u, 0u})
        {
            idx.push_back(base + offset);
        }
    }

    return Mesh(std::span<const Vertex>(verts), std::span<const std::uint32_t>(idx));
}

Mesh Mesh::sphere(float radius, unsigned int rings, unsigned int segments)
{
    rings = rings < 2 ? 2 : rings;
    segments = segments < 3 ? 3 : segments;

    std::vector<Vertex> verts;
    std::vector<std::uint32_t> idx;
    verts.reserve(static_cast<std::size_t>(rings + 1) * (segments + 1));
    idx.reserve(static_cast<std::size_t>(rings) * segments * 6);

    for (unsigned int ring = 0; ring <= rings; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float phi = v * kPi;
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);

        for (unsigned int segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float x = ringRadius * std::cos(theta);
            const float z = ringRadius * std::sin(theta);
            // The unit-sphere position doubles as the normal.
            verts.push_back(vtx(x * radius, y * radius, z * radius, x, y, z, u, 1.0f - v));
        }
    }

    const auto stride = static_cast<std::uint32_t>(segments + 1);
    for (unsigned int ring = 0; ring < rings; ++ring)
    {
        for (unsigned int segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t a = ring * stride + segment;
            const std::uint32_t b = a + stride;

            if (ring != 0) // the top row collapses into the pole
            {
                idx.push_back(a);
                idx.push_back(a + 1);
                idx.push_back(b);
            }
            if (ring + 1 != rings) // and so does the bottom one
            {
                idx.push_back(a + 1);
                idx.push_back(b + 1);
                idx.push_back(b);
            }
        }
    }

    return Mesh(std::span<const Vertex>(verts), std::span<const std::uint32_t>(idx));
}
