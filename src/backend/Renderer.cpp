#include <Renderer.h>

#include <algorithm>
#include <iostream>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

glm::mat4 DrawCall::modelMatrix() const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    // Yaw, then pitch, then roll -- the order a scene script expects when it
    // spins an object around the world's up axis.
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, scale);
    // The part's own transform runs first, so a model's node hierarchy is
    // placed inside whatever the script asked for.
    return model * preTransform;
}

void Renderer::init()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    // The window was created with GLFW_SRGB_CAPABLE, so let the hardware do the
    // encoding on write and keep every colour in the shaders linear.
    glEnable(GL_FRAMEBUFFER_SRGB);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // A shader that always samples is simpler than one that branches, so an
    // untextured draw call gets a 1x1 white texture instead of a special case.
    defaultTexture = std::make_shared<Texture>(Texture::white());
}

void Renderer::setViewport(int newWidth, int newHeight)
{
    if (newWidth <= 0 || newHeight <= 0)
    {
        return; // a minimised window reports 0x0
    }
    if (newWidth == width && newHeight == height)
    {
        return;
    }

    width = newWidth;
    height = newHeight;
    glViewport(0, 0, width, height);
}

float Renderer::aspect() const
{
    if (width <= 0 || height <= 0)
    {
        return 1.0f;
    }
    return static_cast<float>(width) / static_cast<float>(height);
}

void Renderer::setClearColor(float r, float g, float b, float a)
{
    clearColor = glm::vec4(r, g, b, a);
}

void Renderer::setCamera(std::shared_ptr<EngineCamera> value)
{
    activeCamera = std::move(value);
}

