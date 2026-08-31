#include <Shader.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef HAVE_GLM
#include <glm/gtc/type_ptr.hpp>
#endif

namespace
{

const char* stageName(GLenum type)
{
    switch (type)
    {
    case GL_VERTEX_SHADER:
        return "vertex";
    case GL_FRAGMENT_SHADER:
        return "fragment";
    case GL_GEOMETRY_SHADER:
        return "geometry";
    case GL_TESS_CONTROL_SHADER:
        return "tessellation control";
    case GL_TESS_EVALUATION_SHADER:
        return "tessellation evaluation";
    case GL_COMPUTE_SHADER:
        return "compute";
    default:
        return "unknown";
    }
}

GLenum stageFromExtension(const std::filesystem::path& path)
{
    const std::string ext = path.extension().string();
    if (ext == ".vert" || ext == ".vs")
    {
        return GL_VERTEX_SHADER;
    }
    if (ext == ".frag" || ext == ".fs")
    {
        return GL_FRAGMENT_SHADER;
    }
    if (ext == ".geom" || ext == ".gs")
    {
        return GL_GEOMETRY_SHADER;
    }
    if (ext == ".tesc")
    {
        return GL_TESS_CONTROL_SHADER;
    }
    if (ext == ".tese")
    {
        return GL_TESS_EVALUATION_SHADER;
    }
    if (ext == ".comp")
    {
        return GL_COMPUTE_SHADER;
    }
    return 0;
}

} // namespace

std::string Shader::readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Shader: cannot open '" + path.string() + "'");
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad())
    {
        throw std::runtime_error("Shader: failed while reading '" + path.string() + "'");
    }
    return contents.str();
}

GLuint Shader::compile(const Stage& stage)
{
    const GLuint shader = glCreateShader(stage.type);
    if (shader == 0)
    {
        throw std::runtime_error(std::string("Shader: glCreateShader failed for the ") +
                                 stageName(stage.type) + " stage");
    }

    const char* source = stage.source.c_str();
    const auto length = static_cast<GLint>(stage.source.size());
    glShaderSource(shader, 1, &source, &length);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<std::size_t>(logLength > 0 ? logLength : 1), '\0');
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader: ") + stageName(stage.type) +
                                 " stage failed to compile (" + stage.name + "):\n" + log.c_str());
    }

    return shader;
}

GLuint Shader::link(const std::vector<Stage>& stages)
{
    if (stages.empty())
    {
        throw std::runtime_error("Shader: no stages given");
    }

    const GLuint prog = glCreateProgram();
    if (prog == 0)
    {
        throw std::runtime_error("Shader: glCreateProgram failed");
    }

    std::vector<GLuint> compiled;
    compiled.reserve(stages.size());

    // Compilation throws on the first bad stage, so everything created up to
    // that point has to be released before the exception leaves this function.
    try
    {
        for (const Stage& stage : stages)
        {
            const GLuint shader = compile(stage);
            compiled.push_back(shader);
            glAttachShader(prog, shader);
        }

        glLinkProgram(prog);

        GLint linked = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE)
        {
            GLint logLength = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLength);
            std::string log(static_cast<std::size_t>(logLength > 0 ? logLength : 1), '\0');
            glGetProgramInfoLog(prog, static_cast<GLsizei>(log.size()), nullptr, log.data());
            throw std::runtime_error(std::string("Shader: program failed to link:\n") + log.c_str());
        }
    }
    catch (...)
    {
        for (GLuint shader : compiled)
        {
            glDetachShader(prog, shader);
            glDeleteShader(shader);
        }
        glDeleteProgram(prog);
        throw;
    }

    // The program keeps its own reference until it is deleted.
    for (GLuint shader : compiled)
    {
        glDetachShader(prog, shader);
        glDeleteShader(shader);
    }

    return prog;
}

Shader::Shader(std::vector<Stage> stages) : program(link(stages))
{
}

void Shader::destroy() noexcept
{
    if (program != 0)
    {
        glDeleteProgram(program);
        program = 0;
    }
    uniforms.clear();
}

Shader::~Shader()
{
    destroy();
}

Shader::Shader(Shader&& other) noexcept
    : program(std::exchange(other.program, 0)), files(std::move(other.files)),
      uniforms(std::move(other.uniforms))
{
    other.files.clear();
    other.uniforms.clear();
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        program = std::exchange(other.program, 0);
        files = std::move(other.files);
        uniforms = std::move(other.uniforms);
        other.files.clear();
        other.uniforms.clear();
    }
    return *this;
}

Shader Shader::fromFiles(const std::filesystem::path& vertex, const std::filesystem::path& fragment,
                         const std::filesystem::path& geometry)
{
    std::vector<std::pair<GLenum, std::filesystem::path>> paths{
        {GL_VERTEX_SHADER, vertex},
        {GL_FRAGMENT_SHADER, fragment},
    };
    if (!geometry.empty())
    {
        paths.emplace_back(GL_GEOMETRY_SHADER, geometry);
    }

    std::vector<Stage> stages;
    stages.reserve(paths.size());
    for (const auto& [type, path] : paths)
    {
        stages.push_back(Stage{type, readFile(path), path.string()});
    }

    Shader shader(std::move(stages));
    shader.files = std::move(paths);
    return shader;
}

Shader Shader::fromSource(std::string_view vertex, std::string_view fragment)
{
    std::vector<Stage> stages{
        Stage{GL_VERTEX_SHADER, std::string(vertex), "<memory:vertex>"},
        Stage{GL_FRAGMENT_SHADER, std::string(fragment), "<memory:fragment>"},
    };
    return Shader(std::move(stages));
}

