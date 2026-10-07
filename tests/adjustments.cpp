#include "glowcontroller.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDialog>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>
#include <KConfigGroup>
#include <KSharedConfig>

#include <functional>
#include <limits>
#include <iostream>
#include <cstdlib>

class MockEffects : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")
public Q_SLOTS:
    bool isEffectLoaded(const QString &) { return true; }
};

class MockGlow : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.plasmaglow.Effect1")
public:
    QVariantMap values{{QStringLiteral("apiVersion"), 3u}, {QStringLiteral("ready"), true}, {QStringLiteral("vibrance"), 0.0}, {QStringLiteral("saturation"), 1.0}, {QStringLiteral("gamma"), 1.0},
                       {QStringLiteral("sharpeningMode"), QStringLiteral("off")}, {QStringLiteral("sharpeningStrength"), 0.5}, {QStringLiteral("sharpeningDenoise"), 0.17}, {QStringLiteral("error"), QString()}};
    int calls = 0;
public Q_SLOTS:
    QVariantMap getState() { return values; }
    bool setAllParameters(double saturation, double gamma, const QString &mode, double strength, double denoise, double vibrance)
    {
        values[QStringLiteral("vibrance")] = vibrance;
        values[QStringLiteral("saturation")] = saturation;
        values[QStringLiteral("gamma")] = gamma;
        values[QStringLiteral("sharpeningMode")] = mode;
        values[QStringLiteral("sharpeningStrength")] = strength;
        values[QStringLiteral("sharpeningDenoise")] = denoise;
        ++calls;
        Q_EMIT stateChanged(values);
        return true;
    }
Q_SIGNALS:
    void stateChanged(const QVariantMap &state);
};

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static void waitFor(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 4000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(done(), "Async state timeout");
}

static void checkSavedValues(const GlowController &controller)
{
    require(controller.vibrance() == 0.5 && controller.saturation() == 2.0 && controller.gamma() == 0.9
                && controller.sharpeningMode() == QStringLiteral("luma") && controller.sharpeningStrength() == 0.75
                && controller.sharpeningDenoise() == 0.25,
            "Disabling or refreshing lost selected values");
}

