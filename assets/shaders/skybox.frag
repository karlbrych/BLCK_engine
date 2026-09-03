#version 460 core 

in vec2 uv;
out vec4 fragColor;

uniform mat4 invView;
uniform mat4 invProjection;

void main(){
    vec2 ndc = uv * 2.0 - 1.0;

    
    vec4 nearPoint = invProjection * vec4(ndc, -1.0, 1.0);
    vec4 farPoint  = invProjection * vec4(ndc,  1.0, 1.0);
    vec3 viewRay = farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w;
    vec3 rayDir = normalize(mat3(invView) * viewRay);

    // Fixed low sun direction -- a sunrise sitting just above the horizon.
    vec3 sunDir = normalize(vec3(0.35, 0.12, -0.9));

    
    float t = clamp(rayDir.y, 0.0, 1.0);
    float horizonBand = pow(1.0 - t, 4.0);
    vec3 zenithColor  = vec3(0.06, 0.10, 0.28);
    vec3 horizonColor = vec3(1.0, 0.55, 0.30);
    vec3 skyColor = mix(zenithColor, horizonColor, horizonBand);

    // Below the horizon fades toward a dimmer, cooler tone so the ground
    // side of the skybox doesn't glow as brightly as the sky.
    float below = clamp(-rayDir.y, 0.0, 1.0);
    skyColor = mix(skyColor, vec3(0.10, 0.08, 0.10), below * 0.6);

    // Sun disc and its warm halo.
    float sunDot = max(dot(rayDir, sunDir), 0.0);
    float sunDisc = pow(sunDot, 2000.0);
    float sunHalo = pow(sunDot, 16.0) * 0.6;
    vec3 sunColor = vec3(1.0, 0.75, 0.45);
    skyColor += sunColor * (sunDisc * 4.0 + sunHalo);

    fragColor = vec4(skyColor, 1.0);
}
