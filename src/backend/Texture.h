#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include <glad/gl.h>

struct TextureOptions
{
    bool srgb = true;
    bool mipmaps = true;
    GLenum wrapS = GL_REPEAT;
    GLenum wrapT = GL_REPEAT;
    GLenum minFilter = GL_LINEAR_MIPMAP_LINEAR;
    GLenum magFilter = GL_LINEAR;
    bool flipVertically = false; 
};

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
