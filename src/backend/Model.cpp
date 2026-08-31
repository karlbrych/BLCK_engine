#include <Model.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace fs = std::filesystem;

namespace
{

// ------------------------------------------------------------------ JSON
//
// glTF is JSON with a strictly bounded shape, so this is a small recursive
// descent parser rather than a dependency. It keeps object members in file
// order and looks them up linearly: glTF objects have a handful of keys each,
// and avoiding a hash map per object keeps parsing a 200 KB manifest cheap.

class Json
{
public:
    enum class Type
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> elements;
    std::vector<std::pair<std::string, Json>> members;

    [[nodiscard]] bool isArray() const { return type == Type::Array; }
    [[nodiscard]] bool isObject() const { return type == Type::Object; }
    [[nodiscard]] bool isNumber() const { return type == Type::Number; }
    [[nodiscard]] bool isString() const { return type == Type::String; }

    [[nodiscard]] std::size_t size() const
    {
        return type == Type::Array ? elements.size() : members.size();
    }

    // nullptr when this is not an object or the key is absent.
    [[nodiscard]] const Json* find(std::string_view key) const
    {
        if (type != Type::Object)
        {
            return nullptr;
        }
        for (const auto& [name, value] : members)
        {
            if (name == key)
            {
                return &value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Json& at(std::size_t index) const { return elements[index]; }

    [[nodiscard]] double asNumber(double fallback = 0.0) const
    {
        return type == Type::Number ? number : fallback;
    }
    [[nodiscard]] int asInt(int fallback = 0) const
    {
        return type == Type::Number ? static_cast<int>(number) : fallback;
    }
    [[nodiscard]] bool asBool(bool fallback = false) const
    {
        return type == Type::Boolean ? boolean : fallback;
    }
    [[nodiscard]] std::string asString(std::string fallback = {}) const
    {
        return type == Type::String ? text : std::move(fallback);
    }

    static Json parse(std::string_view source);
};

class JsonParser
{
public:
    explicit JsonParser(std::string_view source) : input(source) {}

    Json parseDocument()
    {
        skipWhitespace();
        Json value = parseValue(0);
        skipWhitespace();
        if (cursor != input.size())
        {
            fail("trailing data after the top-level value");
        }
        return value;
    }

private:
    static constexpr int maxDepth = 64;

    [[noreturn]] void fail(const std::string& message) const
    {
        throw std::runtime_error("JSON at byte " + std::to_string(cursor) + ": " + message);
    }

    void skipWhitespace()
    {
        while (cursor < input.size())
        {
            const char c = input[cursor];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++cursor;
                continue;
            }
            break;
        }
    }

    char peek() const
    {
        if (cursor >= input.size())
        {
            throw std::runtime_error("JSON: unexpected end of input");
        }
        return input[cursor];
    }

    void expect(char c)
    {
        if (cursor >= input.size() || input[cursor] != c)
        {
            fail(std::string("expected '") + c + '\'');
        }
        ++cursor;
    }

    bool consumeLiteral(std::string_view literal)
    {
        if (input.compare(cursor, literal.size(), literal) == 0)
        {
            cursor += literal.size();
            return true;
        }
        return false;
    }

    static void appendUtf8(std::string& out, unsigned int codepoint)
    {
        if (codepoint < 0x80)
        {
            out += static_cast<char>(codepoint);
        }
        else if (codepoint < 0x800)
        {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint < 0x10000)
        {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    unsigned int parseHex4()
    {
        if (cursor + 4 > input.size())
        {
            fail("truncated \\u escape");
        }
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = input[cursor + static_cast<std::size_t>(i)];
            value <<= 4;
            if (c >= '0' && c <= '9')
            {
                value |= static_cast<unsigned int>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                value |= static_cast<unsigned int>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                value |= static_cast<unsigned int>(c - 'A' + 10);
            }
            else
            {
                fail("bad hex digit in \\u escape");
            }
        }
        cursor += 4;
        return value;
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        while (true)
        {
            if (cursor >= input.size())
            {
                fail("unterminated string");
            }
            const char c = input[cursor++];
            if (c == '"')
            {
                break;
            }
            if (c != '\\')
            {
                out += c;
                continue;
            }

            if (cursor >= input.size())
            {
                fail("unterminated escape");
            }
            switch (const char escape = input[cursor++])
            {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                unsigned int codepoint = parseHex4();
                // Surrogate pair: the second half is a separate \u escape.
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF && cursor + 1 < input.size() &&
                    input[cursor] == '\\' && input[cursor + 1] == 'u')
                {
                    cursor += 2;
                    const unsigned int low = parseHex4();
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else
                    {
                        appendUtf8(out, codepoint);
                        codepoint = low;
                    }
                }
                appendUtf8(out, codepoint);
                break;
            }
            default:
                fail("unknown escape sequence");
            }
        }
        return out;
    }

    double parseNumber()
    {
        const std::size_t start = cursor;
        if (cursor < input.size() && (input[cursor] == '-' || input[cursor] == '+'))
        {
            ++cursor;
        }
        while (cursor < input.size())
        {
            const char c = input[cursor];
            const bool part =
                (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
            if (!part)
            {
                break;
            }
            ++cursor;
        }

        double value = 0.0;
        const char* first = input.data() + start;
        const char* last = input.data() + cursor;
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last)
        {
            cursor = start;
            fail("malformed number");
        }
        return value;
    }

    Json parseValue(int depth)
    {
        if (depth > maxDepth)
        {
            fail("nesting too deep");
        }

        skipWhitespace();
        Json value;
        switch (peek())
        {
        case '{':
        {
            ++cursor;
            value.type = Json::Type::Object;
            skipWhitespace();
            if (peek() == '}')
            {
                ++cursor;
                break;
            }
            while (true)
            {
                skipWhitespace();
                std::string key = parseString();
                skipWhitespace();
                expect(':');
                value.members.emplace_back(std::move(key), parseValue(depth + 1));
                skipWhitespace();
                if (peek() == ',')
                {
                    ++cursor;
                    continue;
                }
                expect('}');
                break;
            }
            break;
        }
        case '[':
        {
            ++cursor;
            value.type = Json::Type::Array;
            skipWhitespace();
            if (peek() == ']')
            {
                ++cursor;
                break;
            }
            while (true)
            {
                value.elements.push_back(parseValue(depth + 1));
                skipWhitespace();
                if (peek() == ',')
                {
                    ++cursor;
                    continue;
                }
                expect(']');
                break;
            }
            break;
        }
        case '"':
            value.type = Json::Type::String;
            value.text = parseString();
            break;
        case 't':
            if (!consumeLiteral("true"))
            {
                fail("expected 'true'");
            }
            value.type = Json::Type::Boolean;
            value.boolean = true;
            break;
        case 'f':
            if (!consumeLiteral("false"))
            {
                fail("expected 'false'");
            }
            value.type = Json::Type::Boolean;
            value.boolean = false;
            break;
        case 'n':
            if (!consumeLiteral("null"))
            {
                fail("expected 'null'");
            }
            value.type = Json::Type::Null;
            break;
        default:
            value.type = Json::Type::Number;
            value.number = parseNumber();
            break;
        }
        return value;
    }

    std::string_view input;
    std::size_t cursor = 0;
};

Json Json::parse(std::string_view source)
{
    return JsonParser(source).parseDocument();
}

// ------------------------------------------------------------------ URIs

bool startsWith(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

// Percent-decoding, so a path with a space in it resolves. Anything malformed
// is passed through untouched rather than dropped.
std::string decodeUri(std::string_view uri)
{
    std::string out;
    out.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i)
    {
        if (uri[i] != '%' || i + 2 >= uri.size())
        {
            out += uri[i];
            continue;
        }
        const auto hex = [](char c) -> int
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F')
            {
                return c - 'A' + 10;
            }
            return -1;
        };
        const int high = hex(uri[i + 1]);
        const int low = hex(uri[i + 2]);
        if (high < 0 || low < 0)
        {
            out += uri[i];
            continue;
        }
        out += static_cast<char>((high << 4) | low);
        i += 2;
    }
    return out;
}

std::vector<std::byte> decodeBase64(std::string_view encoded)
{
    static constexpr auto table = []
    {
        std::array<signed char, 256> values{};
        values.fill(-1);
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (signed char i = 0; i < 64; ++i)
        {
            values[static_cast<unsigned char>(alphabet[i])] = i;
        }
        return values;
    }();

    std::vector<std::byte> out;
    out.reserve(encoded.size() / 4 * 3);

    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const char c : encoded)
    {
        if (c == '=')
        {
            break;
        }
        const signed char value = table[static_cast<unsigned char>(c)];
        if (value < 0)
        {
            continue; // whitespace and line breaks are legal inside a data URI
        }
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFFu));
        }
    }
    return out;
}

