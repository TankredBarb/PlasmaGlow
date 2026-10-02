/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "plasmagloweffect.h"

#include <effect/effecthandler.h>
#include <scene/item.h>
#include <scene/workspacescene.h>
#include <core/rendertarget.h>
#include <core/output.h>
#include <core/renderviewport.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/glutils.h>

#include <QDBusConnection>
#include <QDebug>
#include <KConfigGroup>
#include <KConfig>
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

    m_isGreeter = qEnvironmentVariable("XDG_SESSION_CLASS") == QLatin1String("greeter");
    const auto config = m_isGreeter
        ? KSharedConfig::openConfig(QStringLiteral("/etc/xdg/plasmaglow-loginrc"), KConfig::SimpleConfig)
        : KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    const KConfigGroup settings(config, QStringLiteral("General"));
    const bool enabled = !m_isGreeter || settings.readEntry(QStringLiteral("enabled"), false);
    const double savedSaturation = enabled ? settings.readEntry(QStringLiteral("saturation"), 1.0) : 1.0;
    const double savedGamma = enabled ? settings.readEntry(QStringLiteral("gamma"), 1.0) : 1.0;
    if (std::isfinite(savedSaturation) && savedSaturation >= kMinimumSaturation && savedSaturation <= kMaximumSaturation) {
        m_saturation = savedSaturation;
    }
    if (std::isfinite(savedGamma) && savedGamma >= kMinimumGamma && savedGamma <= kMaximumGamma) {
        m_gamma = savedGamma;
    }

    if (!m_isGreeter) {
        const QString mode = settings.readEntry(QStringLiteral("sharpeningMode"), QStringLiteral("off"));
        if (mode == QLatin1String("off") || mode == QLatin1String("cas") || mode == QLatin1String("luma")) {
            m_sharpeningMode = mode;
        }
        const double strength = settings.readEntry(QStringLiteral("sharpeningStrength"), 0.5);
        const double denoise = settings.readEntry(QStringLiteral("sharpeningDenoise"), 0.17);
        if (std::isfinite(strength) && strength >= 0.0 && strength <= 1.0) {
            m_sharpeningStrength = strength;
        }
        if (std::isfinite(denoise) && denoise >= 0.0 && denoise <= 1.0) {
            m_sharpeningDenoise = denoise;
        }
    }

    m_shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture,
        QString(),
        QStringLiteral(":/effects/plasmaglow/shaders/plasmaglow-color.frag"));

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

    if (m_shader && m_sharpeningMode != QLatin1String("off") && !ensureSharpeningShader()) {
        m_sharpeningMode = QStringLiteral("off");
    }

    if (!m_isGreeter) {
        m_dbusServiceRegistered = m_sessionBus.registerService(m_dbusService);
        if (m_dbusServiceRegistered) {
            m_dbusRegistered = m_sessionBus.registerObject(
                m_objectPath,
                this,
                QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
        }
    }
    if (!m_isGreeter && !m_dbusRegistered) {
        m_lastError = m_dbusServiceRegistered
            ? QStringLiteral("Failed to register the PlasmaGlow D-Bus endpoint")
            : QStringLiteral("Failed to register the PlasmaGlow D-Bus service");
        qWarning().noquote() << "PlasmaGlow:" << m_lastError;
    }

    connect(effects, &EffectsHandler::screenRemoved, this, [this](LogicalOutput *screen) {
        effects->makeOpenGLContextCurrent();
        m_screenCaptures.erase(screen);
    });
    updateSceneEffect();
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

bool PlasmaGlowEffect::ensureSharpeningShader()
{
    if (m_sharpeningShader) {
        return true;
    }
    effects->makeOpenGLContextCurrent();
    auto shader = ShaderManager::instance()->generateShaderFromFile(
        ShaderTrait::MapTexture,
        QString(),
        QStringLiteral(":/effects/plasmaglow/shaders/plasmaglow.frag"));
    if (!shader || shader->uniformLocation("plasmaglowSaturation") < 0
        || shader->uniformLocation("gamma") < 0 || shader->uniformLocation("sharpeningMode") < 0
        || shader->uniformLocation("sharpeningStrength") < 0 || shader->uniformLocation("sharpeningDenoise") < 0) {
        m_lastError = QStringLiteral("Failed to create the sharpening shader");
        qWarning().noquote() << "PlasmaGlow:" << m_lastError;
        return false;
    }
    m_sharpeningShader = std::move(shader);
    return true;
}

bool PlasmaGlowEffect::supported()
{
    return effects->isOpenGLCompositing();
}

bool PlasmaGlowEffect::isActive() const
{
    return (m_isGreeter || m_dbusRegistered) && m_shader
        && (m_saturation != 1.0 || m_gamma != 1.0 || m_sharpeningMode != QLatin1String("off"));
}

bool PlasmaGlowEffect::blocksDirectScanout() const
{
    return false;
}

void PlasmaGlowEffect::updateSceneEffect()
{
    // Keep windows in the corrected scene while allowing a separate cursor plane.
    if (isActive()) {
        if (!m_sceneEffect) {
            m_sceneEffect = std::make_unique<ItemEffect>(effects->scene()->containerItem());
        }
    } else {
        m_sceneEffect.reset();
    }
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
        {QStringLiteral("sharpeningMode"), m_sharpeningMode},
        {QStringLiteral("sharpeningStrength"), m_sharpeningStrength},
        {QStringLiteral("sharpeningDenoise"), m_sharpeningDenoise},
        {QStringLiteral("error"), m_lastError},
    };
}

QVariantMap PlasmaGlowEffect::getState() const
{
    return state();
}

