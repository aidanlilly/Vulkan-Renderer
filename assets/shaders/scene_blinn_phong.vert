#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambientColor;
    vec4 directionalDirection;
    vec4 directionalColor;
    vec4 pointPositionRange[8];
    vec4 pointColorIntensity[8];
    ivec4 lightOptions;
} scene;

layout(push_constant) uniform DrawPush {
    mat4 model;
    vec4 diffuse;
    vec4 specular;
    vec4 materialOptions;
} draw;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;

void main() {
    vec4 world = draw.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(transpose(inverse(draw.model))) * inNormal;
    texCoord = inTexCoord;
    gl_Position = scene.viewProjection * world;
}
