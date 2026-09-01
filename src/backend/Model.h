#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Mesh.h>
#include <Texture.h>

// One glTF material, reduced to what the forward Blinn-Phong shader can use.
// The metallic/roughness channels are parsed but not consumed yet -- they are
// kept here so a PBR shader can be dropped in without touching the loader.
struct ModelMaterial
{
    std::string name;
    glm::vec3 baseColor{1.0f};
    float alpha = 1.0f;
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::shared_ptr<Texture> baseColorTexture;
    bool doubleSided = false;
    bool blend = false;
};

// One primitive of one node: a mesh plus the node's world transform inside the
// model. Every primitive keeps its own mesh instead of being merged into one
// giant buffer, because each one may carry a different material.
struct ModelPart
{
    std::string name;
    std::shared_ptr<Mesh> mesh;
    glm::mat4 transform{1.0f};
    int material = -1; // index into materials(), or -1 for the default
    std::size_t triangles = 0;
};

// Knobs for the loader. At namespace scope for the same reason TextureOptions
// is: a nested type's defaults cannot be used by the enclosing class.
struct ModelOptions
{
    // Bakes a translation that puts the bounding-box centre at the origin.
    // Scanned or geo-referenced data usually sits thousands of units out.
    bool recenter = false;
    // Uniform scale baked into every part transform.
    float scale = 1.0f;
    // Flat normals for primitives that ship none, so lighting still works.
    bool generateNormals = true;
    // Skip image decoding entirely; materials keep their base colour.
    bool loadTextures = true;
    // Printed to stdout as the file is read.
    bool verbose = true;
    // Keeps a CPU-side copy of the triangles, indexed by a uniform grid, so the
    // model can be raycast. Off for anything a script never has to stand on or
    // shoot at: the copy costs 36 bytes per triangle.
    bool collision = true;
};

// Where a ray met the collision geometry. distance is along the (normalised)
// ray direction, so position == origin + direction * distance.
struct ModelRayHit
{
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    std::size_t triangle = 0;
};

// A loaded glTF 2.0 / GLB file: flattened parts, their materials, and the
// bounds of the whole thing.
//
// The loader is deliberately narrow -- it reads the subset of glTF that a
// static scene needs (node hierarchy, meshes, PBR base colour, embedded or
// external images) and warns about the rest instead of failing. Skinning,
// morph targets, animation, sparse accessors and compressed extensions such as
// KHR_draco_mesh_compression are not supported.
//
// Meshes and textures are GL objects, so a Model must be built and released
// while the GL context is current.
class Model
{
public:
    using Options = ModelOptions;

    // Throws std::runtime_error on anything it cannot parse.
    static std::shared_ptr<Model> loadFromFile(const std::filesystem::path& path,
                                               const Options& options = {});

    [[nodiscard]] const std::vector<ModelPart>& parts() const { return modelParts; }
    [[nodiscard]] const std::vector<ModelMaterial>& materials() const { return modelMaterials; }
    [[nodiscard]] const ModelMaterial* materialFor(const ModelPart& part) const;

    [[nodiscard]] const std::string& source() const { return sourcePath; }
    [[nodiscard]] const glm::vec3& boundsMin() const { return minBounds; }
    [[nodiscard]] const glm::vec3& boundsMax() const { return maxBounds; }
    [[nodiscard]] glm::vec3 center() const { return (minBounds + maxBounds) * 0.5f; }
    [[nodiscard]] glm::vec3 size() const { return maxBounds - minBounds; }
    // Radius of the bounding sphere around center().
    [[nodiscard]] float radius() const;

    // ---- collision ----
    // Present only when the model was loaded with options.collision.
    [[nodiscard]] bool hasCollision() const { return !collisionVertices.empty(); }
    [[nodiscard]] std::size_t collisionTriangleCount() const { return collisionVertices.size() / 3; }

    // Closest hit along the ray, or false for a miss. direction need not be
    // normalised; maxDistance <= 0 means unbounded. Triangles are two-sided,
    // because terrain exported from a scan is not reliably wound one way.
    [[nodiscard]] bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                               float maxDistance = 0.0f, ModelRayHit* hit = nullptr) const;

    // Height of the highest surface at (x, z) that is at or below fromY, which
    // defaults to just above the model. Empty where nothing is underfoot -- a
    // hole in the mesh, or a point off the edge of the map.
    [[nodiscard]] std::optional<float> groundHeight(float x, float z,
                                                    std::optional<float> fromY = {}) const;

    [[nodiscard]] std::size_t vertexCount() const { return vertices; }
    [[nodiscard]] std::size_t triangleCount() const { return triangles; }
    [[nodiscard]] std::size_t textureCount() const { return textures; }

private:
    // Fills the triangle soup and the grid that indexes it. Called once, after
    // the part transforms are final, so the triangles are in model space and a
    // query needs no matrix work.
    void buildCollision(std::vector<glm::vec3> triangleVertices);

    std::vector<ModelPart> modelParts;
    std::vector<ModelMaterial> modelMaterials;
    std::string sourcePath;
    glm::vec3 minBounds{0.0f};
    glm::vec3 maxBounds{0.0f};
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t textures = 0;

    // Collision geometry: three vertices per triangle, in model space.
    std::vector<glm::vec3> collisionVertices;
    // A uniform grid over XZ -- cells are infinite columns in Y, which suits a
    // map that is wide and flat and a ground query that always points down.
    // CSR layout: gridStart[cell]..gridStart[cell + 1] indexes gridItems, whose
    // entries are triangle indices.
    std::vector<std::uint32_t> gridStart;
    std::vector<std::uint32_t> gridItems;
    glm::vec2 gridMin{0.0f};
    glm::vec2 gridCell{1.0f};
    int gridX = 0;
    int gridZ = 0;
};
