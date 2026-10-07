/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "computesharpening.h"
#include "screenpass.h"

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/eglcontext.h>
#include <opengl/egldisplay.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/glvertexbuffer.h>
#include <QGuiApplication>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace KWin;

static void require(bool condition, const char *message)
{
    if (!condition) {
        qFatal("%s", message);
    }
}

static std::vector<unsigned short> readImage(QSize size, GLenum format)
{
    std::vector<unsigned short> result(size.width() * size.height() * 4);
    if (format == GL_RGBA8) {
        std::vector<unsigned char> bytes(result.size());
        glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
        std::copy(bytes.begin(), bytes.end(), result.begin());
    } else if (format == GL_RGBA16) {
        glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_SHORT, result.data());
    } else {
        glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_HALF_FLOAT, result.data());
    }
    return result;
}

static void testScreenPass()
{
    auto shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture, QString(),
        QStringLiteral(":/effects/plasmaglow/shaders/plasmaglow-color.frag"));
    require(bool(shader), "Color shader failed");
    const QSize size(11, 7);
    auto source = GLTexture::allocate(GL_RGBA8, size);
    auto destination = GLTexture::allocate(GL_RGBA8, size);
    std::vector<unsigned char> input(size.width() * size.height() * 4, 180);
    for (size_t i = 3; i < input.size(); i += 4) {
        input[i] = 255;
    }
    source->bind();
    source->setFilter(GL_NEAREST);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, input.data());
    GLFramebuffer framebuffer(destination.get());
    require(framebuffer.valid(), "Screen-pass framebuffer failed");
    GLFramebuffer::pushFramebuffer(&framebuffer);
    ShaderManager::instance()->pushShader(shader.get());
    shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(1, 1, 1, 1));
    shader->setUniform(GLShader::IntUniform::Sampler, 0);
    shader->setUniform("plasmaglowSaturation", 2.0f);
    shader->setUniform("gamma", 0.9f);
    shader->setColorspaceUniforms(ColorDescription::sRGB, ColorDescription::sRGB, RenderingIntent::RelativeColorimetric);
    int cases = 0;
    for (int kind = 0; kind < 8; ++kind) {
        const OutputTransform transform(static_cast<OutputTransform::Kind>(kind));
        destination->setContentTransform(transform);
        RenderTarget target(&framebuffer);
        const QSize localSize = transform.map(size);
        const RectF rect(120, -32, localSize.width() / 1.25, localSize.height() / 1.25);
        RenderViewport viewport(rect, 1.25, target, QPoint());
        const auto scaled = rect.scaled(viewport.scale());
        const QVector2D tl(scaled.left(), scaled.top()), tr(scaled.right(), scaled.top());
        const QVector2D bl(scaled.left(), scaled.bottom()), br(scaled.right(), scaled.bottom());
        GLVertexBuffer vbo(GLVertexBuffer::Static);
        vbo.setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
        const auto mapped = vbo.map<GLVertex2D>(6);
        require(bool(mapped), "Screen-pass VBO map failed");
        auto vertices = *mapped;
        vertices[0] = {tl, {0, 1}}; vertices[1] = {br, {1, 0}}; vertices[2] = {bl, {0, 0}};
        vertices[3] = {tl, {0, 1}}; vertices[4] = {tr, {1, 1}}; vertices[5] = {br, {1, 0}};
        vbo.unmap();
        shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
        vbo.bindArrays();
        glDisable(GL_SCISSOR_TEST);
        drawScreenPass(&vbo, target, viewport, Region::infinite());
        const auto full = readImage(size, GL_RGBA8);
        require(full[3] == 255, "Full screen pass did not draw");
        if (kind != OutputTransform::Normal && kind != OutputTransform::FlipY) {
            require(!computeOutputFlipY(target, viewport, rect, size, Region::infinite(), 1), "Rotated/mirrored output selected compute");
        }
        const Region damage = Region(Rect(-2, 1, 5, 3)) | Rect(localSize.width() - 3, localSize.height() - 3, 5, 5);
        require(!computeOutputFlipY(target, viewport, rect, size, damage, 1), "Partial damage selected compute");
        for (bool enabled : {false, true}) for (int regionKind : {0, 1, 2}) {
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            // Preserve this box even when scissoring was initially disabled.
            glScissor(1, 2, 7, 4);
            if (enabled) {
                glEnable(GL_SCISSOR_TEST);
            }
            const Region region = regionKind == 0 ? Region() : regionKind == 1 ? damage : Region::infinite();
            drawScreenPass(&vbo, target, viewport, region);
            require(bool(glIsEnabled(GL_SCISSOR_TEST)) == enabled, "Scissor enable was not restored");
            GLint box[4];
            glGetIntegerv(GL_SCISSOR_BOX, box);
            require(box[0] == 1 && box[1] == 2 && box[2] == 7 && box[3] == 4, "Scissor box was not restored");
            const auto actual = readImage(size, GL_RGBA8);
            for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width(); ++x) {
                // Independently invert the output transform in top-left pixel coordinates.
                const int py = size.height() - 1 - y;
                int lx = x, ly = py;
                switch (kind) {
                case 1: lx = localSize.width() - 1 - py; ly = x; break;
                case 2: lx = localSize.width() - 1 - x; ly = localSize.height() - 1 - py; break;
                case 3: lx = py; ly = localSize.height() - 1 - x; break;
                case 4: lx = localSize.width() - 1 - x; break;
                case 5: lx = py; ly = x; break;
                case 6: ly = localSize.height() - 1 - py; break;
                case 7: lx = localSize.width() - 1 - py; ly = localSize.height() - 1 - x; break;
                }
                const bool painted = region.contains(QPoint(lx, ly))
                    && (!enabled || (x >= 1 && x < 8 && y >= 2 && y < 6));
                for (int channel = 0; channel < 4; ++channel) {
                    const size_t index = (y * size.width() + x) * 4 + channel;
                    require(actual[index] == (painted ? full[index] : 0), "Partial pass wrote wrong pixels");
                }
            }
            ++cases;
        }
        glDisable(GL_SCISSOR_TEST);
        vbo.unbindArrays();
    }
    ShaderManager::instance()->popShader();
    GLFramebuffer::popFramebuffer();
    require(glGetError() == GL_NO_ERROR, "Screen-pass OpenGL error");
    qInfo() << "PASS:" << cases << "color-pass/scissor cases across all output transforms";
}

