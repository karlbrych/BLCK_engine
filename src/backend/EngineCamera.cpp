#include <EngineCamera.h>

#include <Shader.h>

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace
{

constexpr float kMinAspect = 0.0001f;

// Wrapping keeps yaw finite no matter how long the camera spins in one
// direction, which matters once a script drives it from mouse deltas.
float wrapDegrees(float degrees)
{
    degrees = std::fmod(degrees, 360.0f);
    if (degrees < 0.0f)
    {
        degrees += 360.0f;
    }
    return degrees;
}

} // namespace

EngineCamera::EngineCamera(const glm::vec3& position, const glm::vec3& target) : position(position)
{
    lookAt(target);
}

void EngineCamera::invalidateView()
{
    viewDirty = true;
}

void EngineCamera::invalidateProjection()
{
    projectionDirty = true;
}

// ---------------------------------------------------------------- placement

void EngineCamera::setPosition(const glm::vec3& value)
{
    position = value;
    invalidateView();
}

void EngineCamera::move(const glm::vec3& delta)
{
    position += delta;
    invalidateView();
}

void EngineCamera::moveLocal(float rightAmount, float upAmount, float forwardAmount)
{
    position += right() * rightAmount + up() * upAmount + forward() * forwardAmount;
    invalidateView();
}

// ---------------------------------------------------------------- orientation

void EngineCamera::setRotation(float yawDegrees, float pitchDegrees, float rollDegrees)
{
    yaw = wrapDegrees(yawDegrees);
    pitch = std::clamp(pitchDegrees, -pitchLimit, pitchLimit);
    roll = wrapDegrees(rollDegrees);
    invalidateView();
}

void EngineCamera::rotate(float deltaYaw, float deltaPitch, float deltaRoll)
{
    setRotation(yaw + deltaYaw, pitch + deltaPitch, roll + deltaRoll);
}

void EngineCamera::lookAt(const glm::vec3& target)
{
    const glm::vec3 direction = target - position;
    const float lengthSquared = glm::dot(direction, direction);
    if (lengthSquared < 1e-12f)
    {
        return; // a target on top of the camera has no direction to derive
    }

    const glm::vec3 unit = direction / std::sqrt(lengthSquared);
    // Inverse of forward(): -Z at yaw 0, +Y as the pitch axis.
    setRotation(glm::degrees(std::atan2(unit.x, -unit.z)), glm::degrees(std::asin(unit.y)), roll);
}

void EngineCamera::orbit(const glm::vec3& pivot, float deltaYaw, float deltaPitch)
{
    const float distance = glm::length(position - pivot);
    rotate(deltaYaw, deltaPitch);
    // Re-derive the position from the new angles so the radius stays exact.
    position = pivot - forward() * distance;
    invalidateView();
}

void EngineCamera::dolly(const glm::vec3& pivot, float amount)
{
    const glm::vec3 offset = position - pivot;
    const float distance = glm::length(offset);
    if (distance < 1e-6f)
    {
        return;
    }

    // Clamped so the camera never lands on -- or tunnels through -- the pivot.
    const float target = std::max(distance - amount, nearPlane * 2.0f);
    position = pivot + offset * (target / distance);
    invalidateView();
}

void EngineCamera::setPitchLimit(float degrees)
{
    pitchLimit = std::clamp(degrees, 0.0f, 89.9f);
    pitch = std::clamp(pitch, -pitchLimit, pitchLimit);
    invalidateView();
}

// ---------------------------------------------------------------- projection

void EngineCamera::setPerspective(float fovDegrees, float aspectRatio, float nearZ, float farZ)
{
    mode = Projection::Perspective;
    fov = std::clamp(fovDegrees, 1.0f, 179.0f);
    aspect = std::max(aspectRatio, kMinAspect);
    setClipPlanes(nearZ, farZ);
}

void EngineCamera::setOrthographic(float height, float aspectRatio, float nearZ, float farZ)
{
    mode = Projection::Orthographic;
    orthoHeight = std::max(height, 1e-4f);
    aspect = std::max(aspectRatio, kMinAspect);
    setClipPlanes(nearZ, farZ);
}

