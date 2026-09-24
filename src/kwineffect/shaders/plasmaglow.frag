#version 140

#include "colormanagement.glsl"

uniform sampler2D sampler;
uniform vec4 modulation;
uniform float plasmaglowSaturation;
uniform float gamma;

in vec2 texcoord0;
out vec4 fragColor;

void main()
{
    vec4 color = sourceEncodingToNitsInDestinationColorspace(texture(sampler, texcoord0));
    float alpha = color.a;

    vec3 rgb = color.rgb / max(alpha, 0.001);
    float luminance = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = vec3(luminance) + plasmaglowSaturation * (rgb - vec3(luminance));

    float referenceLuminance = max(destinationReferenceLuminance, 0.001);
    rgb = pow(max(rgb, vec3(0.0)) / referenceLuminance, vec3(1.0 / gamma)) * referenceLuminance;
    color.rgb = clamp(rgb, vec3(0.0), vec3(maxDestinationLuminance)) * alpha;

    color *= modulation;
    fragColor = nitsToDestinationEncoding(color);
}
