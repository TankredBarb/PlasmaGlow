/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <memory>
#include <QString>
#include <epoxy/gl.h>

namespace KWin
{
class GLTexture;
class ColorDescription;
class GLShader;

std::unique_ptr<GLShader> loadPlasmaGlowShader(const QString &fileName, bool vibrance);

class ComputeSharpening
{
public:
    explicit ComputeSharpening(bool vibrance = false);
    ~ComputeSharpening();
    bool dispatch(GLTexture *source, GLTexture *destination, const ColorDescription &description,
                  int mode, float saturation, float gamma, float strength, float denoise, bool flipY, float vibrance = 0.0f);

private:
    enum Uniform {
        Sampler, Modulation, Saturation, Vibrance, Gamma, Mode, Strength, Denoise,
        SourceTransfer, DestinationTransfer, SourceParams, DestinationParams,
        ReferenceLuminance, MaximumLuminance, FlipY, UniformCount
    };
    bool m_vibranceEnabled = false;
    GLuint m_program = 0;
    std::array<GLint, UniformCount> m_uniforms;
};
} // namespace KWin
