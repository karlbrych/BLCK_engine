#version 460 core

// Matches the Vertex struct in src/backend/Mesh.h.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
// mat3(transpose(inverse(model))), so normals survive non-uniform scaling.
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vUV;

void main()
{
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    vWorldPosition = worldPosition.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUV = aUV;

    gl_Position = uProjection * uView * worldPosition;
}
