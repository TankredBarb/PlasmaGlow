#version 140

#include "colormanagement.glsl"

uniform sampler2D sampler;
uniform vec4 modulation;
uniform float plasmaglowSaturation;
#ifdef PLASMAGLOW_VIBRANCE
uniform float plasmaglowVibrance;
#endif
uniform float gamma;

in vec2 texcoord0;
out vec4 fragColor;

void main()
{
    vec4 color = sourceEncodingToNitsInDestinationColorspace(texture(sampler, texcoord0));
    float alpha = color.a;

    vec3 rgb = color.rgb / max(alpha, 0.001);
    float luminance = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
#ifdef PLASMAGLOW_VIBRANCE
    // Relative chroma protects saturated colors regardless of their brightness.
    float peak = max(max(rgb.r, rgb.g), rgb.b);
    float minimum = min(min(rgb.r, rgb.g), rgb.b);
    float chroma = clamp((peak - minimum) / max(peak, 0.001), 0.0, 1.0);
    rgb = vec3(luminance) + (1.0 + plasmaglowVibrance * (1.0 - chroma)) * (rgb - vec3(luminance));
#endif
    rgb = vec3(luminance) + plasmaglowSaturation * (rgb - vec3(luminance));

    float referenceLuminance = max(destinationReferenceLuminance, 0.001);
    rgb = max(rgb, vec3(0.0));
    if (gamma != 1.0) {
        rgb = pow(rgb / referenceLuminance, vec3(1.0 / gamma)) * referenceLuminance;
    }
    color.rgb = clamp(rgb, vec3(0.0), vec3(maxDestinationLuminance)) * alpha;

    color *= modulation;
    fragColor = nitsToDestinationEncoding(color);
}