Shader Shader::computeFromFile(const std::filesystem::path& compute)
{
    std::vector<Stage> stages{Stage{GL_COMPUTE_SHADER, readFile(compute), compute.string()}};

    Shader shader(std::move(stages));
    shader.files.emplace_back(GL_COMPUTE_SHADER, compute);
    return shader;
}

Shader Shader::fromPaths(std::span<const std::filesystem::path> paths)
{
    std::vector<std::pair<GLenum, std::filesystem::path>> typed;
    std::vector<Stage> stages;
    typed.reserve(paths.size());
    stages.reserve(paths.size());

    for (const std::filesystem::path& path : paths)
    {
        const GLenum type = stageFromExtension(path);
        if (type == 0)
        {
            throw std::runtime_error("Shader: cannot tell the stage of '" + path.string() +
                                     "' from its extension");
        }
        stages.push_back(Stage{type, readFile(path), path.string()});
        typed.emplace_back(type, path);
    }

    Shader shader(std::move(stages));
    shader.files = std::move(typed);
    return shader;
}

bool Shader::reload()
{
    if (files.empty())
    {
        std::cerr << "Shader: reload() needs a shader loaded from files\n";
        return false;
    }

    try
    {
        std::vector<Stage> stages;
        stages.reserve(files.size());
        for (const auto& [type, path] : files)
        {
            stages.push_back(Stage{type, readFile(path), path.string()});
        }

        // Link first: only once the new program exists do we drop the old one,
        // so a broken edit leaves the previous shader running.
        const GLuint replacement = link(stages);
        if (program != 0)
        {
            glDeleteProgram(program);
        }
        program = replacement;
        uniforms.clear();
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Shader: reload failed, keeping the previous program.\n"
                  << error.what() << '\n';
        return false;
    }
}

void Shader::use() const
{
    glUseProgram(program);
}

void Shader::unbind()
{
    glUseProgram(0);
}

void Shader::dispatch(GLuint groupsX, GLuint groupsY, GLuint groupsZ) const
{
    use();
    glDispatchCompute(groupsX, groupsY, groupsZ);
}

GLint Shader::location(std::string_view name) const
{
    if (program == 0)
    {
        return -1;
    }

    if (const auto found = uniforms.find(name); found != uniforms.end())
    {
        return found->second;
    }

    // glGetUniformLocation needs a NUL-terminated string, which string_view is not.
    const std::string key(name);
    const GLint uniform = glGetUniformLocation(program, key.c_str());
    if (uniform < 0)
    {
        // Cached as -1 as well, so an unused uniform warns once instead of every frame.
        std::cerr << "Shader: uniform '" << key << "' not found in program " << program
                  << " (unused uniforms are optimised out)\n";
    }
    uniforms.emplace(key, uniform);
    return uniform;
}

void Shader::set(std::string_view name, bool value) const
{
    glProgramUniform1i(program, location(name), value ? 1 : 0);
}

void Shader::set(std::string_view name, int value) const
{
    glProgramUniform1i(program, location(name), value);
}

void Shader::set(std::string_view name, unsigned int value) const
{
    glProgramUniform1ui(program, location(name), value);
}

void Shader::set(std::string_view name, float value) const
{
    glProgramUniform1f(program, location(name), value);
}

void Shader::set(std::string_view name, float x, float y) const
{
    glProgramUniform2f(program, location(name), x, y);
}

void Shader::set(std::string_view name, float x, float y, float z) const
{
    glProgramUniform3f(program, location(name), x, y, z);
}

void Shader::set(std::string_view name, float x, float y, float z, float w) const
{
    glProgramUniform4f(program, location(name), x, y, z, w);
}

void Shader::set(std::string_view name, std::span<const float> values) const
{
    glProgramUniform1fv(program, location(name), static_cast<GLsizei>(values.size()),
                        values.data());
}

void Shader::set(std::string_view name, std::span<const int> values) const
{
    glProgramUniform1iv(program, location(name), static_cast<GLsizei>(values.size()),
                        values.data());
}

#ifdef HAVE_GLM
void Shader::set(std::string_view name, const glm::vec2& value) const
{
    glProgramUniform2fv(program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::vec3& value) const
{
    glProgramUniform3fv(program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::vec4& value) const
{
    glProgramUniform4fv(program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::ivec2& value) const
{
    glProgramUniform2iv(program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::ivec3& value) const
{
    glProgramUniform3iv(program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::ivec4& value) const
{
    glProgramUniform4iv(program, location(name), 1, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::mat3& value) const
{
    glProgramUniformMatrix3fv(program, location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::set(std::string_view name, const glm::mat4& value) const
{
    glProgramUniformMatrix4fv(program, location(name), 1, GL_FALSE, glm::value_ptr(value));
}
#endif

void Shader::bindTexture(std::string_view name, GLuint unit, GLuint texture) const
{
    glBindTextureUnit(unit, texture);
    glProgramUniform1i(program, location(name), static_cast<GLint>(unit));
}

void Shader::bindUniformBlock(std::string_view name, GLuint bindingPoint) const
{
    const std::string key(name);
    const GLuint index = glGetUniformBlockIndex(program, key.c_str());
    if (index == GL_INVALID_INDEX)
    {
        std::cerr << "Shader: uniform block '" << key << "' not found in program " << program
                  << '\n';
        return;
    }
    glUniformBlockBinding(program, index, bindingPoint);
}
