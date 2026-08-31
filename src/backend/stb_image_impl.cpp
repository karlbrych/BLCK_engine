// The one translation unit that compiles stb_image. Kept on its own so the
// implementation is never pulled into a header, and so CMake can drop the
// project's warning flags for this file alone.

#ifdef HAVE_STB_IMAGE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STBI_ONLY_HDR
#include <stb_image.h>
#endif