std::vector<std::byte> readWholeFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        throw std::runtime_error("cannot open '" + path.string() + '\'');
    }
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        throw std::runtime_error("cannot read '" + path.string() + '\'');
    }
    return bytes;
}

// ------------------------------------------------------------------ glTF

int componentCountFor(std::string_view type)
{
    if (type == "SCALAR")
    {
        return 1;
    }
    if (type == "VEC2")
    {
        return 2;
    }
    if (type == "VEC3")
    {
        return 3;
    }
    if (type == "VEC4" || type == "MAT2")
    {
        return 4;
    }
    if (type == "MAT3")
    {
        return 9;
    }
    if (type == "MAT4")
    {
        return 16;
    }
    return 0;
}

std::size_t componentSizeFor(int componentType)
{
    switch (componentType)
    {
    case 5120: // BYTE
    case 5121: // UNSIGNED_BYTE
        return 1;
    case 5122: // SHORT
    case 5123: // UNSIGNED_SHORT
        return 2;
    case 5125: // UNSIGNED_INT
    case 5126: // FLOAT
        return 4;
    default:
        return 0;
    }
}

// Reads one component and converts it to float, applying the glTF
// normalized-integer rules when the accessor asks for them.
float componentAsFloat(const std::byte* source, int componentType, bool normalized)
{
    switch (componentType)
    {
    case 5126:
    {
        float value = 0.0f;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    case 5120:
    {
        std::int8_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return normalized ? std::max(static_cast<float>(value) / 127.0f, -1.0f)
                          : static_cast<float>(value);
    }
    case 5121:
    {
        std::uint8_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
    }
    case 5122:
    {
        std::int16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return normalized ? std::max(static_cast<float>(value) / 32767.0f, -1.0f)
                          : static_cast<float>(value);
    }
    case 5123:
    {
        std::uint16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
    }
    case 5125:
    {
        std::uint32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return static_cast<float>(value);
    }
    default:
        return 0.0f;
    }
}

std::uint32_t componentAsIndex(const std::byte* source, int componentType)
{
    switch (componentType)
    {
    case 5121:
    {
        std::uint8_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    case 5123:
    {
        std::uint16_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    case 5125:
    {
        std::uint32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

// Everything needed to walk one accessor: where the first element sits, how far
// apart they are, and how to widen each component.
struct AccessorView
{
    const std::byte* base = nullptr;
    std::size_t stride = 0;
    std::size_t count = 0;
    int components = 0;
    int componentType = 0;
    bool normalized = false;
};

class GltfLoader
{
public:
    GltfLoader(fs::path file, const Model::Options& loadOptions)
        : path(std::move(file)), options(loadOptions), baseDirectory(path.parent_path())
    {
    }

    void run(Model& model, std::vector<ModelPart>& parts, std::vector<ModelMaterial>& materials);

private:
    void readContainer();
    void readBuffers();
    void readMaterials(std::vector<ModelMaterial>& materials);
    void walkNode(int nodeIndex, const glm::mat4& parentTransform, int depth,
                  std::vector<ModelPart>& parts);
    void readPrimitive(const Json& primitive, const glm::mat4& transform, std::string_view meshName,
                       std::vector<ModelPart>& parts);

    [[nodiscard]] const Json& array(std::string_view key) const;
    [[nodiscard]] std::optional<AccessorView> viewFor(int accessorIndex) const;
    [[nodiscard]] std::vector<float> readFloats(int accessorIndex, int wanted) const;
    [[nodiscard]] std::vector<std::uint32_t> readIndices(int accessorIndex) const;
    [[nodiscard]] std::shared_ptr<Texture> textureAt(int textureIndex, bool srgb);
    [[nodiscard]] std::vector<std::byte> bufferViewBytes(int bufferViewIndex) const;

    void warn(const std::string& message)
    {
        // One line per distinct problem: a file with 200 primitives missing the
        // same attribute should not print 200 identical warnings.
        if (warned.insert(message).second)
        {
            std::cerr << "Model: " << path.filename().string() << ": " << message << '\n';
        }
    }

    fs::path path;
    Model::Options options;
    fs::path baseDirectory;

    std::vector<std::byte> fileBytes;
    std::span<const std::byte> binaryChunk;
    Json root;
    static inline const Json emptyJson{};

    std::vector<std::vector<std::byte>> ownedBuffers;
    std::vector<std::span<const std::byte>> buffers;
    std::vector<std::shared_ptr<Texture>> textureCache;

    std::set<std::string> warned;

public:
    glm::vec3 minBounds{std::numeric_limits<float>::max()};
    glm::vec3 maxBounds{std::numeric_limits<float>::lowest()};
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t loadedTextures = 0;
};

const Json& GltfLoader::array(std::string_view key) const
{
    const Json* value = root.find(key);
    return (value != nullptr && value->isArray()) ? *value : emptyJson;
}

void GltfLoader::readContainer()
{
    fileBytes = readWholeFile(path);
    if (fileBytes.size() < 4)
    {
        throw std::runtime_error("file is too small to be a glTF document");
    }

    std::uint32_t magic = 0;
    std::memcpy(&magic, fileBytes.data(), sizeof(magic));

    if (magic == 0x46546C67u) // "glTF": the binary container
    {
        if (fileBytes.size() < 12)
        {
            throw std::runtime_error("truncated GLB header");
        }
        std::uint32_t version = 0;
        std::uint32_t totalLength = 0;
        std::memcpy(&version, fileBytes.data() + 4, sizeof(version));
        std::memcpy(&totalLength, fileBytes.data() + 8, sizeof(totalLength));
        if (version != 2)
        {
            throw std::runtime_error("GLB version " + std::to_string(version) +
                                     " is not supported (only glTF 2.0)");
        }
        if (totalLength > fileBytes.size())
        {
            throw std::runtime_error("GLB claims to be longer than the file on disk");
        }

        std::string json;
        std::size_t cursor = 12;
        while (cursor + 8 <= totalLength)
        {
            std::uint32_t chunkLength = 0;
            std::uint32_t chunkType = 0;
            std::memcpy(&chunkLength, fileBytes.data() + cursor, sizeof(chunkLength));
            std::memcpy(&chunkType, fileBytes.data() + cursor + 4, sizeof(chunkType));
            cursor += 8;
            if (chunkLength > totalLength - cursor)
            {
                throw std::runtime_error("GLB chunk runs past the end of the file");
            }

            if (chunkType == 0x4E4F534Au) // "JSON"
            {
                json.assign(reinterpret_cast<const char*>(fileBytes.data() + cursor), chunkLength);
            }
            else if (chunkType == 0x004E4942u) // "BIN"
            {
                binaryChunk = std::span<const std::byte>(fileBytes.data() + cursor, chunkLength);
            }
            // Unknown chunk types are skipped by design; the spec requires it.

            cursor += chunkLength;
            cursor += (4 - (chunkLength % 4)) % 4; // chunks are 4-byte aligned
        }

        if (json.empty())
        {
            throw std::runtime_error("GLB has no JSON chunk");
        }
        root = Json::parse(json);
    }
    else
    {
        const std::string_view text(reinterpret_cast<const char*>(fileBytes.data()),
                                    fileBytes.size());
        // Check before parsing, so pointing the loader at the wrong file says so
        // instead of reporting whatever the JSON parser tripped over first.
        const std::size_t start = text.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos || text[start] != '{')
        {
            throw std::runtime_error("not a glTF document: no 'glTF' magic and no JSON object");
        }
        root = Json::parse(text);
    }

    if (!root.isObject())
    {
        throw std::runtime_error("the glTF root is not a JSON object");
    }

    if (const Json* required = root.find("extensionsRequired"); required != nullptr)
    {
        for (const Json& extension : required->elements)
        {
            // Refusing here beats loading a file that would silently come out
            // as garbage geometry -- Draco-compressed buffers in particular.
            throw std::runtime_error("requires the unsupported extension '" + extension.asString() +
                                     '\'');
        }
    }
}

void GltfLoader::readBuffers()
{
    const Json& declared = array("buffers");
    buffers.resize(declared.size());
    ownedBuffers.resize(declared.size());

    for (std::size_t i = 0; i < declared.size(); ++i)
    {
        const Json& buffer = declared.at(i);
        const Json* uri = buffer.find("uri");

        if (uri == nullptr || !uri->isString())
        {
            // No URI means the GLB binary chunk, which only buffer 0 may claim.
            if (binaryChunk.empty())
            {
                throw std::runtime_error("buffer " + std::to_string(i) +
                                         " has no uri and there is no binary chunk");
            }
            buffers[i] = binaryChunk;
            continue;
        }

        const std::string value = uri->text;
        if (startsWith(value, "data:"))
        {
            const std::size_t comma = value.find(',');
            if (comma == std::string::npos)
            {
                throw std::runtime_error("malformed data URI on buffer " + std::to_string(i));
            }
            ownedBuffers[i] = decodeBase64(std::string_view(value).substr(comma + 1));
        }
        else
        {
            ownedBuffers[i] = readWholeFile(baseDirectory / decodeUri(value));
        }
        buffers[i] = ownedBuffers[i];
    }
}

std::vector<std::byte> GltfLoader::bufferViewBytes(int bufferViewIndex) const
{
    const Json& views = array("bufferViews");
    if (bufferViewIndex < 0 || static_cast<std::size_t>(bufferViewIndex) >= views.size())
    {
        return {};
    }

    const Json& view = views.at(static_cast<std::size_t>(bufferViewIndex));
    const int bufferIndex = view.find("buffer") != nullptr ? view.find("buffer")->asInt(-1) : -1;
    if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= buffers.size())
    {
        return {};
    }

    const std::span<const std::byte> buffer = buffers[static_cast<std::size_t>(bufferIndex)];
    const auto offset = static_cast<std::size_t>(
        view.find("byteOffset") != nullptr ? view.find("byteOffset")->asNumber(0.0) : 0.0);
    const auto length = static_cast<std::size_t>(
        view.find("byteLength") != nullptr ? view.find("byteLength")->asNumber(0.0) : 0.0);
    if (offset + length > buffer.size())
    {
        return {};
    }

    return std::vector<std::byte>(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                                  buffer.begin() + static_cast<std::ptrdiff_t>(offset + length));
}

std::optional<AccessorView> GltfLoader::viewFor(int accessorIndex) const
{
    const Json& accessors = array("accessors");
    if (accessorIndex < 0 || static_cast<std::size_t>(accessorIndex) >= accessors.size())
    {
        return std::nullopt;
    }

    const Json& accessor = accessors.at(static_cast<std::size_t>(accessorIndex));
    AccessorView result;
    result.count = static_cast<std::size_t>(
        accessor.find("count") != nullptr ? accessor.find("count")->asNumber(0.0) : 0.0);
    result.componentType =
        accessor.find("componentType") != nullptr ? accessor.find("componentType")->asInt(0) : 0;
    result.normalized =
        accessor.find("normalized") != nullptr && accessor.find("normalized")->asBool(false);
    result.components = componentCountFor(
        accessor.find("type") != nullptr ? std::string_view(accessor.find("type")->text) : "");

    const std::size_t componentSize = componentSizeFor(result.componentType);
    if (result.components == 0 || componentSize == 0 || result.count == 0)
    {
        return std::nullopt;
    }

    const Json* viewIndex = accessor.find("bufferView");
    if (viewIndex == nullptr)
    {
        return std::nullopt; // a buffer-view-less accessor is all zeroes
    }

    const Json& views = array("bufferViews");
    const int index = viewIndex->asInt(-1);
    if (index < 0 || static_cast<std::size_t>(index) >= views.size())
    {
        return std::nullopt;
    }

    const Json& view = views.at(static_cast<std::size_t>(index));
    const int bufferIndex = view.find("buffer") != nullptr ? view.find("buffer")->asInt(-1) : -1;
    if (bufferIndex < 0 || static_cast<std::size_t>(bufferIndex) >= buffers.size())
    {
        return std::nullopt;
    }

    const std::span<const std::byte> buffer = buffers[static_cast<std::size_t>(bufferIndex)];
    const auto viewOffset = static_cast<std::size_t>(
        view.find("byteOffset") != nullptr ? view.find("byteOffset")->asNumber(0.0) : 0.0);
    const auto accessorOffset = static_cast<std::size_t>(
        accessor.find("byteOffset") != nullptr ? accessor.find("byteOffset")->asNumber(0.0) : 0.0);

    const std::size_t tight = componentSize * static_cast<std::size_t>(result.components);
    const auto declaredStride = static_cast<std::size_t>(
        view.find("byteStride") != nullptr ? view.find("byteStride")->asNumber(0.0) : 0.0);
    result.stride = declaredStride != 0 ? declaredStride : tight;

    const std::size_t start = viewOffset + accessorOffset;
    const std::size_t span = result.stride * (result.count - 1) + tight;
    if (start + span > buffer.size())
    {
        return std::nullopt;
    }

    result.base = buffer.data() + start;
    return result;
}

std::vector<float> GltfLoader::readFloats(int accessorIndex, int wanted) const
{
    const std::optional<AccessorView> view = viewFor(accessorIndex);
    if (!view)
    {
        return {};
    }

    const std::size_t componentSize = componentSizeFor(view->componentType);
    std::vector<float> out(view->count * static_cast<std::size_t>(wanted), 0.0f);
    const int copied = std::min(wanted, view->components);

    for (std::size_t element = 0; element < view->count; ++element)
    {
        const std::byte* source = view->base + element * view->stride;
        for (int component = 0; component < copied; ++component)
        {
            out[element * static_cast<std::size_t>(wanted) + static_cast<std::size_t>(component)] =
                componentAsFloat(source + static_cast<std::size_t>(component) * componentSize,
                                 view->componentType, view->normalized);
        }
    }
    return out;
}

std::vector<std::uint32_t> GltfLoader::readIndices(int accessorIndex) const
{
    const std::optional<AccessorView> view = viewFor(accessorIndex);
    if (!view || view->components != 1)
    {
        return {};
    }

    std::vector<std::uint32_t> out(view->count, 0);
    for (std::size_t element = 0; element < view->count; ++element)
    {
        out[element] = componentAsIndex(view->base + element * view->stride, view->componentType);
    }
    return out;
}

std::shared_ptr<Texture> GltfLoader::textureAt(int textureIndex, bool srgb)
{
    const Json& textures = array("textures");
    if (!options.loadTextures || textureIndex < 0 ||
        static_cast<std::size_t>(textureIndex) >= textures.size())
    {
        return nullptr;
    }

    textureCache.resize(textures.size());
    std::shared_ptr<Texture>& cached = textureCache[static_cast<std::size_t>(textureIndex)];
    if (cached)
    {
        return cached; // several materials commonly share one image
    }

    if (!Texture::decoderAvailable())
    {
        warn("built without an image decoder, materials fall back to their base colour");
        return nullptr;
    }

    const Json& texture = textures.at(static_cast<std::size_t>(textureIndex));
    const Json* sourceIndex = texture.find("source");
    if (sourceIndex == nullptr)
    {
        return nullptr;
    }

    const Json& images = array("images");
    const int imageIndex = sourceIndex->asInt(-1);
    if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= images.size())
    {
        return nullptr;
    }
    const Json& image = images.at(static_cast<std::size_t>(imageIndex));

    Texture::Options textureOptions;
    textureOptions.srgb = srgb;
    // glTF puts the UV origin at the top-left of the image, which is also where
    // an undecoded scanline order puts it -- so no flip, unlike the usual OBJ.
    textureOptions.flipVertically = false;

    // The sampler is optional; glTF's default is repeat with mipmapped linear.
    if (const Json* samplerIndex = texture.find("sampler"); samplerIndex != nullptr)
    {
        const Json& samplers = array("samplers");
        const int index = samplerIndex->asInt(-1);
        if (index >= 0 && static_cast<std::size_t>(index) < samplers.size())
        {
            const Json& sampler = samplers.at(static_cast<std::size_t>(index));
            if (const Json* wrapS = sampler.find("wrapS"); wrapS != nullptr)
            {
                textureOptions.wrapS = static_cast<GLenum>(wrapS->asInt(GL_REPEAT));
            }
            if (const Json* wrapT = sampler.find("wrapT"); wrapT != nullptr)
            {
                textureOptions.wrapT = static_cast<GLenum>(wrapT->asInt(GL_REPEAT));
            }
            if (const Json* magFilter = sampler.find("magFilter"); magFilter != nullptr)
            {
                textureOptions.magFilter = static_cast<GLenum>(magFilter->asInt(GL_LINEAR));
            }
            if (const Json* minFilter = sampler.find("minFilter"); minFilter != nullptr)
            {
                const auto filter = static_cast<GLenum>(minFilter->asInt(GL_LINEAR));
                textureOptions.mipmaps = filter != GL_NEAREST && filter != GL_LINEAR;
                textureOptions.minFilter = textureOptions.mipmaps ? filter : GL_LINEAR;
            }
        }
    }

    try
    {
        std::vector<std::byte> bytes;
        if (const Json* viewIndex = image.find("bufferView"); viewIndex != nullptr)
        {
            bytes = bufferViewBytes(viewIndex->asInt(-1));
        }
        else if (const Json* uri = image.find("uri"); uri != nullptr && uri->isString())
        {
            if (startsWith(uri->text, "data:"))
            {
                const std::size_t comma = uri->text.find(',');
                if (comma != std::string::npos)
                {
                    bytes = decodeBase64(std::string_view(uri->text).substr(comma + 1));
                }
            }
            else
            {
                cached = std::make_shared<Texture>(
                    Texture::fromFile(baseDirectory / decodeUri(uri->text), textureOptions));
                ++loadedTextures;
                return cached;
            }
        }

        if (bytes.empty())
        {
            warn("image " + std::to_string(imageIndex) + " has no readable data");
            return nullptr;
        }

        cached = std::make_shared<Texture>(Texture::fromMemory(bytes, textureOptions));
        ++loadedTextures;
    }
    catch (const std::exception& error)
    {
        // A single bad image must not take the whole map down with it.
        warn(std::string("image ") + std::to_string(imageIndex) + " failed: " + error.what());
        cached.reset();
    }
    return cached;
}

void GltfLoader::readMaterials(std::vector<ModelMaterial>& materials)
{
    const Json& declared = array("materials");
    materials.reserve(declared.size());

    for (std::size_t i = 0; i < declared.size(); ++i)
    {
        const Json& source = declared.at(i);
        ModelMaterial material;
        material.name = source.find("name") != nullptr ? source.find("name")->asString()
                                                       : "material " + std::to_string(i);
        material.doubleSided =
            source.find("doubleSided") != nullptr && source.find("doubleSided")->asBool(false);

        if (const Json* alphaMode = source.find("alphaMode"); alphaMode != nullptr)
        {
            // MASK is treated as BLEND: close enough for foliage cards, and it
            // avoids an alpha-test uniform the shader does not have yet.
            material.blend = alphaMode->text == "BLEND" || alphaMode->text == "MASK";
        }

        if (const Json* pbr = source.find("pbrMetallicRoughness"); pbr != nullptr)
        {
            if (const Json* factor = pbr->find("baseColorFactor");
                factor != nullptr && factor->isArray() && factor->size() >= 3)
            {
                material.baseColor = glm::vec3(static_cast<float>(factor->at(0).asNumber(1.0)),
                                               static_cast<float>(factor->at(1).asNumber(1.0)),
                                               static_cast<float>(factor->at(2).asNumber(1.0)));
                if (factor->size() >= 4)
                {
                    material.alpha = static_cast<float>(factor->at(3).asNumber(1.0));
                }
            }
            if (const Json* metallic = pbr->find("metallicFactor"); metallic != nullptr)
            {
                material.metallic = static_cast<float>(metallic->asNumber(1.0));
            }
            if (const Json* roughness = pbr->find("roughnessFactor"); roughness != nullptr)
            {
                material.roughness = static_cast<float>(roughness->asNumber(1.0));
            }
            if (const Json* baseColorTexture = pbr->find("baseColorTexture");
                baseColorTexture != nullptr)
            {
                if (const Json* index = baseColorTexture->find("index"); index != nullptr)
                {
                    material.baseColorTexture = textureAt(index->asInt(-1), true);
                }
                if (const Json* uv = baseColorTexture->find("texCoord");
                    uv != nullptr && uv->asInt(0) != 0)
                {
                    warn("only TEXCOORD_0 is sampled; a material asks for another UV set");
                }
            }
        }

        if (material.alpha < 1.0f)
        {
            material.blend = true;
        }
        materials.push_back(std::move(material));
    }
}

void GltfLoader::readPrimitive(const Json& primitive, const glm::mat4& transform,
                               std::string_view meshName, std::vector<ModelPart>& parts)
{
    if (const Json* mode = primitive.find("mode"); mode != nullptr && mode->asInt(4) != 4)
    {
        warn("skipping a primitive that is not a triangle list");
        return;
    }
    if (primitive.find("extensions") != nullptr)
    {
        warn("a primitive carries extensions that are ignored (compressed geometry will be lost)");
    }

    const Json* attributes = primitive.find("attributes");
    if (attributes == nullptr)
    {
        return;
    }

    const auto attribute = [&](const char* name) -> int
    {
        const Json* value = attributes->find(name);
        return value != nullptr ? value->asInt(-1) : -1;
    };

    const std::vector<float> positions = readFloats(attribute("POSITION"), 3);
    if (positions.empty())
    {
        warn("skipping a primitive with no POSITION data");
        return;
    }

    const std::size_t vertexCount = positions.size() / 3;
    std::vector<float> normals = readFloats(attribute("NORMAL"), 3);
    const std::vector<float> uvs = readFloats(attribute("TEXCOORD_0"), 2);

    std::vector<std::uint32_t> indices;
    if (const Json* indexAccessor = primitive.find("indices"); indexAccessor != nullptr)
    {
        indices = readIndices(indexAccessor->asInt(-1));
    }
    if (indices.empty())
    {
        // A non-indexed primitive draws its vertices in order; giving it an
        // index buffer anyway keeps one code path in Mesh and in the renderer.
        indices.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            indices[i] = static_cast<std::uint32_t>(i);
        }
    }

    std::vector<Vertex> vertexData(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i)
    {
        vertexData[i].position = Vec3{positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
        if (normals.size() >= (i + 1) * 3)
        {
            vertexData[i].normal = Vec3{normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]};
        }
        if (uvs.size() >= (i + 1) * 2)
        {
            vertexData[i].uv = Vec2{uvs[i * 2], uvs[i * 2 + 1]};
        }
    }

    if (normals.empty() && options.generateNormals)
    {
        // Flat normals, accumulated per face. Not smooth, but a mesh with no
        // normals lit by a black constant is worse.
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const std::uint32_t a = indices[i];
            const std::uint32_t b = indices[i + 1];
            const std::uint32_t c = indices[i + 2];
            if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
            {
                continue;
            }
            const glm::vec3 edge1 =
                glm::vec3(vertexData[b].position) - glm::vec3(vertexData[a].position);
            const glm::vec3 edge2 =
                glm::vec3(vertexData[c].position) - glm::vec3(vertexData[a].position);
            const glm::vec3 faceNormal = glm::cross(edge1, edge2);
            for (const std::uint32_t index : {a, b, c})
            {
                vertexData[index].normal = Vec3(glm::vec3(vertexData[index].normal) + faceNormal);
            }
        }
        for (Vertex& vertex : vertexData)
        {
            const glm::vec3 normal = glm::vec3(vertex.normal);
            vertex.normal =
                glm::length(normal) > 1e-8f ? Vec3(glm::normalize(normal)) : Vec3{0.0f, 1.0f, 0.0f};
        }
    }

    // Drop indices that point past the end rather than letting the GPU read
    // out of bounds; a truncated export should still show what it does have.
    const std::size_t before = indices.size();
    std::erase_if(indices, [&](std::uint32_t index) { return index >= vertexCount; });
    if (indices.size() != before)
    {
        warn("dropped out-of-range indices");
        indices.resize(indices.size() - indices.size() % 3);
    }
    if (indices.empty())
    {
        return;
    }

    ModelPart part;
    part.name = std::string(meshName);
    part.transform = transform;
    part.triangles = indices.size() / 3;
    if (const Json* material = primitive.find("material"); material != nullptr)
    {
        part.material = material->asInt(-1);
    }
    part.mesh = std::make_shared<Mesh>(std::span<const Vertex>(vertexData),
                                       std::span<const std::uint32_t>(indices));

    // Bounds from the local box's eight corners: a transformed AABB, not a
    // per-vertex sweep, which is plenty for framing a camera on the model.
    glm::vec3 localMin(std::numeric_limits<float>::max());
    glm::vec3 localMax(std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : vertexData)
    {
        localMin = glm::min(localMin, glm::vec3(vertex.position));
        localMax = glm::max(localMax, glm::vec3(vertex.position));
    }
    for (int corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 point((corner & 1) ? localMax.x : localMin.x,
                              (corner & 2) ? localMax.y : localMin.y,
                              (corner & 4) ? localMax.z : localMin.z);
        const glm::vec3 world = glm::vec3(transform * glm::vec4(point, 1.0f));
        minBounds = glm::min(minBounds, world);
        maxBounds = glm::max(maxBounds, world);
    }

    vertices += vertexCount;
    triangles += part.triangles;
    parts.push_back(std::move(part));
}

void GltfLoader::walkNode(int nodeIndex, const glm::mat4& parentTransform, int depth,
                          std::vector<ModelPart>& parts)
{
    const Json& nodes = array("nodes");
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodes.size())
    {
        return;
    }
    if (depth > 128)
    {
        warn("node hierarchy is deeper than 128 levels, or contains a cycle");
        return;
    }

    const Json& node = nodes.at(static_cast<std::size_t>(nodeIndex));

    glm::mat4 local(1.0f);
    if (const Json* matrix = node.find("matrix");
        matrix != nullptr && matrix->isArray() && matrix->size() == 16)
    {
        // glTF matrices are column-major, which is glm's layout too.
        float values[16] = {};
        for (std::size_t i = 0; i < 16; ++i)
        {
            values[i] = static_cast<float>(matrix->at(i).asNumber(0.0));
        }
        std::memcpy(&local[0][0], values, sizeof(values));
    }
    else
    {
        glm::vec3 translation(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale(1.0f);

        if (const Json* value = node.find("translation");
            value != nullptr && value->isArray() && value->size() >= 3)
        {
            translation = glm::vec3(static_cast<float>(value->at(0).asNumber(0.0)),
                                    static_cast<float>(value->at(1).asNumber(0.0)),
                                    static_cast<float>(value->at(2).asNumber(0.0)));
        }
        if (const Json* value = node.find("rotation");
            value != nullptr && value->isArray() && value->size() >= 4)
        {
            // glTF stores quaternions xyzw; glm's constructor takes wxyz.
            rotation = glm::quat(static_cast<float>(value->at(3).asNumber(1.0)),
                                 static_cast<float>(value->at(0).asNumber(0.0)),
                                 static_cast<float>(value->at(1).asNumber(0.0)),
                                 static_cast<float>(value->at(2).asNumber(0.0)));
        }
        if (const Json* value = node.find("scale");
            value != nullptr && value->isArray() && value->size() >= 3)
        {
            scale = glm::vec3(static_cast<float>(value->at(0).asNumber(1.0)),
                              static_cast<float>(value->at(1).asNumber(1.0)),
                              static_cast<float>(value->at(2).asNumber(1.0)));
        }

        local = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1.0f), scale);
    }

    const glm::mat4 world = parentTransform * local;

    if (const Json* meshIndex = node.find("mesh"); meshIndex != nullptr)
    {
        const Json& meshes = array("meshes");
        const int index = meshIndex->asInt(-1);
        if (index >= 0 && static_cast<std::size_t>(index) < meshes.size())
        {
            const Json& mesh = meshes.at(static_cast<std::size_t>(index));
            const std::string meshName =
                mesh.find("name") != nullptr ? mesh.find("name")->asString() : "mesh";
            if (const Json* primitives = mesh.find("primitives");
                primitives != nullptr && primitives->isArray())
            {
                for (const Json& primitive : primitives->elements)
                {
                    readPrimitive(primitive, world, meshName, parts);
                }
            }
        }
    }

    if (const Json* children = node.find("children"); children != nullptr && children->isArray())
    {
        for (const Json& child : children->elements)
        {
            walkNode(child.asInt(-1), world, depth + 1, parts);
        }
    }
}

