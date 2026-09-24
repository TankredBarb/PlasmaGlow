#include "kwinbackend.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>

#include <cmath>

KWinBackend::KWinBackend(QObject *parent)
    : QObject(parent)
    , m_kwinServiceWatcher(new QDBusServiceWatcher(m_service, m_bus,
                                                   QDBusServiceWatcher::WatchForOwnerChange, this))
    , m_effectServiceWatcher(new QDBusServiceWatcher(m_effectService, m_bus,
                                                     QDBusServiceWatcher::WatchForOwnerChange, this))
{
    connect(m_kwinServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &KWinBackend::handleServiceOwnerChanged);
    connect(m_effectServiceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &KWinBackend::handleServiceOwnerChanged);
    m_bus.connect(m_effectService, m_effectObjectPath, m_effectInterface, QStringLiteral("stateChanged"),
                  this, SLOT(handleEffectStateChanged(QVariantMap)));
    checkEffect();
}

quint64 KWinBackend::applyParameters(double saturation, double gamma)
{
    if (!std::isfinite(saturation) || !std::isfinite(gamma)) {
        return 0;
    }

    m_pendingParameters = {
        qBound(minimumSaturation, saturation, maximumSaturation),
        qBound(minimumGamma, gamma, maximumGamma),
        ++m_nextRequestId
    };
    m_hasPendingParameters = true;
    startNextApply();
    return m_pendingParameters.requestId;
}

void KWinBackend::refresh()
{
    ++m_generation;
    m_hasPendingParameters = false;
    m_refreshInProgress = false;
    setReady(false, QStringLiteral("Checking the PlasmaGlow KWin effect"));
    m_refreshInProgress = true;
    checkEffect();
}

bool KWinBackend::isRefreshing() const
{
    return m_refreshInProgress;
}

void KWinBackend::handleServiceOwnerChanged(const QString &service, const QString &, const QString &newOwner)
{
    ++m_generation;
    m_applyInProgress = false;
    if (service == m_service) {
        setReady(false, newOwner.isEmpty() ? QStringLiteral("KWin is not running") : QString());
        if (!newOwner.isEmpty()) {
            checkEffect();
        }
        return;
    }

    setReady(false, newOwner.isEmpty() ? QStringLiteral("The PlasmaGlow KWin effect is unavailable") : QString());
    if (!newOwner.isEmpty()) {
        readEffectState();
    }
}

void KWinBackend::handleEffectStateChanged(const QVariantMap &state)
{
    acceptState(state);
}

void KWinBackend::watchCall(const QDBusMessage &message,
                            const std::function<void(const QDBusMessage &)> &completion)
{
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, callTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [watcher, completion](QDBusPendingCallWatcher *) {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        completion(reply);
    });
}

void KWinBackend::checkEffect()
{
    const quint64 generation = m_generation;
    QDBusMessage message = QDBusMessage::createMethodCall(m_service, m_effectsPath,
                                                          m_effectsInterface, QStringLiteral("isEffectLoaded"));
    message.setArguments({QStringLiteral("plasmaglow")});
    watchCall(message, [this, generation](const QDBusMessage &reply) {
        if (generation != m_generation) {
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setReady(false, reply.errorMessage());
            return;
        }
        const bool loaded = !reply.arguments().isEmpty() && reply.arguments().first().toBool();
        if (loaded) {
            readEffectState();
        } else {
            loadEffect();
        }
    });
}

void KWinBackend::loadEffect()
{
    const quint64 generation = m_generation;
    QDBusMessage message = QDBusMessage::createMethodCall(m_service, m_effectsPath,
                                                          m_effectsInterface, QStringLiteral("loadEffect"));
    message.setArguments({QStringLiteral("plasmaglow")});
    watchCall(message, [this, generation](const QDBusMessage &reply) {
        if (generation != m_generation) {
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setReady(false, reply.errorMessage());
            return;
        }
        if (reply.arguments().isEmpty() || !reply.arguments().first().toBool()) {
            setReady(false, QStringLiteral("KWin could not load the PlasmaGlow effect"));
            return;
        }
        readEffectState();
    });
}

