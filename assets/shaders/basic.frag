#version 460 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uBaseColor = vec3(0.85, 0.35, 0.25);
uniform float uAlpha = 1.0;
// Always bound -- the renderer supplies a 1x1 white texture when a draw call
// has none, so this sampler never reads from an incomplete unit.
uniform sampler2D uBaseColorTexture;
uniform bool uHasTexture = false;
uniform vec3 uLightDirection = vec3(-0.4, -1.0, -0.6); // world space, points *from* the light
uniform vec3 uLightColor = vec3(1.0);
uniform vec3 uAmbientColor = vec3(0.06, 0.07, 0.10);
uniform vec3 uCameraPosition;
uniform float uShininess = 48.0;

out vec4 fragColor;

// Blinn-Phong. The framebuffer is sRGB, so everything here stays linear and the
// hardware does the encoding on write.
void main()
{
    vec4 sampled = uHasTexture ? texture(uBaseColorTexture, vUV) : vec4(1.0);
    vec3 albedo = uBaseColor * sampled.rgb;
    float alpha = uAlpha * sampled.a;

    // Imported meshes are not always wound consistently, and a double-sided
    // material draws both faces -- flip the normal towards the viewer so the
    // back side is lit rather than black.
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing)
    {
        normal = -normal;
    }

    vec3 toLight = normalize(-uLightDirection);
    vec3 toCamera = normalize(uCameraPosition - vWorldPosition);
    vec3 halfway = normalize(toLight + toCamera);

    float diffuse = max(dot(normal, toLight), 0.0);
    float specular = diffuse > 0.0 ? pow(max(dot(normal, halfway), 0.0), uShininess) : 0.0;

    // Textured surfaces carry their own highlights already; a specular lobe on
    // top of photographed geometry just reads as haze.
    float specularStrength = uHasTexture ? 0.05 : 0.3;

    vec3 color = albedo * (uAmbientColor + uLightColor * diffuse)
               + uLightColor * specular * specularStrength;

    fragColor = vec4(color, alpha);
}