bool PlasmaGlowEffect::setParameters(double saturation, double gamma)
{
    return setAllParameters(saturation, gamma, m_sharpeningMode, m_sharpeningStrength, m_sharpeningDenoise);
}

bool PlasmaGlowEffect::setAllParameters(double saturation, double gamma, const QString &sharpeningMode,
                                       double sharpeningStrength, double sharpeningDenoise)
{
    if (!std::isfinite(saturation) || !std::isfinite(gamma)
        || saturation < kMinimumSaturation || saturation > kMaximumSaturation
        || gamma < kMinimumGamma || gamma > kMaximumGamma
        || (sharpeningMode != QLatin1String("off") && sharpeningMode != QLatin1String("cas")
            && sharpeningMode != QLatin1String("luma"))
        || !std::isfinite(sharpeningStrength) || sharpeningStrength < 0.0 || sharpeningStrength > 1.0
        || !std::isfinite(sharpeningDenoise) || sharpeningDenoise < 0.0 || sharpeningDenoise > 1.0) {
        m_lastError = QStringLiteral("Color or sharpening parameters are outside the supported range");
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

    if (sharpeningMode != QLatin1String("off") && !ensureSharpeningShader()) {
        Q_EMIT stateChanged(state());
        return false;
    }
    m_lastError.clear();
    if (m_saturation == saturation && m_gamma == gamma
        && m_sharpeningMode == sharpeningMode && m_sharpeningStrength == sharpeningStrength
        && m_sharpeningDenoise == sharpeningDenoise) {
        return true;
    }

    const bool repaint = m_saturation != saturation || m_gamma != gamma
        || m_sharpeningMode != sharpeningMode
        || (sharpeningMode != QLatin1String("off")
            && (m_sharpeningStrength != sharpeningStrength || m_sharpeningDenoise != sharpeningDenoise));
    m_saturation = saturation;
    m_gamma = gamma;
    m_sharpeningMode = sharpeningMode;
    m_sharpeningStrength = sharpeningStrength;
    m_sharpeningDenoise = sharpeningDenoise;
    updateSceneEffect();
    if (repaint) {
        effects->addRepaintFull();
    }
    Q_EMIT stateChanged(state());
    return true;
}

void PlasmaGlowEffect::prePaintScreen(ScreenPrePaintData &data)
{
    effects->prePaintScreen(data);
    if (m_sharpeningMode != QLatin1String("off") && data.screen) {
        // KWin collects surface damage after prePaintScreen. Repaint the whole
        // output so neighbouring pixels changed by the filter are presented too.
        data.paint |= data.screen->geometry();
    }
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

    // Each output must retain its own unfiltered source for partial repaints.
    auto &capture = m_screenCaptures[screen];
    auto &screenTexture = capture.texture;
    auto &screenFramebuffer = capture.framebuffer;
    const QSize size = screen->geometry().size() * viewport.scale();
    const GLenum format = renderTarget.texture()->internalFormat();
    const bool newTexture = !screenTexture || screenTexture->size() != size
        || screenTexture->internalFormat() != format;
    if (newTexture) {
        screenFramebuffer.reset();
        screenTexture = GLTexture::allocate(format, size);
        if (screenTexture) {
            screenTexture->setFilter(GL_NEAREST);
            screenTexture->setWrapMode(GL_CLAMP_TO_EDGE);
            screenFramebuffer = std::make_unique<GLFramebuffer>(screenTexture.get());
        }
    }
    if (!screenFramebuffer || !screenFramebuffer->valid()) {
        effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        return;
    }

    RenderTarget fboTarget(screenFramebuffer.get(), renderTarget.colorDescription());
    RenderViewport fboViewport(viewport.renderRect(), viewport.scale(), fboTarget, QPoint());
    GLFramebuffer::pushFramebuffer(screenFramebuffer.get());
    effects->paintScreen(fboTarget, fboViewport, mask,
                         newTexture ? Region(fboViewport.deviceRect()) : deviceRegion, screen);
    GLFramebuffer::popFramebuffer();

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));
    const auto mapped = vbo->map<GLVertex2D>(6);
    if (!mapped) {
        screenTexture->render(screen->geometry().size());
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

    const auto &description = renderTarget.colorDescription();
    int mode = 0;
    if (m_sharpeningMode != QLatin1String("off")) {
        const bool hdr = description->transferFunction().type == TransferFunction::PerceptualQuantizer
            || description->maxHdrLuminance().value_or(description->referenceLuminance())
                > description->referenceLuminance() * 1.01;
        if (!hdr) {
            mode = m_sharpeningMode == QLatin1String("cas") ? 1 : 2;
        }
    }
    // Off uses the original color shader, with no sharpening code or uniforms.
    GLShader *shader = mode == 0 ? m_shader.get() : m_sharpeningShader.get();
    screenTexture->bind();
    ShaderManager::instance()->pushShader(shader);
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    shader->setUniform(GLShader::Vec4Uniform::ModulationConstant, QVector4D(1, 1, 1, 1));
    shader->setUniform("sampler", 0);
    shader->setUniform("plasmaglowSaturation", static_cast<float>(m_saturation));
    shader->setUniform("gamma", static_cast<float>(m_gamma));
    if (mode != 0) {
        shader->setUniform("sharpeningMode", mode);
        shader->setUniform("sharpeningStrength", static_cast<float>(m_sharpeningStrength));
        shader->setUniform("sharpeningDenoise", static_cast<float>(m_sharpeningDenoise));
    }
    shader->setColorspaceUniforms(description, description, RenderingIntent::RelativeColorimetric);
    vbo->bindArrays();
    vbo->draw(GL_TRIANGLES, 0, 6);
    vbo->unbindArrays();
    ShaderManager::instance()->popShader();
    screenTexture->unbind();
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
