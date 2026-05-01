#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D particleTex;

layout(push_constant) uniform PushConstants {
    float day_night_factor;
};

void main() {
    // Minecraft-style sky colors
    vec3 daySkyTop = vec3(0.4, 0.6, 1.0);
    vec3 daySkyBottom = vec3(0.7, 0.8, 1.0);
    vec3 nightSkyTop = vec3(0.02, 0.05, 0.1);
    vec3 nightSkyBottom = vec3(0.05, 0.1, 0.2);
    
    // Smoothstep transition for sunset/sunrise glow
    float glow = smoothstep(0.4, 0.7, day_night_factor) * (1.0 - smoothstep(0.7, 1.0, day_night_factor));
    vec3 sunsetGlow = vec3(1.0, 0.4, 0.2);

    // Interpolate based on day_night_factor (1.0 = Day, 0.4 = Night)
    float t = (day_night_factor - 0.4) / 0.6;
    vec3 skyTop = mix(nightSkyTop, daySkyTop, t);
    vec3 skyBottom = mix(nightSkyBottom, daySkyBottom, t);
    
    // Apply sunset/sunrise glow
    skyBottom = mix(skyBottom, sunsetGlow, glow * 0.5);

    vec3 skyColor = mix(skyTop, skyBottom, fragUV.y);
    
    vec4 particleColor = texture(particleTex, fragUV);
    
    // Blend particles over sky
    outColor = vec4(mix(skyColor, particleColor.rgb, particleColor.a), 1.0);
}
