#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
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

    [[nodiscard]] std::size_t vertexCount() const { return vertices; }
    [[nodiscard]] std::size_t triangleCount() const { return triangles; }
    [[nodiscard]] std::size_t textureCount() const { return textures; }

private:
    std::vector<ModelPart> modelParts;
    std::vector<ModelMaterial> modelMaterials;
    std::string sourcePath;
    glm::vec3 minBounds{0.0f};
    glm::vec3 maxBounds{0.0f};
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t textures = 0;
};
