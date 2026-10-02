#version 140

#include "colormanagement.glsl"

uniform sampler2D sampler;
uniform vec4 modulation;
uniform float plasmaglowSaturation;
uniform float gamma;
uniform int sharpeningMode;
uniform float sharpeningStrength;
uniform float sharpeningDenoise;

in vec2 texcoord0;
out vec4 fragColor;

// Sharpen in normalized linear light, using KWin's transfer/color conversion.
// Explicit texel-center clamping prevents sampling the opposite screen edge.
vec3 sampleLinear(ivec2 offset)
{
    vec2 size = vec2(textureSize(sampler, 0));
    vec2 uv = clamp(texcoord0 + vec2(offset) / size, 0.5 / size, 1.0 - 0.5 / size);
    vec4 color = sourceEncodingToNitsInDestinationColorspace(texture(sampler, uv));
    return color.rgb / (max(color.a, 0.001) * max(destinationReferenceLuminance, 0.001));
}

// CAS: adapted from FrameFlow's Vulkan adaptation of vkBasalt's CAS shader.
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT (see THIRD_PARTY_NOTICES.md).
vec3 sharpenCas(vec3 center)
{
    vec3 a = sampleLinear(ivec2(-1, -1));
    vec3 b = sampleLinear(ivec2( 0, -1));
    vec3 c = sampleLinear(ivec2( 1, -1));
    vec3 d = sampleLinear(ivec2(-1,  0));
    vec3 e = center;
    vec3 f = sampleLinear(ivec2( 1,  0));
    vec3 g = sampleLinear(ivec2(-1,  1));
    vec3 h = sampleLinear(ivec2( 0,  1));
    vec3 i = sampleLinear(ivec2( 1,  1));

    vec3 minimum = min(min(min(d, e), min(f, b)), h);
    minimum += min(min(min(minimum, a), min(g, c)), i);
    vec3 maximum = max(max(max(d, e), max(f, b)), h);
    maximum += max(max(max(maximum, a), max(g, c)), i);
    vec3 amplitude = clamp(min(minimum, 2.0 - maximum)
        / max(maximum, vec3(1.0e-6)), 0.0, 1.0);
    amplitude = inversesqrt(max(amplitude, vec3(1.0e-6)));
    // The first half covers the original range; the second adds overdrive.
    float peak = 8.0 - 3.0 * min(2.0 * sharpeningStrength, 1.0);
    vec3 weight = -1.0 / (amplitude * peak);
    vec3 result = ((b + d + f + h) * weight + e) / (1.0 + 4.0 * weight);
    float gain = 1.0 + 3.0 * max(2.0 * sharpeningStrength - 1.0, 0.0);
    result = clamp(center + (result - center) * gain, 0.0, 1.0);
    return result;
}

// Denoised Luma Sharpening: adapted from FrameFlow/vkBasalt.
// Image sharpening filter from GeForce Experience. Provided by NVIDIA Corporation.
// Copyright 2019 Suketu J. Shah. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause (see THIRD_PARTY_NOTICES.md).
float luma(vec3 value)
{
    return dot(value, vec3(0.299, 0.587, 0.114));
}

vec3 sharpenLuma(vec3 center)
{
    float x = luma(center);
    float a = luma(sampleLinear(ivec2(-1,  0)));
    float b = luma(sampleLinear(ivec2( 1,  0)));
    float c = luma(sampleLinear(ivec2( 0,  1)));
    float d = luma(sampleLinear(ivec2( 0, -1)));
    float e = luma(sampleLinear(ivec2(-1, -1)));
    float f = luma(sampleLinear(ivec2( 1,  1)));
    float g = luma(sampleLinear(ivec2(-1,  1)));
    float h = luma(sampleLinear(ivec2( 1, -1)));

    float diagonalMin = min(min(e, f), min(g, h));
    float diagonalMax = max(max(e, f), max(g, h));
    float crossMin = min(min(min(a, b), min(c, d)), x);
    float crossMax = max(max(max(a, b), max(c, d)), x);
    float localMin = 0.5 * min(diagonalMin, crossMin) + 0.5 * crossMin;
    float localMax = 0.5 * max(diagonalMax, crossMax) + 0.5 * crossMax;

    float lowWeight = localMin / (localMax + 1.0 / 256.0);
    float highWeight = pow(1.0 - pow(max(localMax - 0.65, 0.0) / 0.35, 2.0), 2.0);
    float denoiseKernel = 1.0 / mix(0.001, 0.1, clamp(sharpeningDenoise, 0.0, 1.0));
    float noiseWeight = pow((localMax - localMin) * denoiseKernel, 2.0);
    float boost = min(min(lowWeight, highWeight), noiseWeight);
    float sharpnessKernel = mix(-1.0 / 14.0, -1.0 / 6.5,
        clamp(2.0 * sharpeningStrength, 0.0, 1.0));
    float k = boost * sharpnessKernel;
    float sharpened = (x + (a + b + c + d) * k
        + (e + f + g + h) * (k * 0.5)) / (1.0 + 6.0 * k);
    vec3 result = center + (sharpened - x);
    float gain = 1.0 + 3.0 * max(2.0 * sharpeningStrength - 1.0, 0.0);
    result = clamp(center + (result - center) * gain, 0.0, 1.0);
    return result;
}

void main()
{
    vec4 color = sourceEncodingToNitsInDestinationColorspace(texture(sampler, texcoord0));
    float alpha = color.a;

    vec3 rgb = color.rgb / max(alpha, 0.001);
    float referenceLuminance = max(destinationReferenceLuminance, 0.001);
    if (sharpeningMode == 1) {
        rgb = sharpenCas(rgb / referenceLuminance) * referenceLuminance;
    } else if (sharpeningMode == 2) {
        rgb = sharpenLuma(rgb / referenceLuminance) * referenceLuminance;
    }
    float luminance = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = vec3(luminance) + plasmaglowSaturation * (rgb - vec3(luminance));

    rgb = pow(max(rgb, vec3(0.0)) / referenceLuminance, vec3(1.0 / gamma)) * referenceLuminance;
    color.rgb = clamp(rgb, vec3(0.0), vec3(maxDestinationLuminance)) * alpha;

    color *= modulation;
    fragColor = nitsToDestinationEncoding(color);
}
