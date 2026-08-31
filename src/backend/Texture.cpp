#include <Texture.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef HAVE_STB_IMAGE
#include <stb_image.h>
#endif

namespace
{

// Number of mip levels a width x height image can hold, including level 0.
GLsizei mipLevels(int width, int height)
{
    GLsizei levels = 1;
    int size = std::max(width, height);
    while (size > 1)
    {
        size /= 2;
        ++levels;
    }
    return levels;
}

struct FormatPair
{
    GLenum internalFormat;
    GLenum format;
};

FormatPair formatsFor(int channels, bool srgb)
{
    switch (channels)
    {
    case 1:
        return {GL_R8, GL_RED};
    case 2:
        return {GL_RG8, GL_RG};
    case 3:
        return {static_cast<GLenum>(srgb ? GL_SRGB8 : GL_RGB8), GL_RGB};
    default:
        return {static_cast<GLenum>(srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8), GL_RGBA};
    }
}

} // namespace

Texture::~Texture()
{
    destroy();
}

Texture::Texture(Texture&& other) noexcept
    : texture(std::exchange(other.texture, 0)), width(std::exchange(other.width, 0)),
      height(std::exchange(other.height, 0)), channels(std::exchange(other.channels, 0))
{
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        texture = std::exchange(other.texture, 0);
        width = std::exchange(other.width, 0);
        height = std::exchange(other.height, 0);
        channels = std::exchange(other.channels, 0);
    }
    return *this;
}

void Texture::destroy() noexcept
{
    if (texture != 0)
    {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    width = 0;
    height = 0;
    channels = 0;
}

bool Texture::decoderAvailable()
{
#ifdef HAVE_STB_IMAGE
    return true;
#else
    return false;
#endif
}

Texture Texture::fromPixels(int pixelWidth, int pixelHeight, int pixelChannels,
                            const std::uint8_t* pixels, const Options& options)
{
    if (pixelWidth <= 0 || pixelHeight <= 0 || pixelChannels < 1 || pixelChannels > 4 ||
        pixels == nullptr)
    {
        throw std::runtime_error("Texture: invalid pixel data");
    }

    Texture result;
    result.width = pixelWidth;
    result.height = pixelHeight;
    result.channels = pixelChannels;

    const FormatPair formats = formatsFor(pixelChannels, options.srgb);
    const GLsizei levels = options.mipmaps ? mipLevels(pixelWidth, pixelHeight) : 1;

    glCreateTextures(GL_TEXTURE_2D, 1, &result.texture);
    glTextureStorage2D(result.texture, levels, formats.internalFormat, pixelWidth, pixelHeight);

    // Rows are tightly packed whatever the channel count, which the default
    // 4-byte unpack alignment would get wrong for 1- and 3-channel images.
    GLint previousAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(result.texture, 0, 0, 0, pixelWidth, pixelHeight, formats.format,
                        GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

    // A single-channel image is a mask, not a red image: broadcast it to rgb.
    if (pixelChannels == 1)
    {
        const GLint swizzle[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
        glTextureParameteriv(result.texture, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    }
    else if (pixelChannels == 2)
    {
        const GLint swizzle[4] = {GL_RED, GL_RED, GL_RED, GL_GREEN};
        glTextureParameteriv(result.texture, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    }

    glTextureParameteri(result.texture, GL_TEXTURE_WRAP_S, static_cast<GLint>(options.wrapS));
    glTextureParameteri(result.texture, GL_TEXTURE_WRAP_T, static_cast<GLint>(options.wrapT));
    glTextureParameteri(result.texture, GL_TEXTURE_MAG_FILTER,
                        static_cast<GLint>(options.magFilter));

    if (options.mipmaps)
    {
        glGenerateTextureMipmap(result.texture);
        glTextureParameteri(result.texture, GL_TEXTURE_MIN_FILTER,
                            static_cast<GLint>(options.minFilter));
        // Anisotropy is core since 4.6, and a ground plane at a grazing angle is
        // exactly the case this renderer draws most of.
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy);
        glTextureParameterf(result.texture, GL_TEXTURE_MAX_ANISOTROPY,
                            std::min(maxAnisotropy, 8.0f));
    }
    else
    {
        // A minFilter asking for mips would sample an incomplete texture.
        glTextureParameteri(result.texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    return result;
}

Texture Texture::white()
{
    const std::uint8_t pixel[4] = {255, 255, 255, 255};
    Options options;
    options.mipmaps = false;
    options.srgb = false;
    return fromPixels(1, 1, 4, pixel, options);
}

Texture Texture::fromMemory([[maybe_unused]] std::span<const std::byte> bytes,
                            [[maybe_unused]] const Options& options)
{
#ifdef HAVE_STB_IMAGE
    if (bytes.empty())
    {
        throw std::runtime_error("Texture: empty image data");
    }

    stbi_set_flip_vertically_on_load(options.flipVertically ? 1 : 0);

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            static_cast<int>(bytes.size()), &decodedWidth,
                                            &decodedHeight, &decodedChannels, 0);
    if (pixels == nullptr)
    {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error(std::string("Texture: decode failed: ") +
                                 (reason != nullptr ? reason : "unknown format"));
    }

    try
    {
        Texture result = fromPixels(decodedWidth, decodedHeight, decodedChannels, pixels, options);
        stbi_image_free(pixels);
        return result;
    }
    catch (...)
    {
        stbi_image_free(pixels);
        throw;
    }
#else
    throw std::runtime_error("Texture: this build has no image decoder (stb_image missing)");
#endif
}

Texture Texture::fromFile(const std::filesystem::path& path, const Options& options)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("Texture: cannot open '" + path.string() + '\'');
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        throw std::runtime_error("Texture: cannot read '" + path.string() + '\'');
    }

    try
    {
        return fromMemory(bytes, options);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

void Texture::bind(GLuint unit) const
{
    glBindTextureUnit(unit, texture);
}
