/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <epoxy/gl.h>

namespace KWin
{
class GLTexture;
class ColorDescription;

class ComputeSharpening
{
public:
    ComputeSharpening();
    ~ComputeSharpening();
    bool dispatch(GLTexture *source, GLTexture *destination, const ColorDescription &description,
                  int mode, float saturation, float gamma, float strength, float denoise, bool flipY);

private:
    enum Uniform {
        Sampler, Modulation, Saturation, Gamma, Mode, Strength, Denoise,
        SourceTransfer, DestinationTransfer, SourceParams, DestinationParams,
        ReferenceLuminance, MaximumLuminance, FlipY, UniformCount
    };
    GLuint m_program = 0;
    std::array<GLint, UniformCount> m_uniforms;
};
} // namespace KWin
