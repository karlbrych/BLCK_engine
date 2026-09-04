#version 460 core 
layout(location = 0) in vec3 aPosition;
layout (location =1) in vec3 aNormal;

out vec3 WorldPos;
out vec3 Normal;
out vec3 LocalPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main(){
    vec4 worldPos = model * vec4(aPosition,1.0);
    WorldPos = worldPos.xyz;

    Normal = mat3(transpose(inverse(model))) * aNormal;

    gl_Position = worldPos * view * projection;
}