void Renderer::beginFrame()
{
    queue.clear();
    // stats deliberately survives: lastFrameStats() has to keep reporting the
    // frame that was actually drawn, so a script reading it from update() --
    // which runs before this frame's flush() -- gets numbers instead of zeroes.

    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::submit(DrawCall call)
{
    if (!call.mesh || !call.mesh->valid() || call.instances <= 0)
    {
        return; // dropping a bad call beats a GL error every frame
    }
    queue.push_back(std::move(call));
}

void Renderer::discard()
{
    queue.clear();
}

void Renderer::shutdown()
{
    queue.clear();
    shaderCache.clear();
    modelCache.clear();
    textureCache.clear();
    defaultTexture.reset();
    activeCamera.reset();
}

std::size_t Renderer::submitModel(const std::shared_ptr<Model>& model, const DrawCall& prototype)
{
    if (!model)
    {
        return 0;
    }

    std::size_t queued = 0;
    for (const ModelPart& part : model->parts())
    {
        if (!part.mesh || !part.mesh->valid())
        {
            continue;
        }

        DrawCall call = prototype;
        call.mesh = part.mesh;
        call.preTransform = prototype.preTransform * part.transform;

        if (const ModelMaterial* material = model->materialFor(part); material != nullptr)
        {
            // The prototype's colour stays in play as a tint, so a script can
            // still fade or highlight a whole model without touching its
            // materials.
            call.color = prototype.color * material->baseColor;
            call.alpha = prototype.alpha * material->alpha;
            call.texture = material->baseColorTexture;
            call.doubleSided = prototype.doubleSided || material->doubleSided;
            call.blend = prototype.blend || material->blend;
        }

        submit(std::move(call));
        ++queued;
    }
    return queued;
}

void Renderer::flush()
{
    if (queue.empty())
    {
        stats = Stats{}; // a frame that drew nothing should report nothing
        return;
    }

    if (activeCamera && activeCamera->usesAutoAspect())
    {
        activeCamera->setAspect(aspect());
    }

    // Opaque first, then by program, then by texture. The opaque/blended split
    // is what makes transparency come out right at all; the rest turns a scene
    // of many objects into one bind per shader and per texture.
    std::stable_sort(queue.begin(), queue.end(),
                     [](const DrawCall& a, const DrawCall& b)
                     {
                         if (a.blend != b.blend)
                         {
                             return !a.blend;
                         }
                         const GLuint left = a.shader ? a.shader->id() : 0;
                         const GLuint right = b.shader ? b.shader->id() : 0;
                         if (left != right)
                         {
                             return left < right;
                         }
                         const GLuint leftTexture = a.texture ? a.texture->id() : 0;
                         const GLuint rightTexture = b.texture ? b.texture->id() : 0;
                         return leftTexture < rightTexture;
                     });

    Stats frameStats;
    GLuint boundProgram = 0;
    GLuint boundTexture = 0;
    bool culling = true;
    bool blending = false;
    for (const DrawCall& call : queue)
    {
        if (!call.shader || !call.shader->valid())
        {
            continue;
        }

        if (call.shader->id() != boundProgram)
        {
            call.shader->use();
            boundProgram = call.shader->id();
            ++frameStats.shaderBinds;

            if (activeCamera)
            {
                activeCamera->apply(*call.shader);
            }
            // The sampler binding lives in the program, not in the GL state, so
            // it has to be pointed at unit 0 again for every program.
            call.shader->set("uBaseColorTexture", 0);
        }

        // Double-sided materials are common in exported models, and a scanned
        // mesh with inconsistent winding is unreadable with culling left on.
        const bool wantCulling = !call.doubleSided;
        if (wantCulling != culling)
        {
            culling = wantCulling;
            if (culling)
            {
                glEnable(GL_CULL_FACE);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }
        }
        if (call.blend != blending)
        {
            blending = call.blend;
            if (blending)
            {
                glEnable(GL_BLEND);
                glDepthMask(GL_FALSE);
            }
            else
            {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
        }

        const std::shared_ptr<Texture>& texture = call.texture ? call.texture : defaultTexture;
        const GLuint textureId = texture ? texture->id() : 0;
        if (textureId != boundTexture)
        {
            glBindTextureUnit(0, textureId);
            boundTexture = textureId;
            ++frameStats.textureBinds;
        }
        call.shader->set("uHasTexture", call.texture != nullptr);

        const glm::mat4 model = call.modelMatrix();
        call.shader->set("uModel", model);
        // Inverse-transpose, so a non-uniformly scaled object still lights right.
        call.shader->set("uNormalMatrix", glm::inverseTranspose(glm::mat3(model)));
        call.shader->set("uBaseColor", call.color);
        call.shader->set("uAlpha", call.alpha);

        call.mesh->drawInstanced(call.instances, call.mode);

        ++frameStats.drawCalls;
        const GLsizei elements =
            call.mesh->indexed() ? call.mesh->indexCount() : call.mesh->vertexCount();
        if (call.mode == GL_TRIANGLES)
        {
            frameStats.triangles +=
                static_cast<std::size_t>(elements / 3) * static_cast<std::size_t>(call.instances);
        }
    }

    // Leave the pipeline in the state init() set up, so the next frame -- and
    // anything else drawing into this context -- starts from a known place.
    if (!culling)
    {
        glEnable(GL_CULL_FACE);
    }
    if (blending)
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    glBindVertexArray(0);
    queue.clear();
    stats = frameStats;
}

std::shared_ptr<Shader> Renderer::loadShader(const std::string& vertexPath,
                                             const std::string& fragmentPath)
{
    const std::string key = vertexPath + '|' + fragmentPath;
    for (const auto& [cachedKey, shader] : shaderCache)
    {
        if (cachedKey == key)
        {
            return shader;
        }
    }

    auto shader = std::make_shared<Shader>(Shader::fromFiles(vertexPath, fragmentPath));
    shaderCache.emplace_back(key, shader);
    return shader;
}

int Renderer::reloadShaders()
{
    int reloaded = 0;
    for (const auto& [key, shader] : shaderCache)
    {
        if (shader && shader->fileBacked() && shader->reload())
        {
            ++reloaded;
        }
    }
    return reloaded;
}

std::shared_ptr<Model> Renderer::loadModel(const std::string& path, const Model::Options& options)
{
    // The options are part of the key: the same file recentred and not is two
    // different models, and silently handing back the wrong one would be worse
    // than parsing it twice.
    const std::string key =
        path + '|' + (options.recenter ? "c" : "-") + (options.loadTextures ? "t" : "-") +
        (options.generateNormals ? "n" : "-") + '|' + std::to_string(options.scale);
    for (const auto& [cachedKey, model] : modelCache)
    {
        if (cachedKey == key)
        {
            return model;
        }
    }

    std::shared_ptr<Model> model = Model::loadFromFile(path, options);
    modelCache.emplace_back(key, model);
    return model;
}

std::shared_ptr<Texture> Renderer::loadTexture(const std::string& path, bool srgb)
{
    const std::string key = path + (srgb ? "|srgb" : "|linear");
    for (const auto& [cachedKey, texture] : textureCache)
    {
        if (cachedKey == key)
        {
            return texture;
        }
    }

    Texture::Options options;
    options.srgb = srgb;
    auto texture = std::make_shared<Texture>(Texture::fromFile(path, options));
    textureCache.emplace_back(key, texture);
    return texture;
}

void Renderer::trimCaches()
{
    const auto unused = [](const auto& entry) { return entry.second.use_count() == 1; };
    std::erase_if(modelCache, unused);
    std::erase_if(textureCache, unused);
}
