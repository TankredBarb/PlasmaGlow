/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "screenpass.h"

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>

namespace KWin
{
std::optional<bool> computeOutputFlipY(const RenderTarget &target, const RenderViewport &viewport,
                                     const RectF &geometry, QSize captureSize, const Region &region, int mode)
{
    const auto transfer = target.colorDescription()->transferFunction().type;
    const auto transform = viewport.transform().kind();
    const GLenum format = target.texture()->internalFormat();
    if (mode == 0 || (transfer != TransferFunction::sRGB && transfer != TransferFunction::gamma22)
        || (format != GL_RGBA8 && format != GL_RGBA16 && format != GL_RGBA16F)
        || target.size() != captureSize || !viewport.renderOffset().isNull()
        || !region.contains(viewport.deviceRect())
        || (transform != OutputTransform::Normal && transform != OutputTransform::FlipY)) {
        return std::nullopt;
    }
    // Match the actual fragment quad, including fractional scale and output
    // origin. Only exact full-target texel mapping can use image stores.
    const auto scaled = geometry.scaled(viewport.scale());
    const QVector2D tl(scaled.left(), scaled.top()), tr(scaled.right(), scaled.top());
    const QVector2D bl(scaled.left(), scaled.bottom()), br(scaled.right(), scaled.bottom());
    const auto matrix = viewport.projectionMatrix();
    const bool flipY = (matrix * QVector4D(tl, 0, 1)).toVector2D().y() < 0;
    const float top = flipY ? -1.0f : 1.0f;
    const auto matches = [&matrix](QVector2D point, QVector2D expected) {
        return ((matrix * QVector4D(point, 0, 1)).toVector2D() - expected).lengthSquared() < 1.0e-10f;
    };
    if (!matches(tl, {-1, top}) || !matches(tr, {1, top})
        || !matches(bl, {-1, -top}) || !matches(br, {1, -top})) {
        return std::nullopt;
    }
    return flipY;
}

void drawScreenPass(GLVertexBuffer *vbo, const RenderTarget &target,
                    const RenderViewport &viewport, const Region &region)
{
    if (region.contains(viewport.deviceRect())) {
        vbo->draw(GL_TRIANGLES, 0, 6);
        return;
    }
    // deviceRegion is layer-local; scissoring needs framebuffer coordinates,
    // including the output transform. Sharpening still requests a full repaint.
    Region clip = viewport.transform().map(region & viewport.deviceRect(),
                                          viewport.transform().map(target.size()));
    const bool scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint scissorBox[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    if (scissorEnabled) {
        clip &= Rect(scissorBox[0], target.size().height() - scissorBox[1] - scissorBox[3],
                     scissorBox[2], scissorBox[3]);
    }
    glEnable(GL_SCISSOR_TEST);
    vbo->draw(clip, GL_TRIANGLES, 0, 6, true);
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    if (!scissorEnabled) {
        glDisable(GL_SCISSOR_TEST);
    }
}
}