void GltfLoader::run(Model& model, std::vector<ModelPart>& parts,
                     std::vector<ModelMaterial>& materials)
{
    (void)model;

    readContainer();
    readBuffers();
    readMaterials(materials);

    // Fall back to walking every node when the file names no scene: a few
    // exporters leave the scene list out and still expect their nodes drawn.
    const Json& scenes = array("scenes");
    const int defaultScene = root.find("scene") != nullptr ? root.find("scene")->asInt(0) : 0;

    if (defaultScene >= 0 && static_cast<std::size_t>(defaultScene) < scenes.size())
    {
        const Json& scene = scenes.at(static_cast<std::size_t>(defaultScene));
        if (const Json* nodes = scene.find("nodes"); nodes != nullptr && nodes->isArray())
        {
            for (const Json& node : nodes->elements)
            {
                walkNode(node.asInt(-1), glm::mat4(1.0f), 0, parts);
            }
        }
    }
    else
    {
        const Json& nodes = array("nodes");
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            walkNode(static_cast<int>(i), glm::mat4(1.0f), 0, parts);
        }
    }

    if (parts.empty())
    {
        throw std::runtime_error("no drawable geometry found");
    }
}

} // namespace

const ModelMaterial* Model::materialFor(const ModelPart& part) const
{
    if (part.material < 0 || static_cast<std::size_t>(part.material) >= modelMaterials.size())
    {
        return nullptr;
    }
    return &modelMaterials[static_cast<std::size_t>(part.material)];
}

