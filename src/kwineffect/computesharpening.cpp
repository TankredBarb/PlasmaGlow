/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "computesharpening.h"

#include <core/colorspace.h>
#include <opengl/gltexture.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <QFile>
#include <QDebug>

namespace KWin
{

std::unique_ptr<GLShader> loadPlasmaGlowShader(const QString &fileName, bool vibrance)
{
    if (!vibrance) {
        return ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture, QString(), fileName);
    }
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    QByteArray source = file.readAll();
    source.replace("#version 140", "#version 140\n#define PLASMAGLOW_VIBRANCE");
    return ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, {}, source);
}

ComputeSharpening::ComputeSharpening(bool vibrance)
    : m_vibranceEnabled(vibrance)
{
    if (!epoxy_is_desktop_gl() || epoxy_gl_version() < 43) {
        return;
    }
    QFile file(QStringLiteral(":/effects/plasmaglow/shaders/plasmaglow.comp"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QByteArray source = file.readAll();
    if (vibrance) {
        source.replace("#version 430", "#version 430\n#define PLASMAGLOW_VIBRANCE");
    }
    // GLShader preprocesses vertex/fragment programs to GLSL 140. Compute
    // needs GLSL 430, but uses the same KWin color conversion resource.
    for (const char *name : {"colormanagement.glsl", "plasmaglow-sharpening.glsl"}) {
        QFile include(QStringLiteral(":/opengl/") + QLatin1String(name));
        if (!include.open(QIODevice::ReadOnly)) {
            return;
        }
        const QByteArray directive = QByteArray("#include \"") + name + "\"";
        source.replace(directive, include.readAll());
    }
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char *data = source.constData();
    glShaderSource(shader, 1, &data, nullptr);
    glCompileShader(shader);
    GLint valid = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &valid);
    if (valid) {
        m_program = glCreateProgram();
        glAttachShader(m_program, shader);
        glLinkProgram(m_program);
        glGetProgramiv(m_program, GL_LINK_STATUS, &valid);
    }
    if (!valid) {
        char log[4096] = {};
        if (m_program) {
            glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
            glDeleteProgram(m_program);
            m_program = 0;
        } else {
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        }
        qWarning().noquote() << "PlasmaGlow: compute sharpening unavailable, using fragment shader:" << log;
    }
    glDeleteShader(shader);
    if (!m_program) {
        return;
    }
    const char *names[] = {
        "sampler", "modulation", "plasmaglowSaturation", "plasmaglowVibrance", "gamma", "sharpeningMode",
        "sharpeningStrength", "sharpeningDenoise", "sourceNamedTransferFunction",
        "destinationNamedTransferFunction", "sourceTransferFunctionParams",
        "destinationTransferFunctionParams", "destinationReferenceLuminance",
        "maxDestinationLuminance", "flipOutputY"
    };
    for (int i = 0; i < UniformCount; ++i) {
        m_uniforms[i] = glGetUniformLocation(m_program, names[i]);
        if (m_uniforms[i] < 0 && (i != Vibrance || m_vibranceEnabled)) {
            qWarning() << "PlasmaGlow: missing compute uniform" << names[i];
            glDeleteProgram(m_program);
            m_program = 0;
            return;
        }
    }
}

ComputeSharpening::~ComputeSharpening()
{
    if (m_program) {
        glDeleteProgram(m_program);
    }
}

bool ComputeSharpening::dispatch(GLTexture *source, GLTexture *destination,
                                const ColorDescription &description, int mode,
                                float saturation, float gamma, float strength, float denoise, bool flipY, float vibrance)
{
    if (!m_program || glIsEnabled(GL_BLEND) || glIsEnabled(GL_SCISSOR_TEST)
        || glIsEnabled(GL_DEPTH_TEST) || glIsEnabled(GL_STENCIL_TEST)
        || glIsEnabled(GL_RASTERIZER_DISCARD) || glIsEnabled(GL_CULL_FACE)
        || glIsEnabled(GL_COLOR_LOGIC_OP)) {
        return false;
    }
    GLint viewport[4];
    GLboolean colorMask[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    const QSize size = source->size();
    if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != size.width() || viewport[3] != size.height()
        || !colorMask[0] || !colorMask[1] || !colorMask[2] || !colorMask[3]) {
        return false;
    }

    GLint program, activeTexture, texture;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    GLint image, level, layered, layer, access, format;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &image);
    glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL, 0, &level);
    glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED, 0, &layered);
    glGetIntegeri_v(GL_IMAGE_BINDING_LAYER, 0, &layer);
    glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, 0, &access);
    glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, 0, &format);

    glUseProgram(m_program);
    glUniform1i(m_uniforms[Sampler], 0);
    glUniform4f(m_uniforms[Modulation], 1, 1, 1, 1);
    glUniform1f(m_uniforms[Saturation], saturation);
    if (m_vibranceEnabled) {
        glUniform1f(m_uniforms[Vibrance], vibrance);
    }
    glUniform1f(m_uniforms[Gamma], gamma);
    glUniform1i(m_uniforms[Mode], mode);
    glUniform1f(m_uniforms[Strength], strength);
    glUniform1f(m_uniforms[Denoise], denoise);
    const auto transfer = description.transferFunction();
    glUniform1i(m_uniforms[SourceTransfer], transfer.type);
    glUniform1i(m_uniforms[DestinationTransfer], transfer.type);
    glUniform2f(m_uniforms[SourceParams], transfer.minLuminance, transfer.maxLuminance - transfer.minLuminance);
    glUniform2f(m_uniforms[DestinationParams], transfer.minLuminance, transfer.maxLuminance - transfer.minLuminance);
    glUniform1f(m_uniforms[ReferenceLuminance], description.referenceLuminance());
    glUniform1f(m_uniforms[MaximumLuminance], description.maxHdrLuminance().value_or(10000));
    glUniform1i(m_uniforms[FlipY], flipY);
    glBindTexture(GL_TEXTURE_2D, source->texture());
    glBindImageTexture(0, destination->texture(), 0, GL_FALSE, 0, GL_WRITE_ONLY, destination->internalFormat());
    glDispatchCompute((size.width() + 15) / 16, (size.height() + 15) / 16, 1);
    glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    glBindImageTexture(0, image, level, layered, layer, access, format);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(activeTexture);
    glUseProgram(program);
    return true;
}
} // namespace KWin
