/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <effect/effect.h>

#include <QDBusConnection>
#include <QString>
#include <QVariantMap>

#include <map>
#include <memory>

namespace KWin
{

class ItemEffect;
class GLShader;
class GLTexture;
class GLFramebuffer;

class PlasmaGlowEffect final : public Effect
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.plasmaglow.Effect1")

public:
    PlasmaGlowEffect();
    ~PlasmaGlowEffect() override;

    bool isActive() const override;
    bool blocksDirectScanout() const override;
    int requestedEffectChainPosition() const override;

    static bool supported();

public Q_SLOTS:
    Q_SCRIPTABLE QVariantMap getState() const;
    Q_SCRIPTABLE bool setParameters(double saturation, double gamma);
    Q_SCRIPTABLE bool setAllParameters(double saturation, double gamma, const QString &sharpeningMode,
                                      double sharpeningStrength, double sharpeningDenoise);

Q_SIGNALS:
    Q_SCRIPTABLE void stateChanged(const QVariantMap &state);

protected:
    void prePaintScreen(ScreenPrePaintData &data) override;
    void paintScreen(const RenderTarget &renderTarget,
                     const RenderViewport &viewport,
                     int mask,
                     const Region &deviceRegion,
                     LogicalOutput *screen) override;

private:
    QVariantMap state() const;
    bool ensureSharpeningShader();
    void updateSceneEffect();

    static constexpr double kMinimumSaturation = 0.0;
    static constexpr double kMaximumSaturation = 4.0;
    static constexpr double kMinimumGamma = 0.1;
    static constexpr double kMaximumGamma = 5.0;
    static constexpr uint kApiVersion = 2;

    const QString m_dbusService = QStringLiteral("org.kde.PlasmaGlow");
    const QString m_objectPath = QStringLiteral("/org/kde/PlasmaGlow");
    QDBusConnection m_sessionBus = QDBusConnection::sessionBus();
    std::unique_ptr<ItemEffect> m_sceneEffect;
    std::unique_ptr<GLShader> m_shader;
    std::unique_ptr<GLShader> m_sharpeningShader;
    struct ScreenCapture {
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };
    std::map<LogicalOutput *, ScreenCapture> m_screenCaptures;
    double m_saturation = 1.0;
    double m_gamma = 1.0;
    QString m_sharpeningMode = QStringLiteral("off");
    double m_sharpeningStrength = 0.5;
    double m_sharpeningDenoise = 0.17;
    QString m_lastError;
    bool m_dbusServiceRegistered = false;
    bool m_dbusRegistered = false;
    bool m_isGreeter = false;
};

} // namespace KWin