void EngineCamera::setFov(float fovDegrees)
{
    fov = std::clamp(fovDegrees, 1.0f, 179.0f);
    invalidateProjection();
}

void EngineCamera::setOrthoHeight(float height)
{
    orthoHeight = std::max(height, 1e-4f);
    invalidateProjection();
}

void EngineCamera::setAspect(float value)
{
    const float clamped = std::max(value, kMinAspect);
    if (clamped != aspect)
    {
        aspect = clamped;
        invalidateProjection();
    }
}

void EngineCamera::setViewport(int width, int height)
{
    if (width > 0 && height > 0)
    {
        setAspect(static_cast<float>(width) / static_cast<float>(height));
    }
}

void EngineCamera::setClipPlanes(float nearZ, float farZ)
{
    // A zero or inverted range would produce a degenerate projection matrix.
    nearPlane = std::max(nearZ, 1e-4f);
    farPlane = std::max(farZ, nearPlane * 1.001f);
    invalidateProjection();
}

// ---------------------------------------------------------------- queries

glm::vec3 EngineCamera::forward() const
{
    const float yawRadians = glm::radians(yaw);
    const float pitchRadians = glm::radians(pitch);
    const float cosPitch = std::cos(pitchRadians);
    return glm::vec3(cosPitch * std::sin(yawRadians), std::sin(pitchRadians),
                     -cosPitch * std::cos(yawRadians));
}

glm::vec3 EngineCamera::right() const
{
    const glm::vec3 axis = glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    const float lengthSquared = glm::dot(axis, axis);
    if (lengthSquared < 1e-12f)
    {
        // Looking straight up or down: fall back to a yaw-only right vector.
        const float yawRadians = glm::radians(yaw);
        return glm::vec3(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
    }
    return axis / std::sqrt(lengthSquared);
}

glm::vec3 EngineCamera::up() const
{
    const glm::vec3 unrolled = glm::cross(right(), forward());
    if (roll == 0.0f)
    {
        return unrolled;
    }
    const float rollRadians = glm::radians(roll);
    return glm::normalize(unrolled * std::cos(rollRadians) + right() * std::sin(rollRadians));
}

void EngineCamera::rebuild() const
{
    if (viewDirty)
    {
        viewMatrix = glm::lookAt(position, position + forward(), up());
        viewDirty = false;
        // The combined matrix is derived from both, so it goes stale with either.
        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }

    if (projectionDirty)
    {
        if (mode == Projection::Perspective)
        {
            projectionMatrix = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
        }
        else
        {
            const float halfHeight = orthoHeight * 0.5f;
            const float halfWidth = halfHeight * aspect;
            projectionMatrix =
                glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
        }
        projectionDirty = false;
        viewProjectionMatrix = projectionMatrix * viewMatrix;
    }
}

const glm::mat4& EngineCamera::view() const
{
    rebuild();
    return viewMatrix;
}

const glm::mat4& EngineCamera::projection() const
{
    rebuild();
    return projectionMatrix;
}

const glm::mat4& EngineCamera::viewProjection() const
{
    rebuild();
    return viewProjectionMatrix;
}

void EngineCamera::apply(const Shader& shader) const
{
    if (!shader.valid())
    {
        return;
    }

    // Only what the shader actually declares. A lit surface wants the matrices
    // and the eye position; a fullscreen effect wants the inverses and has no
    // use for the rest -- and neither should be warned about uniforms it was
    // never going to read.
    if (shader.has("uView"))
    {
        shader.set("uView", view());
    }
    if (shader.has("uProjection"))
    {
        shader.set("uProjection", projection());
    }
    if (shader.has("uCameraPosition"))
    {
        shader.set("uCameraPosition", position);
    }

    // The pair a fullscreen pass reconstructs view rays from: unproject a
    // clip-space corner with invProjection, rotate it into the world with
    // invView, and every pixel knows which way it is looking. Inverted here
    // rather than cached, because it costs two 4x4 inversions per shader per
    // frame and only for shaders that ask.
    if (shader.has("invProjection"))
    {
        shader.set("invProjection", glm::inverse(projection()));
    }
    if (shader.has("invView"))
    {
        shader.set("invView", glm::inverse(view()));
    }
}