static void testRcas()
{
    const QSize size(5, 5);
    auto source = GLTexture::allocate(GL_RGBA8, size);
    auto destination = GLTexture::allocate(GL_RGBA16, size);
    GLFramebuffer framebuffer(destination.get());
    require(framebuffer.valid(), "RCAS framebuffer failed");
    GLFramebuffer::pushFramebuffer(&framebuffer);
    glViewport(0, 0, 5, 5);
    const auto description = ColorDescription::sRGB->withTransferFunction(
        TransferFunction(TransferFunction::gamma22, 0, 80));
    ComputeSharpening compute;
    int previous = 0;
    for (int field : {0, 255, 112, 200, 32}) {
        std::vector<unsigned char> input(5 * 5 * 4, field);
        for (size_t i = 3; i < input.size(); i += 4) input[i] = 255;
        const int center = (2 * 5 + 2) * 4;
        if (field != 0 && field != 255) {
            for (int c = 0; c < 3; ++c) input[center + c] = 128;
        }
        source->bind();
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 5, 5, GL_RGBA, GL_UNSIGNED_BYTE, input.data());
        for (float strength : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            require(compute.dispatch(source.get(), destination.get(), *description,
                3, 1, 1, strength, 0, false), "RCAS reference dispatch failed");
            const auto actual = readImage(size, GL_RGBA16);
            // Symmetric crosses exercise the fixed -3/16 cap, the black
            // clipping limit (200), and the white clipping limit (32).
            // Analytic result includes gamma-2 filtering and gamma-2.2 output.
            const double e = std::pow(128.0 / 255, 1.1);
            const double neighbor = std::pow(field / 255.0, 1.1);
            const double attenuation[] = {0.25, 0.5, 1.0, 1.0, 1.0};
            const double gains[] = {1.0, 1.0, 1.0, 2.5, 4.0};
            const int preset = std::lround(strength * 4);
            const double limit = field == 200 ? -e / (4 * neighbor)
                : field == 32 ? (1 - e) / (4 * neighbor - 4) : -3.0 / 16;
            const double weight = limit * attenuation[preset];
            const double sharpened = (e + 4 * neighbor * weight) / (1 + 4 * weight);
            const double linear = std::clamp(e * e + (sharpened * sharpened - e * e) * gains[preset], 0.0, 1.0);
            const int expected = field != 0 && field != 255 ? std::lround(std::pow(linear, 1.0 / 2.2) * 65535)
                : field == 0 ? 0 : 65535;
            for (int c = 0; c < 3; ++c) {
                require(std::abs(int(actual[center + c]) - expected) <= 1,
                    "RCAS differs from analytic strength reference");
            }
            for (size_t i = 3; i < actual.size(); i += 4) require(actual[i] == 65535, "RCAS changed alpha");
            if (field == 112) {
                require(actual[center] > previous, "RCAS strength did not increase");
                previous = actual[center];
            } else if (field == 0 || field == 255) {
                for (size_t i = 0; i < actual.size(); ++i) {
                    require(actual[i] == (i % 4 == 3 ? 65535 : expected), "RCAS changed black/white flat field");
                }
            }
        }
    }
    GLFramebuffer::popFramebuffer();
    require(glGetError() == GL_NO_ERROR, "RCAS reference OpenGL error");
    qInfo() << "PASS: RCAS analytic kernel/strength/clipping reference, black/white fields and alpha, 25 cases";
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    Q_INIT_RESOURCE(plasmaglow);
    auto display = EglDisplay::create(eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr), nullptr);
    if (!display) {
        qWarning("No surfaceless EGL display; GPU test skipped");
        return 77;
    }
    eglBindAPI(EGL_OPENGL_API);
    const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    eglChooseConfig(display->handle(), attributes, &config, 1, &count);
    require(count > 0, "No OpenGL EGL config");
    auto context = EglContext::create(display.get(), config, nullptr);
    require(context && context->makeCurrent(), "Cannot create OpenGL context");
    if (!epoxy_is_desktop_gl() || epoxy_gl_version() < 43) {
        qWarning("OpenGL 4.3 unavailable; GPU test skipped");
        return 77;
    }
    qInfo() << "GPU:" << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    auto shader = ShaderManager::instance()->generateShaderFromFile(ShaderTrait::MapTexture, QString(),
        QStringLiteral(":/effects/plasmaglow/shaders/plasmaglow.frag"));
    require(shader && shader->uniformLocation("sharpeningMode") >= 0, "Fragment shader failed");
    ComputeSharpening compute;
    int cases = 0;
    int worst = 0;
    for (int width : {1, 2, 17, 63}) for (GLenum format : {GL_RGBA8, GL_RGBA16, GL_RGBA16F}) {
        const QSize size(width, width == 1 ? 1 : width + 2);
        auto source = GLTexture::allocate(format, size);
        auto destination = GLTexture::allocate(format, size);
        source->setFilter(GL_NEAREST);
        source->setWrapMode(GL_CLAMP_TO_EDGE);
        GLFramebuffer framebuffer(destination.get());
        require(framebuffer.valid(), "Framebuffer failed");
        GLFramebuffer::pushFramebuffer(&framebuffer);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        std::vector<float> input(width * size.height() * 4);
        for (int pattern : {0, 1, 2}) {
            std::mt19937 random(42);
            std::uniform_real_distribution<float> distribution(0, 1);
            for (int y = 0; y < size.height(); ++y) for (int x = 0; x < width; ++x) {
                const int index = (y * width + x) * 4;
                const float alpha = pattern == 0 ? 1.0f : index % 28 == 0 ? 0.0005f : distribution(random);
                input[index + 3] = alpha;
                for (int channel = 0; channel < 3; ++channel) {
                    input[index + channel] = alpha * (pattern == 0 ? float((x + y + channel) % 2)
                        : pattern == 1 ? distribution(random) : float(x + channel) / (width + 2));
                }
            }
            source->bind();
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, size.height(), GL_RGBA, GL_FLOAT, input.data());
            for (auto transfer : {TransferFunction::sRGB, TransferFunction::gamma22})
            for (auto transform : {OutputTransform::Normal, OutputTransform::FlipY})
            for (int mode : {1, 2, 3}) for (float gamma : {0.1f, 1.0f, 5.0f})
            for (float strength : {0.0f, 0.5f, 1.0f}) for (float saturation : {1.0f, 4.0f}) {
                const float denoise = strength == 0 ? 0 : 1;
                const auto description = ColorDescription::sRGB->withTransferFunction(TransferFunction(transfer, 0.1, 80));
                destination->setContentTransform(transform);
                RenderTarget target(&framebuffer, description);
                // Exercise a nonzero logical output origin and a fractional scale.
                const double scale = 1.25;
                const RectF rect(120, -32, width / scale, size.height() / scale);
                RenderViewport viewport(rect, scale, target, QPoint());
                const auto scaled = rect.scaled(scale);
                const QVector2D tl(scaled.left(), scaled.top()), tr(scaled.right(), scaled.top());
                const QVector2D bl(scaled.left(), scaled.bottom()), br(scaled.right(), scaled.bottom());
                GLVertexBuffer vbo(GLVertexBuffer::Static);
                vbo.setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
                const auto mapped = vbo.map<GLVertex2D>(6);
                require(bool(mapped), "VBO map failed");
                auto vertices = *mapped;
                vertices[0] = {tl, {0, 1}}; vertices[1] = {br, {1, 0}}; vertices[2] = {bl, {0, 0}};
                vertices[3] = {tl, {0, 1}}; vertices[4] = {tr, {1, 1}}; vertices[5] = {br, {1, 0}};
                vbo.unmap();
                source->bind();
                ShaderManager::instance()->pushShader(shader.get());
                shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
                shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(1, 1, 1, 1));
                shader->setUniform(GLShader::IntUniform::Sampler, 0);
                shader->setUniform("plasmaglowSaturation", saturation);
                shader->setUniform("gamma", gamma);
                shader->setUniform("sharpeningMode", mode);
                shader->setUniform("sharpeningStrength", strength);
                shader->setUniform("sharpeningDenoise", denoise);
                shader->setColorspaceUniforms(description, description, RenderingIntent::RelativeColorimetric);
                vbo.bindArrays();
                vbo.draw(GL_TRIANGLES, 0, 6);
                vbo.unbindArrays();
                const auto reference = readImage(size, format);
                // Poison every pixel, including alpha, so missing image stores
                // cannot leave the fragment reference (or matching black pixels).
                std::vector<float> poison(reference.size());
                const unsigned short midpoint = format == GL_RGBA8 ? 127 : format == GL_RGBA16 ? 32767 : 0x3800;
                for (size_t i = 0; i < reference.size(); ++i) {
                    poison[i] = reference[i] > midpoint ? 0.0f : 1.0f;
                }
                destination->bind();
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, size.height(), GL_RGBA, GL_FLOAT, poison.data());
                source->bind();
                GLint oldProgram;
                glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
                glBindImageTexture(0, source->texture(), 0, GL_FALSE, 0, GL_READ_WRITE, format);
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, destination->texture());
                const auto projected = (viewport.projectionMatrix() * QVector4D(tl, 0, 1)).toVector2D();
                const auto selectedFlip = computeOutputFlipY(target, viewport, rect, size, Region(viewport.deviceRect()), mode);
                require(selectedFlip && *selectedFlip == (projected.y() < 0), "Full-target selector failed");
                require(!computeOutputFlipY(target, viewport, rect, size, Region(), mode), "Empty region selected compute");
                require(!computeOutputFlipY(target, viewport, rect, size, Region(viewport.deviceRect()), 0), "Off selected compute");
                require(!computeOutputFlipY(target, viewport, rect, size + QSize(1, 0), Region::infinite(), mode), "Size mismatch selected compute");
                require(!computeOutputFlipY(target, viewport, rect.translated(1, 0), size, Region::infinite(), mode), "Quad mismatch selected compute");
                RenderViewport offsetViewport(rect, scale, target, QPoint(1, 0));
                require(!computeOutputFlipY(target, offsetViewport, rect, size, Region::infinite(), mode), "Offset selected compute");
                require(compute.dispatch(source.get(), destination.get(), *description, mode,
                    saturation, gamma, strength, denoise, projected.y() < 0), "Compute dispatch failed");
                GLint restored;
                glGetIntegerv(GL_CURRENT_PROGRAM, &restored);
                require(restored == oldProgram, "Program binding was not restored");
                glGetIntegerv(GL_ACTIVE_TEXTURE, &restored);
                require(restored == GL_TEXTURE3, "Active texture was not restored");
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &restored);
                require(GLuint(restored) == destination->texture(), "Texture binding was not restored");
                glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &restored);
                require(GLuint(restored) == source->texture(), "Image binding was not restored");
                glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, 0, &restored);
                require(restored == GL_READ_WRITE, "Image access was not restored");
                glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, 0, &restored);
                require(GLenum(restored) == format, "Image format was not restored");
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &restored);
                require(GLuint(restored) == source->texture(), "Texture unit zero binding was not restored");
                const auto actual = readImage(size, format);
                for (size_t i = 0; i < actual.size(); ++i) {
                    const int delta = std::abs(int(actual[i]) - int(reference[i]));
                    worst = std::max(worst, delta);
                    if (delta > 1) {
                        qFatal("Pixel mismatch: width=%d format=%x transform=%d mode=%d gamma=%g pixel=%zu delta=%d",
                            width, format, int(transform), mode, double(gamma), i, delta);
                    }
                }
                // Raster state that imageStore cannot reproduce must fall back.
                for (GLenum state : {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
                                     GL_RASTERIZER_DISCARD, GL_CULL_FACE, GL_COLOR_LOGIC_OP}) {
                    glEnable(state);
                    require(!compute.dispatch(source.get(), destination.get(), *description, mode,
                        saturation, gamma, strength, denoise, false), "Raster state did not fall back");
                    require(glIsEnabled(state), "Fallback changed raster state");
                    glDisable(state);
                }
                glViewport(1, 0, size.width(), size.height());
                require(!compute.dispatch(source.get(), destination.get(), *description, mode,
                    saturation, gamma, strength, denoise, false), "Viewport offset did not fall back");
                glViewport(0, 0, size.width() + 1, size.height());
                require(!compute.dispatch(source.get(), destination.get(), *description, mode,
                    saturation, gamma, strength, denoise, false), "Viewport size did not fall back");
                glViewport(0, 0, size.width(), size.height());
                for (int channel = 0; channel < 4; ++channel) {
                    glColorMask(channel != 0, channel != 1, channel != 2, channel != 3);
                    require(!compute.dispatch(source.get(), destination.get(), *description, mode,
                        saturation, gamma, strength, denoise, false), "Color mask did not fall back");
                }
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                ShaderManager::instance()->popShader();
                require(glGetError() == GL_NO_ERROR, "OpenGL error");
                ++cases;
            }
        }
        glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
        GLFramebuffer::popFramebuffer();
    }
    // Other transfers and output formats must retain the fragment fallback.
    for (auto transfer : {TransferFunction::linear, TransferFunction::BT1886, TransferFunction::PerceptualQuantizer}) {
        auto texture = GLTexture::allocate(GL_RGBA8, QSize(11, 7));
        GLFramebuffer fbo(texture.get());
        RenderTarget target(&fbo, ColorDescription::sRGB->withTransferFunction(TransferFunction(transfer, 0, 80)));
        const RectF rect(0, 0, 11, 7);
        RenderViewport viewport(rect, 1, target, QPoint());
        require(!computeOutputFlipY(target, viewport, rect, texture->size(), Region::infinite(), 1), "Unsupported transfer selected compute");
    }
    {
        auto texture = GLTexture::allocate(GL_RGBA32F, QSize(11, 7));
        GLFramebuffer fbo(texture.get());
        RenderTarget target(&fbo);
        const RectF rect(0, 0, 11, 7);
        RenderViewport viewport(rect, 1, target, QPoint());
        require(!computeOutputFlipY(target, viewport, rect, texture->size(), Region::infinite(), 1), "Unsupported format selected compute");
    }
    testScreenPass();
    testRcas();
    qInfo() << "PASS:" << cases << "fragment/compute comparisons; maximum storage step difference:" << worst;
    return 0;
}
