/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "plasmagloweffect.h"

#include <effect/effecthandler.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/glutils.h>

#include <QDBusConnection>
#include <QDebug>
#include <KConfigGroup>
#include <KSharedConfig>

#include <cmath>

static void ensureResources()
{
    Q_INIT_RESOURCE(plasmaglow);
}

namespace KWin
{

PlasmaGlowEffect::PlasmaGlowEffect()
{
    ensureResources();

    const KConfigGroup settings(KSharedConfig::openConfig(QStringLiteral("plasmaglowrc")), QStringLiteral("General"));
    const double savedSaturation = settings.readEntry(QStringLiteral("saturation"), 1.0);
    const double savedGamma = settings.readEntry(QStringLiteral("gamma"), 1.0);
    if (std::isfinite(savedSaturation) && savedSaturation >= kMinimumSaturation && savedSaturation <= kMaximumSaturation) {
        m_saturation = savedSaturation;
    }
    if (std::isfinite(savedGamma) && savedGamma >= kMinimumGamma && savedGamma <= kMaximumGamma) {
        m_gamma = savedGamma;
    }

    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture,
        QString(),
        QStringLiteral(":/effects/plasmaglow/shaders/plasmaglow.frag"));

    if (!m_shader) {
        m_lastError = QStringLiteral("Failed to create the color adjustment shader");
        qWarning().noquote() << "PlasmaGlow:" << m_lastError;
    } else {
        if (m_shader->uniformLocation("plasmaglowSaturation") < 0
            || m_shader->uniformLocation("gamma") < 0) {
            m_shader.reset();
            m_lastError = QStringLiteral("The color adjustment shader is missing required uniforms");
            qWarning().noquote() << "PlasmaGlow:" << m_lastError;
        }
    }

    m_dbusServiceRegistered = m_sessionBus.registerService(m_dbusService);
    if (m_dbusServiceRegistered) {
        m_dbusRegistered = m_sessionBus.registerObject(
            m_objectPath,
            this,
            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
    }
    if (!m_dbusRegistered) {
        m_lastError = m_dbusServiceRegistered
            ? QStringLiteral("Failed to register the PlasmaGlow D-Bus endpoint")
            : QStringLiteral("Failed to register the PlasmaGlow D-Bus service");
        qWarning().noquote() << "PlasmaGlow:" << m_lastError;
    }

    effects->addRepaintFull();
}

PlasmaGlowEffect::~PlasmaGlowEffect()
{
    if (m_dbusRegistered) {
        m_sessionBus.unregisterObject(m_objectPath);
    }
    if (m_dbusServiceRegistered) {
        m_sessionBus.unregisterService(m_dbusService);
    }
}

bool PlasmaGlowEffect::supported()
{
    return effects->isOpenGLCompositing();
}

bool PlasmaGlowEffect::isActive() const
{
    return m_dbusRegistered && m_shader && (m_saturation != 1.0 || m_gamma != 1.0);
}

int PlasmaGlowEffect::requestedEffectChainPosition() const
{
    return 99;
}

QVariantMap PlasmaGlowEffect::state() const
{
    return {
        {QStringLiteral("apiVersion"), kApiVersion},
        {QStringLiteral("ready"), m_dbusRegistered && bool(m_shader)},
        {QStringLiteral("saturation"), m_saturation},
        {QStringLiteral("gamma"), m_gamma},
        {QStringLiteral("error"), m_lastError},
    };
}

QVariantMap PlasmaGlowEffect::getState() const
{
    return state();
}

bool PlasmaGlowEffect::setParameters(double saturation, double gamma)
{
    if (!std::isfinite(saturation) || !std::isfinite(gamma)
        || saturation < kMinimumSaturation || saturation > kMaximumSaturation
        || gamma < kMinimumGamma || gamma > kMaximumGamma) {
        m_lastError = QStringLiteral("Saturation or gamma is outside the supported range");
        Q_EMIT stateChanged(state());
        return false;
    }

    if (!m_dbusRegistered || !m_shader) {
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("The PlasmaGlow effect is not ready");
        }
        Q_EMIT stateChanged(state());
        return false;
    }

