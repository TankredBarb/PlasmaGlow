#version 140

#include "colormanagement.glsl"
#include "plasmaglow-sharpening.glsl"

in vec2 texcoord0;
out vec4 fragColor;

// Sharpen in normalized linear light, using KWin's transfer conversion.
// Integer texel clamping prevents sampling the opposite screen edge.
vec3 sampleLinear(ivec2 offset)
{
    ivec2 size = textureSize(sampler, 0);
    ivec2 pixel = clamp(ivec2(texcoord0 * vec2(size)) + offset, ivec2(0), size - 1);
    vec4 color = decodeSdr(texelFetch(sampler, pixel, 0));
    return color.rgb / (max(color.a, 0.001) * max(destinationReferenceLuminance, 0.001));
}

void main()
{
    fragColor = adjustColor(decodeSdr(texture(sampler, texcoord0)));
}
