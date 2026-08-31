#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include <glad/gl.h>

// How a texture is uploaded and sampled. Declared outside Texture because a
// nested class's default member initializers are not usable in the enclosing
// class's own default arguments until that class is complete.
struct TextureOptions
{
    bool srgb = true;
    bool mipmaps = true;
    GLenum wrapS = GL_REPEAT;
    GLenum wrapT = GL_REPEAT;
    GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR;
    GLenum magFilter = GL_LINEAR;
    bool flipVertically = false; // glTF UVs already have their origin top-left
};

// A GL 2D texture with its own storage. Move-only for the same reason as Mesh
// and Shader: the destructor deletes the name, so exactly one Texture may own
// it. Create and destroy it while the GL context is current.
//
// Colour textures are uploaded as sRGB by default. The framebuffer has
// GL_FRAMEBUFFER_SRGB enabled, so everything the shaders see stays linear and
// the hardware handles both ends of the conversion. Data textures (normal maps,
// roughness, masks) must pass srgb = false or they come out wrong.
class Texture
{
public:
    using Options = TextureOptions;

    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Decodes PNG/JPEG/BMP/TGA/GIF/HDR bytes. Throws std::runtime_error when the
    // format is not recognised or the build has no image decoder.
    static Texture fromMemory(std::span<const std::byte> bytes, const Options& options = {});
    static Texture fromFile(const std::filesystem::path& path, const Options& options = {});
    // Raw pixels, 1..4 channels, 8 bits each, tightly packed, top row first.
    static Texture fromPixels(int width, int height, int channels, const std::uint8_t* pixels,
                              const Options& options = {});
    // 1x1 white. Handy as the "no texture bound" stand-in for a shader that
    // always samples, and as the fallback for a material whose image failed.
    static Texture white();

    void bind(GLuint unit = 0) const;

    [[nodiscard]] bool valid() const { return texture != 0; }
    [[nodiscard]] GLuint id() const { return texture; }
    [[nodiscard]] int getWidth() const { return width; }
    [[nodiscard]] int getHeight() const { return height; }
    [[nodiscard]] int getChannels() const { return channels; }

    // True when this build can decode compressed image files at all.
    [[nodiscard]] static bool decoderAvailable();

private:
    void destroy() noexcept;

    GLuint texture = 0;
    int width = 0;
    int height = 0;
    int channels = 0;
};