    m_lastError.clear();
    if (m_saturation == saturation && m_gamma == gamma) {
        return true;
    }

    m_saturation = saturation;
    m_gamma = gamma;
    effects->addRepaintFull();
    Q_EMIT stateChanged(state());
    return true;
}

void PlasmaGlowEffect::paintScreen(const RenderTarget &renderTarget,
                                   const RenderViewport &viewport,
                                   int mask,
                                   const Region &deviceRegion,
                                   LogicalOutput *screen)
{
    if (!isActive() || !renderTarget.texture() || !screen) {
        effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        return;
    }

    const QSize size = screen->geometry().size() * viewport.scale();
    const GLenum format = renderTarget.texture()->internalFormat();
    const bool newTexture = !m_screenTexture || m_screenTexture->size() != size
        || m_screenTexture->internalFormat() != format;
    if (newTexture) {
        m_screenFramebuffer.reset();
        m_screenTexture = GLTexture::allocate(format, size);
        if (m_screenTexture) {
            m_screenFramebuffer = std::make_unique<GLFramebuffer>(m_screenTexture.get());
        }
    }
    if (!m_screenFramebuffer || !m_screenFramebuffer->valid()) {
        effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        return;
    }

    RenderTarget fboTarget(m_screenFramebuffer.get(), renderTarget.colorDescription());
    RenderViewport fboViewport(viewport.renderRect(), viewport.scale(), fboTarget, QPoint());
    GLFramebuffer::pushFramebuffer(m_screenFramebuffer.get());
    effects->paintScreen(fboTarget, fboViewport, mask,
                         newTexture ? Region(fboViewport.deviceRect()) : deviceRegion, screen);
    GLFramebuffer::popFramebuffer();

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const auto mapped = vbo->map<GLVertex2D>(6);
    if (!mapped) {
        m_screenTexture->render(screen->geometry().size());
        return;
    }
    const auto scaled = screen->geometry().scaled(viewport.scale());
    const QVector2D topLeft(scaled.left(), scaled.top());
    const QVector2D topRight(scaled.right(), scaled.top());
    const QVector2D bottomLeft(scaled.left(), scaled.bottom());
    const QVector2D bottomRight(scaled.right(), scaled.bottom());
    auto vertices = *mapped;
    vertices[0] = {topLeft, {0.0f, 1.0f}};
    vertices[1] = {bottomRight, {1.0f, 0.0f}};
    vertices[2] = {bottomLeft, {0.0f, 0.0f}};
    vertices[3] = {topLeft, {0.0f, 1.0f}};
    vertices[4] = {topRight, {1.0f, 1.0f}};
    vertices[5] = {bottomRight, {1.0f, 0.0f}};
    vbo->unmap();

    m_screenTexture->bind();
    ShaderManager::instance()->pushShader(m_shader.get());
    m_shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    m_shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(1, 1, 1, 1));
    m_shader->setUniform("sampler", 0);
    m_shader->setUniform("plasmaglowSaturation", static_cast<float>(m_saturation));
    m_shader->setUniform("gamma", static_cast<float>(m_gamma));
    m_shader->setColorspaceUniforms(renderTarget.colorDescription(), renderTarget.colorDescription(), RenderingIntent::RelativeColorimetric);
    vbo->bindArrays();
    vbo->draw(GL_TRIANGLES, 0, 6);
    vbo->unbindArrays();
    ShaderManager::instance()->popShader();
    m_screenTexture->unbind();
}

class PlasmaGlowEffectFactory final : public EffectPluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID EffectPluginFactory_iid FILE "plasmaglow.json")
    Q_INTERFACES(KPluginFactory)

public:
    bool isSupported() const override
    {
        return PlasmaGlowEffect::supported();
    }

    Effect *createEffect() const override
    {
        return new PlasmaGlowEffect();
    }
};

} // namespace KWin

#include "plasmagloweffect.moc"
