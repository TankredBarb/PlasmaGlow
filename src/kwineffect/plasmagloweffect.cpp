/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "plasmagloweffect.h"

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>

#include <QDBusConnection>
#include <QDebug>

#include <cmath>
#include <utility>

static void ensureResources()
{
    Q_INIT_RESOURCE(plasmaglow);
}

namespace KWin
{

PlasmaGlowEffect::PlasmaGlowEffect()
{
    ensureResources();

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

    connect(effects, &EffectsHandler::windowAdded, this, &PlasmaGlowEffect::handleWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &PlasmaGlowEffect::handleWindowDeleted);
}

PlasmaGlowEffect::~PlasmaGlowEffect()
{
    for (EffectWindow *window : std::as_const(m_redirectedWindows)) {
        unredirect(window);
    }
    m_redirectedWindows.clear();

    if (m_dbusRegistered) {
        m_sessionBus.unregisterObject(m_objectPath);
    }
    if (m_dbusServiceRegistered) {
        m_sessionBus.unregisterService(m_dbusService);
    }
}

bool PlasmaGlowEffect::supported()
{
    return OffscreenEffect::supported();
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
    syncAllWindows();
    effects->addRepaintFull();
    Q_EMIT stateChanged(state());
    return true;
}

void PlasmaGlowEffect::drawWindow(const RenderTarget &renderTarget,
                                  const RenderViewport &viewport,
                                  EffectWindow *window,
                                  int mask,
                                  const Region &deviceRegion,
                                  WindowPaintData &data)
{
    if (!m_shader) {
        effects->drawWindow(renderTarget, viewport, window, mask, deviceRegion, data);
        return;
    }

    {
        ShaderBinder binder(m_shader.get());
        m_shader->setUniform("plasmaglowSaturation", static_cast<float>(m_saturation));
        m_shader->setUniform("gamma", static_cast<float>(m_gamma));
    }
    OffscreenEffect::drawWindow(renderTarget, viewport, window, mask, deviceRegion, data);
}

void PlasmaGlowEffect::handleWindowAdded(EffectWindow *window)
{
    syncWindow(window);
}

void PlasmaGlowEffect::handleWindowDeleted(EffectWindow *window)
{
    m_redirectedWindows.remove(window);
}

void PlasmaGlowEffect::syncWindow(EffectWindow *window)
{
    if (!window || !isActive()) {
        return;
    }
    if (m_redirectedWindows.contains(window)) {
        return;
    }

    redirect(window);
    setShader(window, m_shader.get());
    m_redirectedWindows.insert(window);
}

void PlasmaGlowEffect::syncAllWindows()
{
    if (isActive()) {
        for (EffectWindow *window : effects->stackingOrder()) {
            syncWindow(window);
        }
        return;
    }

    for (EffectWindow *window : std::as_const(m_redirectedWindows)) {
        unredirect(window);
    }
    m_redirectedWindows.clear();
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
