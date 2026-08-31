#pragma once

#include <glm/glm.hpp>

class Shader;

// A camera that owns its view and projection matrices and keeps them cached.
//
// Orientation is stored as yaw/pitch/roll in degrees rather than as a matrix, so
// the scripting side can nudge angles without ever accumulating drift from
// repeated matrix multiplication. Every mutator marks the matrices dirty; they
// are rebuilt lazily the first time view() or projection() is asked for.
//
// Conventions match the shaders in assets/shaders: right-handed, -Z forward,
// +Y up. Yaw 0 / pitch 0 therefore looks down -Z.
class EngineCamera
{
public:
    enum class Projection
    {
        Perspective,
        Orthographic
    };

    EngineCamera() = default;
    EngineCamera(const glm::vec3& position, const glm::vec3& target);

    // ---- placement -------------------------------------------------------
    void setPosition(const glm::vec3& value);
    void setPosition(float x, float y, float z) { setPosition(glm::vec3(x, y, z)); }
    void move(const glm::vec3& delta);
    // Moves along the camera's own axes: +right, +up, +forward (into the screen).
    void moveLocal(float right, float up, float forward);

    // ---- orientation -----------------------------------------------------
    void setRotation(float yawDegrees, float pitchDegrees, float rollDegrees = 0.0f);
    void rotate(float deltaYaw, float deltaPitch, float deltaRoll = 0.0f);
    // Points the camera at a world-space target, keeping its position.
    void lookAt(const glm::vec3& target);
    // Rotates around a pivot at the current distance and keeps looking at it.
    void orbit(const glm::vec3& pivot, float deltaYaw, float deltaPitch);
    // Moves towards/away from a pivot without passing through it.
    void dolly(const glm::vec3& pivot, float amount);

    // Pitch is clamped to this many degrees either side of the horizon, which is
    // what stops a look-around from flipping upside down at the poles.
    void setPitchLimit(float degrees);

    // ---- projection ------------------------------------------------------
    void setPerspective(float fovDegrees, float aspect, float nearZ, float farZ);
    void setOrthographic(float height, float aspect, float nearZ, float farZ);
    void setFov(float fovDegrees);
    void setOrthoHeight(float height);
    void setAspect(float value);
    void setViewport(int width, int height);
    void setClipPlanes(float nearZ, float farZ);
    // While true, the renderer overwrites the aspect with the current viewport's
    // every frame -- turn it off for a camera that renders to a fixed target.
    void setAutoAspect(bool value) { autoAspect = value; }
    [[nodiscard]] bool usesAutoAspect() const { return autoAspect; }

    // ---- queries ---------------------------------------------------------
    [[nodiscard]] const glm::vec3& getPosition() const { return position; }
    [[nodiscard]] float getYaw() const { return yaw; }
    [[nodiscard]] float getPitch() const { return pitch; }
    [[nodiscard]] float getRoll() const { return roll; }
    [[nodiscard]] float getFov() const { return fov; }
    [[nodiscard]] float getAspect() const { return aspect; }
    [[nodiscard]] float getNear() const { return nearPlane; }
    [[nodiscard]] float getFar() const { return farPlane; }
    [[nodiscard]] Projection getProjectionMode() const { return mode; }

    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 right() const;
    [[nodiscard]] glm::vec3 up() const;

    [[nodiscard]] const glm::mat4& view() const;
    [[nodiscard]] const glm::mat4& projection() const;
    [[nodiscard]] const glm::mat4& viewProjection() const;

    // Uploads uView, uProjection and uCameraPosition, the names the stock
    // shaders use. Silently skips any the program does not declare.
    void apply(const Shader& shader) const;

private:
    void invalidateView();
    void invalidateProjection();
    void rebuild() const;

    glm::vec3 position{0.0f, 0.0f, 3.0f};
    float yaw = 0.0f;   // degrees, around +Y
    float pitch = 0.0f; // degrees, around the camera's right axis
    float roll = 0.0f;  // degrees, around the view direction
    float pitchLimit = 89.0f;

    Projection mode = Projection::Perspective;
    float fov = 60.0f;        // vertical field of view, degrees
    float orthoHeight = 2.0f; // world units covered vertically in ortho mode
    float aspect = 4.0f / 3.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
    bool autoAspect = true;

    mutable glm::mat4 viewMatrix{1.0f};
    mutable glm::mat4 projectionMatrix{1.0f};
    mutable glm::mat4 viewProjectionMatrix{1.0f};
    mutable bool viewDirty = true;
    mutable bool projectionDirty = true;
};