int main(int argc, char **argv)
{
    QTemporaryDir configDirectory;
    qputenv("XDG_CONFIG_HOME", configDirectory.path().toUtf8());
    QApplication app(argc, argv);
    auto bus = QDBusConnection::sessionBus();
    MockEffects effects;
    MockGlow effect;
    require(bus.registerService(QStringLiteral("org.kde.KWin")) && bus.registerService(QStringLiteral("org.kde.PlasmaGlow")), "Mock services");
    require(bus.registerObject(QStringLiteral("/Effects"), &effects, QDBusConnection::ExportAllSlots)
                && bus.registerObject(QStringLiteral("/org/kde/PlasmaGlow"), &effect,
                                      QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals),
            "Mock objects");
    {
        GlowController controller;
        waitFor([&] { return controller.backendReady() && effect.calls > 0; });
        require(controller.adjustmentsEnabled(), "Existing users must default to enabled");
        require(controller.vibrance() == 0.0, "Vibrance must default to neutral");
        controller.setVibrance(std::numeric_limits<double>::quiet_NaN());
        require(controller.vibrance() == 0.0, "Vibrance accepted NaN");
        controller.setVibrance(2.0);
        require(controller.vibrance() == 1.0, "Vibrance upper clamp failed");
        controller.setVibrance(-1.0);
        require(controller.vibrance() == 0.0, "Vibrance lower clamp failed");
        controller.setVibrance(0.5);
        controller.setSaturation(2.0);
        controller.setGamma(0.9);
        controller.setSharpeningMode(QStringLiteral("luma"));
        controller.setSharpeningStrength(0.75);
        controller.setSharpeningDenoise(0.25);
        waitFor([&] { return controller.appliedSharpeningMode() == QStringLiteral("luma") && controller.appliedGamma() == 0.9 && controller.appliedVibrance() == 0.5; });
        controller.setAdjustmentsEnabled(false);
        controller.refresh();
        waitFor([&] { return controller.backendReady() && controller.appliedSaturation() == 1.0 && controller.appliedGamma() == 1.0
                            && controller.appliedSharpeningMode() == QStringLiteral("off") && controller.appliedVibrance() == 0.0; });
        checkSavedValues(controller);
        controller.refresh();
        waitFor([&] { return controller.backendReady(); });
        checkSavedValues(controller);
        controller.setVibrance(0.75);
        waitFor([&] { return effect.values[QStringLiteral("vibrance")].toDouble() == 0.0; });
        require(controller.vibrance() == 0.75 && controller.appliedVibrance() == 0.0,
                "Editing paused Vibrance enabled it or lost the selected value");
        controller.setVibrance(0.5);
        controller.setAdjustmentsEnabled(true);
        waitFor([&] { return controller.appliedSaturation() == 2.0 && controller.appliedGamma() == 0.9
                            && controller.appliedSharpeningMode() == QStringLiteral("luma")
                            && controller.appliedSharpeningStrength() == 0.75 && controller.appliedSharpeningDenoise() == 0.25 && controller.appliedVibrance() == 0.5; });
        // Toggle while earlier D-Bus applies are still queued.
        controller.setAdjustmentsEnabled(false);
        controller.setAdjustmentsEnabled(true);
        controller.setAdjustmentsEnabled(false);
        waitFor([&] { return controller.appliedSaturation() == 1.0 && controller.appliedGamma() == 1.0
                            && controller.appliedSharpeningMode() == QStringLiteral("off") && controller.appliedVibrance() == 0.0; });
    }
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    config->reparseConfiguration();
    KConfigGroup group(config, QStringLiteral("General"));
    require(!group.readEntry(QStringLiteral("adjustmentsEnabled"), true) && group.readEntry(QStringLiteral("saturation"), 1.0) == 2.0
                && group.readEntry(QStringLiteral("vibrance"), 0.0) == 0.5
                && group.readEntry(QStringLiteral("gamma"), 1.0) == 0.9 && group.readEntry(QStringLiteral("sharpeningMode"), QString()) == QStringLiteral("luma"),
            "Disabled state or selected settings not persisted");
    {
        const int previousCalls = effect.calls;
        GlowController controller;
        require(!controller.adjustmentsEnabled(), "Disabled state not restored");
        waitFor([&] { return controller.backendReady() && effect.calls > previousCalls; });
        checkSavedValues(controller);
        require(effect.values[QStringLiteral("saturation")].toDouble() == 1.0 && effect.values[QStringLiteral("gamma")].toDouble() == 1.0
                    && effect.values[QStringLiteral("sharpeningMode")].toString() == QStringLiteral("off"), "Startup enabled a paused effect");
        controller.setAdjustmentsEnabled(true);
        waitFor([&] { return controller.appliedSharpeningMode() == QStringLiteral("luma") && controller.appliedSaturation() == 2.0 && controller.appliedVibrance() == 0.5; });
        controller.setAdjustmentsEnabled(false);
        controller.reset();
        waitFor([&] { return controller.appliedSaturation() == 1.0 && controller.appliedSharpeningMode() == QStringLiteral("off") && controller.appliedVibrance() == 0.0; });
        require(!controller.adjustmentsEnabled() && controller.vibrance() == 0.0 && controller.saturation() == 1.0 && controller.gamma() == 1.0,
                "Reset enabled adjustments");
        controller.showAboutDialog();
        controller.showAboutDialog();
        int dialogs = 0;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            dialogs += qobject_cast<QDialog *>(widget) && widget->isVisible();
            const QString screenshotPath = qEnvironmentVariable("PLASMAGLOW_ABOUT_SCREENSHOT");
            if (!screenshotPath.isEmpty() && qobject_cast<QDialog *>(widget) && widget->isVisible()) {
                require(widget->grab().save(screenshotPath), "Could not capture About dialog");
            }
        }
        require(dialogs == 1, "About dialog duplicated or did not open");
    }
    {
        GlowController controller;
        waitFor([&] { return controller.backendReady(); });
        controller.setAdjustmentsEnabled(true);
        controller.setSharpeningMode(QStringLiteral("rcas"));
        controller.setSharpeningStrength(0.75);
        waitFor([&] { return controller.appliedSharpeningMode() == QStringLiteral("rcas")
                            && controller.appliedSharpeningStrength() == 0.75; });
        controller.setSharpeningMode(QStringLiteral("nis"));
        require(controller.sharpeningMode() == QStringLiteral("rcas"), "Invalid mode replaced RCAS");
        controller.setAdjustmentsEnabled(false);
        waitFor([&] { return controller.appliedSharpeningMode() == QStringLiteral("off") && controller.appliedVibrance() == 0.0; });
    }
    {
        GlowController controller;
        require(controller.sharpeningMode() == QStringLiteral("rcas") && !controller.adjustmentsEnabled(),
                "RCAS persistence or paused startup failed");
        waitFor([&] { return controller.backendReady(); });
        controller.setAdjustmentsEnabled(true);
        waitFor([&] { return controller.appliedSharpeningMode() == QStringLiteral("rcas"); });
        for (const QString &mode : {QStringLiteral("cas"), QStringLiteral("luma"), QStringLiteral("rcas")}) {
            controller.setSharpeningMode(mode);
            waitFor([&] { return controller.appliedSharpeningMode() == mode; });
        }
        controller.reset();
        waitFor([&] { return controller.appliedSharpeningMode() == QStringLiteral("off") && controller.appliedVibrance() == 0.0; });
    }
    std::cout << "PASS disable/restore, refresh, rapid toggles, persistence, paused startup/reset, native About dialog, Vibrance bounds/default/persistence/disable/reset, RCAS persistence/transitions/reset\n";
    return 0;
}

#include "adjustments.moc"
