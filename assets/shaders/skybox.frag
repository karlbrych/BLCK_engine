#version 460 core 

in vec2 uv;
out vec4 fragColor;

uniform mat4 invView;
uniform mat4 invProjection;

void main(){
    vec2 ndc = uv * 2.0 - 1.0;

    // Two points on this pixel's ray, unprojected, and the direction is what
    // separates them. Written this way rather than from a single clip-space
    // point so it holds under either projection: perspective fans the rays out
    // from the eye, orthographic hands every pixel the same direction -- which
    // is exactly what a parallel projection looks like from inside a sky.
    vec4 nearPoint = invProjection * vec4(ndc, -1.0, 1.0);
    vec4 farPoint  = invProjection * vec4(ndc,  1.0, 1.0);
    vec3 viewRay = farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w;
    vec3 rayDir = normalize(mat3(invView) * viewRay);

    float t = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyColor = mix(vec3(0.2, 0.4, 0.6), vec3(0.8, 0.9, 1.0), t);
    fragColor = vec4(skyColor, 1.0);
}
