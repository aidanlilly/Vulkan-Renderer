#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

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

vec3 safeNormalize(vec3 value, vec3 fallbackValue) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-10 ? value * inversesqrt(lengthSquared) : fallbackValue;
}

void main() {
    vec3 baseColor = max(draw.diffuse.rgb, vec3(0.0));
    if (scene.lightOptions.x == 0) {
        outColor = vec4(baseColor, draw.diffuse.a);
        return;
    }

    vec3 N = safeNormalize(worldNormal, vec3(0.0, 1.0, 0.0));
    vec3 V = safeNormalize(scene.cameraPosition.xyz - worldPosition, vec3(0.0, 0.0, 1.0));
    vec3 color = scene.ambientColor.rgb * baseColor;
    float shininess = max(draw.materialOptions.x, 1.0);

    if (scene.lightOptions.y != 0) {
        vec3 L = safeNormalize(-scene.directionalDirection.xyz, vec3(0.0, 1.0, 0.0));
        vec3 H = safeNormalize(L + V, N);
        float ndotl = max(dot(N, L), 0.0);
        float spec = ndotl > 0.0 ? pow(max(dot(N, H), 0.0), shininess) : 0.0;
        color += scene.directionalColor.rgb * (baseColor * ndotl + draw.specular.rgb * spec);
    }

    if (scene.lightOptions.z != 0) {
        for (int i = 0; i < scene.lightOptions.w; ++i) {
            vec3 toLight = scene.pointPositionRange[i].xyz - worldPosition;
            float distanceToLight = length(toLight);
            float range = max(scene.pointPositionRange[i].w, 0.001);
            if (distanceToLight >= range) continue;
            vec3 L = safeNormalize(toLight, N);
            vec3 H = safeNormalize(L + V, N);
            float ndotl = max(dot(N, L), 0.0);
            float spec = ndotl > 0.0 ? pow(max(dot(N, H), 0.0), shininess) : 0.0;
            float falloff = max(1.0 - distanceToLight / range, 0.0);
            float attenuation = falloff * falloff / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
            vec3 light = scene.pointColorIntensity[i].rgb * scene.pointColorIntensity[i].w;
            color += attenuation * light * (baseColor * ndotl + draw.specular.rgb * spec);
        }
    }

    outColor = vec4(max(color, vec3(0.0)), draw.diffuse.a);
}
