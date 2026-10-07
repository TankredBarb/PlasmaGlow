uniform sampler2D sampler;
uniform vec4 modulation;
uniform float plasmaglowSaturation;
#ifdef PLASMAGLOW_VIBRANCE
uniform float plasmaglowVibrance;
#endif
uniform float gamma;
uniform int sharpeningMode;
uniform float sharpeningStrength;
uniform float sharpeningDenoise;

// The capture and destination have the same SDR color description. There is
// no gamut conversion or tone mapping; retain KWin's transfer and clipping.
vec4 decodeSdr(vec4 color)
{
    color = encodingToNits(color, sourceNamedTransferFunction,
        sourceTransferFunctionParams.x, sourceTransferFunctionParams.y);
    color.rgb = clamp(color.rgb, vec3(0.0), vec3(maxDestinationLuminance));
    return color;
}

vec3 sampleLinear(ivec2 offset);

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
    // The first half covers the original range; the second adds overdrive.
    float peak = 8.0 - 3.0 * min(2.0 * sharpeningStrength, 1.0);
    vec3 weight = -sqrt(max(amplitude, vec3(1.0e-6))) / peak;
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

// RCAS: FsrRcasF from AMD FidelityFX FSR, without FSR_RCAS_DENOISE.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT (see THIRD_PARTY_NOTICES.md).
// Work in gamma-2 encoded SDR, then return to normalized linear light.
vec3 sharpenRcas(vec3 center)
{
    vec3 b = sqrt(clamp(sampleLinear(ivec2(0, -1)), 0.0, 1.0));
    vec3 d = sqrt(clamp(sampleLinear(ivec2(-1, 0)), 0.0, 1.0));
    vec3 e = sqrt(clamp(center, 0.0, 1.0));
    vec3 f = sqrt(clamp(sampleLinear(ivec2(1, 0)), 0.0, 1.0));
    vec3 h = sqrt(clamp(sampleLinear(ivec2(0, 1)), 0.0, 1.0));
    vec3 minimum = min(min(b, d), min(f, h));
    vec3 maximum = max(max(b, d), max(f, h));
    // Finite limiting values for constant black/white neighborhoods.
    vec3 hitMin = min(minimum, e) / max(4.0 * maximum, vec3(1.0e-6));
    vec3 hitMax = (1.0 - max(maximum, e)) / min(4.0 * minimum - 4.0, vec3(-1.0e-6));
    vec3 lobes = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(max(lobes.r, lobes.g), lobes.b), 0.0));
    // The first half covers native attenuation; the second adds CAS-style overdrive.
    lobe *= exp2(-2.0 * (1.0 - min(2.0 * sharpeningStrength, 1.0)));
    vec3 result = clamp((lobe * b + lobe * d + lobe * h + lobe * f + e)
        / (4.0 * lobe + 1.0), 0.0, 1.0);
    float gain = 1.0 + 3.0 * max(2.0 * sharpeningStrength - 1.0, 0.0);
    return clamp(center + (result * result - center) * gain, 0.0, 1.0);
}

vec4 adjustColor(vec4 color)
{
    float alpha = color.a;

    vec3 rgb = color.rgb / max(alpha, 0.001);
    float referenceLuminance = max(destinationReferenceLuminance, 0.001);
    if (sharpeningMode == 1) {
        rgb = sharpenCas(rgb / referenceLuminance) * referenceLuminance;
    } else if (sharpeningMode == 2) {
        rgb = sharpenLuma(rgb / referenceLuminance) * referenceLuminance;
    } else if (sharpeningMode == 3) {
        rgb = sharpenRcas(rgb / referenceLuminance) * referenceLuminance;
    }
    float luminance = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
#ifdef PLASMAGLOW_VIBRANCE
    // Relative chroma protects saturated colors regardless of their brightness.
    float peak = max(max(rgb.r, rgb.g), rgb.b);
    float minimum = min(min(rgb.r, rgb.g), rgb.b);
    float chroma = clamp((peak - minimum) / max(peak, 0.001), 0.0, 1.0);
    rgb = vec3(luminance) + (1.0 + plasmaglowVibrance * (1.0 - chroma)) * (rgb - vec3(luminance));
#endif
    rgb = vec3(luminance) + plasmaglowSaturation * (rgb - vec3(luminance));

    rgb = max(rgb, vec3(0.0));
    if (gamma != 1.0) {
        rgb = pow(rgb / referenceLuminance, vec3(1.0 / gamma)) * referenceLuminance;
    }
    color.rgb = clamp(rgb, vec3(0.0), vec3(maxDestinationLuminance)) * alpha;

    color *= modulation;
    return nitsToDestinationEncoding(color);
}
