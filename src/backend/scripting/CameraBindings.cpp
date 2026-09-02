#include <scripting/Bindings.h>

#include <EngineCamera.h>
#include <Renderer.h>
#include <scripting/LuaConvert.h>

#include <memory>
#include <tuple>

// The camera, both halves of it: the usertype a script holds a handle to, and
// the factory that makes one. gameplay/lib/camera.lua wraps this into modes
// (rts, first-person, orbit, fly); everything below is the primitives that
// wrapper is built from.
namespace scripting
{

void installCamera(sol::state& lua, sol::table& engine, const Context& context)
{
    Renderer* r = context.renderer;

    lua.new_usertype<EngineCamera>(
        "EngineCamera", sol::no_constructor,

        // -- placement
        "setPosition",
        [](EngineCamera& self, sol::object x, sol::optional<float> y, sol::optional<float> z)
        {
            self.setPosition(y && z ? glm::vec3(x.as<float>(), *y, *z)
                                    : toVec3(x, self.getPosition()));
        },
        "move", [](EngineCamera& self, float x, float y, float z)
        { self.move(glm::vec3(x, y, z)); }, "moveLocal", &EngineCamera::moveLocal,

        // -- orientation
        "setRotation", [](EngineCamera& self, float yaw, float pitch, sol::optional<float> roll)
        { self.setRotation(yaw, pitch, roll.value_or(self.getRoll())); }, "rotate",
        [](EngineCamera& self, float yaw, float pitch, sol::optional<float> roll)
        { self.rotate(yaw, pitch, roll.value_or(0.0f)); }, "lookAt",
        [](EngineCamera& self, sol::object x, sol::optional<float> y, sol::optional<float> z)
        { self.lookAt(y && z ? glm::vec3(x.as<float>(), *y, *z) : toVec3(x, glm::vec3(0.0f))); },
        "orbit", [](EngineCamera& self, sol::object pivot, float deltaYaw, float deltaPitch)
        { self.orbit(toVec3(pivot, glm::vec3(0.0f)), deltaYaw, deltaPitch); }, "dolly",
        [](EngineCamera& self, sol::object pivot, float amount)
        { self.dolly(toVec3(pivot, glm::vec3(0.0f)), amount); }, "setPitchLimit",
        &EngineCamera::setPitchLimit,

        // -- projection
        "setPerspective", &EngineCamera::setPerspective,   //
        "setOrthographic", &EngineCamera::setOrthographic, //
        "setFov", &EngineCamera::setFov,                   //
        "setOrthoHeight", &EngineCamera::setOrthoHeight,   //
        "setAspect", &EngineCamera::setAspect,             //
        "setViewport", &EngineCamera::setViewport,         //
        "setClipPlanes", &EngineCamera::setClipPlanes,     //
        "setAutoAspect", &EngineCamera::setAutoAspect,

        // -- queries; vectors come back as three values, so a script can write
        //    local x, y, z = camera:getPosition()
        "getPosition",
        [](const EngineCamera& self)
        {
            const glm::vec3& p = self.getPosition();
            return std::make_tuple(p.x, p.y, p.z);
        },
        "forward",
        [](const EngineCamera& self)
        {
            const glm::vec3 v = self.forward();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "right",
        [](const EngineCamera& self)
        {
            const glm::vec3 v = self.right();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "up",
        [](const EngineCamera& self)
        {
            const glm::vec3 v = self.up();
            return std::make_tuple(v.x, v.y, v.z);
        },
        "getYaw", &EngineCamera::getYaw,       //
        "getPitch", &EngineCamera::getPitch,   //
        "getRoll", &EngineCamera::getRoll,     //
        "getFov", &EngineCamera::getFov,                     //
        "getOrthoHeight", &EngineCamera::getOrthoHeight,     //
        "isOrthographic", &EngineCamera::isOrthographic,     //
        "getAspect", &EngineCamera::getAspect,               //
        "getNear", &EngineCamera::getNear,                   //
        "getFar", &EngineCamera::getFar);

    // ---- Engine.camera ---------------------------------------------------
    sol::table camera = engine.create_named("camera");
    camera["new"] = [r](sol::optional<sol::table> options)
    {
        auto instance = std::make_shared<EngineCamera>();

        const glm::vec3 position = options
                                       ? readVec3(*options, "position", glm::vec3(0.0f, 0.0f, 3.0f))
                                       : glm::vec3(0.0f, 0.0f, 3.0f);
        instance->setPosition(position);

        const float aspect = r->aspect();
        float fov = 60.0f;
        float nearZ = 0.1f;
        float farZ = 500.0f;
        if (options)
        {
            fov = (*options)["fov"].get_or(fov);
            nearZ = (*options)["near"].get_or(nearZ);
            farZ = (*options)["far"].get_or(farZ);
        }
        instance->setPerspective(fov, aspect, nearZ, farZ);

        if (options)
        {
            // target wins over yaw/pitch: it is the spelling a scene reaches for.
            if (sol::object target = (*options)["target"]; target.valid())
            {
                instance->lookAt(toVec3(target, glm::vec3(0.0f)));
            }
            else if (sol::object yaw = (*options)["yaw"]; yaw.valid())
            {
                instance->setRotation(yaw.as<float>(), (*options)["pitch"].get_or(0.0f));
            }
        }
        return instance;
    };
}

} // namespace scripting