void KWinBackend::readEffectState()
{
    const quint64 generation = m_generation;
    QDBusMessage message = QDBusMessage::createMethodCall(m_effectService, m_effectObjectPath,
                                                          m_effectInterface, QStringLiteral("getState"));
    watchCall(message, [this, generation](const QDBusMessage &reply) {
        if (generation != m_generation) {
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setReady(false, reply.errorMessage());
            return;
        }
        if (reply.arguments().isEmpty()) {
            setReady(false, QStringLiteral("KWin returned no PlasmaGlow state"));
            return;
        }
        acceptState(qdbus_cast<QVariantMap>(reply.arguments().first()));
    });
}

void KWinBackend::acceptState(const QVariantMap &state)
{
    const uint version = state.value(QStringLiteral("apiVersion")).toUInt();
    if (version != apiVersion) {
        setReady(false, QStringLiteral("The PlasmaGlow effect API version is incompatible"), version);
        return;
    }
    if (!state.value(QStringLiteral("ready")).toBool()) {
        const QString error = state.value(QStringLiteral("error")).toString();
        setReady(false, error.isEmpty() ? QStringLiteral("The PlasmaGlow effect is not ready") : error, version);
        return;
    }

    const double saturation = state.value(QStringLiteral("saturation")).toDouble();
    const double gamma = state.value(QStringLiteral("gamma")).toDouble();
    if (!std::isfinite(saturation) || !std::isfinite(gamma)
        || saturation < minimumSaturation || saturation > maximumSaturation
        || gamma < minimumGamma || gamma > maximumGamma) {
        setReady(false, QStringLiteral("KWin returned invalid PlasmaGlow parameters"), version);
        return;
    }

    setReady(true, QString(), version);
    Q_EMIT stateChanged(saturation, gamma);
    startNextApply();
}

void KWinBackend::setReady(bool ready, const QString &error, uint version)
{
    const bool changed = m_ready != ready || m_apiVersion != version;
    m_ready = ready;
    m_apiVersion = version;
    if (!ready) {
        ++m_generation;
        m_applyInProgress = false;
    }
    if (changed || !error.isEmpty()) {
        Q_EMIT readinessChanged(ready, error, version);
    }
    if (ready || !error.isEmpty()) {
        m_refreshInProgress = false;
    }
}

void KWinBackend::startNextApply()
{
    if (!m_ready || m_applyInProgress || !m_hasPendingParameters) {
        return;
    }

    m_activeParameters = m_pendingParameters;
    m_hasPendingParameters = false;
    m_applyInProgress = true;

    const Parameters parameters = m_activeParameters;
    const quint64 generation = m_generation;
    QDBusMessage message = QDBusMessage::createMethodCall(m_effectService, m_effectObjectPath,
                                                          m_effectInterface, QStringLiteral("setParameters"));
    message.setArguments({parameters.saturation, parameters.gamma});
    watchCall(message, [this, parameters, generation](const QDBusMessage &reply) {
        if (generation != m_generation) {
            return;
        }

        m_applyInProgress = false;
        bool success = reply.type() != QDBusMessage::ErrorMessage
            && !reply.arguments().isEmpty() && reply.arguments().first().toBool();
        QString error;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            error = reply.errorMessage();
        } else if (!success) {
            error = QStringLiteral("The PlasmaGlow effect rejected the parameters");
        }

        if (success) {
            Q_EMIT stateChanged(parameters.saturation, parameters.gamma);
        } else {
            setReady(false, error, m_apiVersion);
        }
        Q_EMIT applyFinished(parameters.requestId, success, error,
                             parameters.saturation, parameters.gamma);
        startNextApply();
    });
}