float Model::radius() const
{
    return glm::length(size()) * 0.5f;
}

std::shared_ptr<Model> Model::loadFromFile(const fs::path& path, const Options& options)
{
    auto model = std::make_shared<Model>();
    model->sourcePath = path.string();

    GltfLoader loader(path, options);
    try
    {
        loader.run(*model, model->modelParts, model->modelMaterials);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error("Model '" + path.string() + "': " + error.what());
    }

    model->minBounds = loader.minBounds;
    model->maxBounds = loader.maxBounds;
    model->vertices = loader.vertices;
    model->triangles = loader.triangles;
    model->textures = loader.loadedTextures;

    // recenter and scale are baked into the part transforms rather than into
    // the vertex data: the meshes are already on the GPU by this point, and a
    // matrix costs nothing extra to apply.
    const glm::vec3 offset = options.recenter ? -model->center() : glm::vec3(0.0f);
    if (options.recenter || options.scale != 1.0f)
    {
        const glm::mat4 adjust = glm::scale(glm::mat4(1.0f), glm::vec3(options.scale)) *
                                 glm::translate(glm::mat4(1.0f), offset);
        for (ModelPart& part : model->modelParts)
        {
            part.transform = adjust * part.transform;
        }
        model->minBounds = (model->minBounds + offset) * options.scale;
        model->maxBounds = (model->maxBounds + offset) * options.scale;
    }

    if (options.verbose)
    {
        const glm::vec3 extent = model->size();
        std::cout << "Model: loaded " << path.filename().string() << " -- "
                  << model->modelParts.size() << " part(s), " << model->triangles << " triangles, "
                  << model->modelMaterials.size() << " material(s), " << model->textures
                  << " texture(s), size " << extent.x << " x " << extent.y << " x " << extent.z
                  << '\n';
    }

    return model;
}
