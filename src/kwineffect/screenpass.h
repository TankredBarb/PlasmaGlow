/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <optional>
#include <QSize>

namespace KWin
{
class RenderTarget;
class RenderViewport;
class RectF;
class Region;
class GLVertexBuffer;

// A value means compute can reproduce the full fragment quad; the value is
// whether image-store coordinates need a vertical flip.
std::optional<bool> computeOutputFlipY(const RenderTarget &target, const RenderViewport &viewport,
                                     const RectF &geometry, QSize captureSize, const Region &region, int mode);
void drawScreenPass(GLVertexBuffer *vbo, const RenderTarget &target,
                    const RenderViewport &viewport, const Region &region);
}
