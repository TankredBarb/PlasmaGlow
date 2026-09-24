#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QVariantMap>

#include <functional>

class QDBusServiceWatcher;

class KWinBackend : public QObject
{
    Q_OBJECT

public:
    explicit KWinBackend(QObject *parent = nullptr);

    quint64 applyParameters(double saturation, double gamma);
    void refresh();
    bool isRefreshing() const;

Q_SIGNALS:
    void readinessChanged(bool ready, const QString &error, uint apiVersion);
    void stateChanged(double saturation, double gamma);
    void applyFinished(quint64 requestId, bool success, const QString &error,
                       double saturation, double gamma);

private Q_SLOTS:
    void handleServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void handleEffectStateChanged(const QVariantMap &state);

private:
    struct Parameters {
        double saturation = 1.0;
        double gamma = 1.0;
        quint64 requestId = 0;
    };

    static constexpr uint apiVersion = 1;
    static constexpr int callTimeoutMs = 2000;
    static constexpr double minimumSaturation = 0.0;
    static constexpr double maximumSaturation = 4.0;
    static constexpr double minimumGamma = 0.1;
    static constexpr double maximumGamma = 5.0;

    const QString m_service = QStringLiteral("org.kde.KWin");
    const QString m_effectService = QStringLiteral("org.kde.PlasmaGlow");
    const QString m_effectsPath = QStringLiteral("/Effects");
    const QString m_effectsInterface = QStringLiteral("org.kde.kwin.Effects");
    const QString m_effectObjectPath = QStringLiteral("/org/kde/PlasmaGlow");
    const QString m_effectInterface = QStringLiteral("org.kde.plasmaglow.Effect1");
    QDBusConnection m_bus = QDBusConnection::sessionBus();
    QDBusServiceWatcher *m_kwinServiceWatcher = nullptr;
    QDBusServiceWatcher *m_effectServiceWatcher = nullptr;
    bool m_ready = false;
    bool m_applyInProgress = false;
    bool m_refreshInProgress = false;
    bool m_hasPendingParameters = false;
    uint m_apiVersion = 0;
    quint64 m_generation = 0;
    quint64 m_nextRequestId = 0;
    Parameters m_activeParameters;
    Parameters m_pendingParameters;

    void watchCall(const QDBusMessage &message, const std::function<void(const QDBusMessage &)> &completion);
    void checkEffect();
    void loadEffect();
    void readEffectState();
    void acceptState(const QVariantMap &state);
    void setReady(bool ready, const QString &error, uint version = 0);
    void startNextApply();
};
