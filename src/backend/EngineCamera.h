#pragma once

#include <glm/glm.hpp>

class Shader;

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

    
    void setPosition(const glm::vec3& value);
    void setPosition(float x, float y, float z) { setPosition(glm::vec3(x, y, z)); }
    void move(const glm::vec3& delta);
    void moveLocal(float right, float up, float forward);
    void setRotation(float yawDegrees, float pitchDegrees, float rollDegrees = 0.0f);
    void rotate(float deltaYaw, float deltaPitch, float deltaRoll = 0.0f);
    void lookAt(const glm::vec3& target);
    void orbit(const glm::vec3& pivot, float deltaYaw, float deltaPitch);
    void dolly(const glm::vec3& pivot, float amount);
    void setPitchLimit(float degrees);

    void setPerspective(float fovDegrees, float aspect, float nearZ, float farZ);
    void setOrthographic(float height, float aspect, float nearZ, float farZ);
    void setFov(float fovDegrees);
    void setOrthoHeight(float height);
    void setAspect(float value);
    void setViewport(int width, int height);
    void setClipPlanes(float nearZ, float farZ);
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

    void apply(const Shader& shader) const;

private:
    void invalidateView();
    void invalidateProjection();
    void rebuild() const;

    glm::vec3 position{0.0f, 0.0f, 3.0f};
    float yaw = 0.0f;   
    float pitch = 0.0f; 
    float roll = 0.0f; 
    float pitchLimit = 89.0f;

    Projection mode = Projection::Perspective;
    float fov = 60.0f;        
    float orthoHeight = 2.0f; 
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
