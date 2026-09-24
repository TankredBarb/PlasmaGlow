#include "glowcontroller.h"

#include "kwinbackend.h"
#include "x11backend.h"

#include <QGuiApplication>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QTimer>

#include <cmath>

namespace
{
constexpr double minimumSaturation = 0.0;
constexpr double maximumSaturation = 4.0;
constexpr double minimumGamma = 0.1;
constexpr double maximumGamma = 5.0;
constexpr unsigned outputSetting = 1;
constexpr unsigned saturationSetting = 2;
constexpr unsigned gammaSetting = 4;

double readSetting(KConfigGroup &group, const QString &key, double fallback, double minimum, double maximum)
{
    const double value = group.readEntry(key, fallback);
    return std::isfinite(value) && value >= minimum && value <= maximum ? value : fallback;
}
}

GlowController::GlowController(QObject *parent)
    : QObject(parent)
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    KConfigGroup group(config, QStringLiteral("General"));
    m_output = group.readEntry(QStringLiteral("output"), QString());
    m_saturation = readSetting(group, QStringLiteral("saturation"), 1.0, minimumSaturation, maximumSaturation);
    m_gamma = readSetting(group, QStringLiteral("gamma"), 1.0, minimumGamma, maximumGamma);
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    connect(m_saveTimer, &QTimer::timeout, this, &GlowController::flushSettings);

    if (!isX11()) {
        m_kwinBackend = new KWinBackend(this);
        connect(m_kwinBackend, &KWinBackend::readinessChanged, this,
                [this](bool ready, const QString &error, uint) {
            const bool capabilitiesChangedValue = m_saturationAvailable != ready
                || m_gammaAvailable != ready || m_backendReady != ready;
            m_backendReady = ready;
            m_saturationAvailable = ready;
            m_gammaAvailable = ready;
            if (capabilitiesChangedValue) {
                Q_EMIT capabilitiesChanged();
            }
            m_backendError = error;
            updateError();
            if (ready && !m_kwinBackend->isRefreshing()) {
                applyWaylandParameters(m_saturation, m_gamma);
            }
        });
        connect(m_kwinBackend, &KWinBackend::stateChanged, this,
                [this](double saturation, double gamma) {
            bool changed = false;
            if (!qFuzzyCompare(m_appliedSaturation, saturation)
                || !qFuzzyCompare(m_appliedGamma, gamma)) {
                m_appliedSaturation = saturation;
                m_appliedGamma = gamma;
                changed = true;
            }
            if (!m_localApplyPending
                && (!qFuzzyCompare(m_saturation, saturation) || !qFuzzyCompare(m_gamma, gamma))) {
                m_saturation = saturation;
                m_gamma = gamma;
                Q_EMIT saturationChanged();
                Q_EMIT gammaChanged();
                saveSettings(saturationSetting | gammaSetting);
            }
            if (changed) {
                Q_EMIT appliedStateChanged();
            }
        });
        connect(m_kwinBackend, &KWinBackend::applyFinished, this,
                [this](quint64 requestId, bool success, const QString &error,
                       double saturation, double gamma) {
            if (requestId != m_latestApplyRequestId) {
                return;
            }
            m_backendError = success ? QString() : error;
            updateError();
            if (!success) {
                m_localApplyPending = false;
                return;
            }
            m_localApplyPending = false;
            if (qFuzzyCompare(m_appliedSaturation, saturation)
                && qFuzzyCompare(m_appliedGamma, gamma)) {
                return;
            }
            m_appliedSaturation = saturation;
            m_appliedGamma = gamma;
            Q_EMIT appliedStateChanged();
        });
        return;
    }

    m_x11Backend = new X11Backend(this);
    m_hasSaturation = m_x11Backend->hasSaturationTool();
    m_hasXGamma = m_x11Backend->hasGammaTool();
    m_gammaAvailable = m_hasXGamma;
    m_backendReady = true;

    connect(m_x11Backend, &X11Backend::commandResult, this,
            [this](const QString &action, bool success, const QString &error) {
        if (success) {
            m_x11Errors.remove(action);
        } else {
            m_x11Errors.insert(action, action + QStringLiteral(": ") + error);
        }
        updateError();
    });
    connect(m_x11Backend, &X11Backend::applyQueueIdle, this, [this] {
        if (m_refreshPendingX11) {
            m_refreshPendingX11 = false;
            refresh();
        }
    });
    connect(m_x11Backend, &X11Backend::outputsReady, this, &GlowController::handleOutputsReady);
    connect(m_x11Backend, &X11Backend::saturationReady, this,
            [this](const QString &output, double value, bool success, const QString &, quint64 requestId) {
        if (!success || output != m_output || requestId != m_saturationReadRequestId) {
            return;
        }
        if (!qFuzzyCompare(m_saturation, value)) {
            m_saturation = value;
            Q_EMIT saturationChanged();
            saveSettings(saturationSetting);
        }
        if (!qFuzzyCompare(m_appliedSaturation, value)) {
            m_appliedSaturation = value;
            Q_EMIT appliedStateChanged();
        }
    });
    connect(m_x11Backend, &X11Backend::gammaReady, this,
            [this](double value, bool success, const QString &, quint64 requestId) {
        if (!success || requestId != m_gammaReadRequestId) {
            return;
        }
        if (!qFuzzyCompare(m_gamma, value)) {
            m_gamma = value;
            Q_EMIT gammaChanged();
            saveSettings(gammaSetting);
        }
        if (!qFuzzyCompare(m_appliedGamma, value)) {
            m_appliedGamma = value;
            Q_EMIT appliedStateChanged();
        }
    });
    connect(m_x11Backend, &X11Backend::saturationApplied, this, [this](double value, bool success) {
        if (!success || qFuzzyCompare(m_appliedSaturation, value)) {
            return;
        }
        m_appliedSaturation = value;
        Q_EMIT appliedStateChanged();
    });
    connect(m_x11Backend, &X11Backend::gammaApplied, this, [this](double value, bool success) {
        if (!success || qFuzzyCompare(m_appliedGamma, value)) {
            return;
        }
        m_appliedGamma = value;
        Q_EMIT appliedStateChanged();
    });

    if (m_hasXGamma) {
        applyGamma(m_gamma);
    }
    m_x11Backend->refreshOutputs();
}

GlowController::~GlowController()
{
    flushSettings();
}

bool GlowController::isX11() const
{
    return QGuiApplication::platformName() == QLatin1String("xcb");
}

bool GlowController::hasSaturation() const
{
    return m_hasSaturation;
}

bool GlowController::saturationAvailable() const
{
    return m_saturationAvailable;
}

bool GlowController::gammaAvailable() const
{
    return m_gammaAvailable;
}

bool GlowController::backendReady() const
{
    return m_backendReady;
}

bool GlowController::hasXGamma() const
{
    return m_hasXGamma;
}

QString GlowController::error() const
{
    return m_error;
}

double GlowController::appliedSaturation() const
{
    return m_appliedSaturation;
}

double GlowController::appliedGamma() const
{
    return m_appliedGamma;
}

double GlowController::saturation() const
{
    return m_saturation;
}

void GlowController::setSaturation(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    value = qBound(minimumSaturation, value, maximumSaturation);

    if (qFuzzyCompare(m_saturation, value)) {
        return;
    }

    m_saturation = value;
    ++m_userActionRevision;
    ++m_saturationReadRequestId;
    Q_EMIT saturationChanged();
    saveSettings(saturationSetting);
    applySaturation(m_saturation);
}

double GlowController::gamma() const
{
    return m_gamma;
}

void GlowController::setGamma(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    value = qBound(minimumGamma, value, maximumGamma);

    if (qFuzzyCompare(m_gamma, value)) {
        return;
    }

    m_gamma = value;
    ++m_userActionRevision;
    ++m_gammaReadRequestId;
    Q_EMIT gammaChanged();
    saveSettings(gammaSetting);
    applyGamma(m_gamma);
}

QString GlowController::output() const
{
    return m_output;
}

void GlowController::setOutput(const QString &output)
{
    if (m_output == output || output.isEmpty() || !m_outputs.contains(output)) {
        return;
    }

    m_output = output;
    ++m_userActionRevision;
    ++m_saturationReadRequestId;
    Q_EMIT outputChanged();
    saveSettings(outputSetting);

    if (m_x11Backend && m_hasSaturation) {
        m_x11Backend->querySaturation(m_output, ++m_saturationReadRequestId);
    }
}

QStringList GlowController::outputs() const
{
    return m_outputs;
}

void GlowController::refresh()
{
    if (m_kwinBackend) {
        m_localApplyPending = false;
        m_kwinBackend->refresh();
        return;
    }
    if (!m_x11Backend) {
        return;
    }

    m_x11Backend->refreshTools();
    const bool hasSaturation = m_x11Backend->hasSaturationTool();
    const bool hasXGamma = m_x11Backend->hasGammaTool();
    const bool saturationToolAdded = !m_hasSaturation && hasSaturation;
    const bool gammaToolAdded = !m_hasXGamma && hasXGamma;
    if (m_hasSaturation != hasSaturation || m_hasXGamma != hasXGamma) {
        m_hasSaturation = hasSaturation;
        m_hasXGamma = hasXGamma;
        m_gammaAvailable = hasXGamma;
        m_saturationAvailable = hasSaturation && !m_outputs.isEmpty();
        Q_EMIT capabilitiesChanged();
    }
    if (gammaToolAdded) {
        applyGamma(m_gamma);
    }
    if (saturationToolAdded && !m_output.isEmpty()) {
        applySaturation(m_saturation);
    }
    if (m_x11Backend->hasPendingApplies()) {
        m_refreshPendingX11 = true;
        return;
    }

    m_refreshUserActionRevision = m_userActionRevision;
    if (m_hasXGamma) {
        m_x11Backend->queryGamma(++m_gammaReadRequestId);
    }
    m_x11Backend->refreshOutputs();
}

void GlowController::reset()
{
    const bool saturationWasChanged = !qFuzzyCompare(m_saturation, 1.0);
    const bool gammaWasChanged = !qFuzzyCompare(m_gamma, 1.0);
    m_saturation = 1.0;
    m_gamma = 1.0;
    ++m_userActionRevision;
    ++m_saturationReadRequestId;
    ++m_gammaReadRequestId;
    if (saturationWasChanged) {
        Q_EMIT saturationChanged();
    }
    if (gammaWasChanged) {
        Q_EMIT gammaChanged();
    }
    saveSettings(saturationSetting | gammaSetting);

    if (m_kwinBackend) {
        applyWaylandParameters(m_saturation, m_gamma);
        return;
    }
    applySaturation(m_saturation);
    if (m_hasXGamma) {
        applyGamma(m_gamma);
    }
}

void GlowController::applySaturation(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    value = qBound(minimumSaturation, value, maximumSaturation);
    if (m_kwinBackend) {
        applyWaylandParameters(value, m_gamma);
        return;
    }
    if (m_x11Backend && m_hasSaturation && !m_output.isEmpty()) {
        m_x11Backend->applySaturation(m_output, value);
    }
}

void GlowController::applyGamma(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    value = qBound(minimumGamma, value, maximumGamma);
    if (m_kwinBackend) {
        applyWaylandParameters(m_saturation, value);
        return;
    }
    if (m_x11Backend && m_hasXGamma) {
        m_x11Backend->applyGamma(value);
    }
}

void GlowController::saveSettings(unsigned fields)
{
    m_dirtySettings |= fields;
    m_saveTimer->start(150);
}

void GlowController::flushSettings()
{
    if (!m_dirtySettings) {
        return;
    }
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    KConfigGroup group(config, QStringLiteral("General"));
    if (m_dirtySettings & outputSetting) {
        group.writeEntry(QStringLiteral("output"), m_output);
    }
    if (m_dirtySettings & saturationSetting) {
        group.writeEntry(QStringLiteral("saturation"), m_saturation);
    }
    if (m_dirtySettings & gammaSetting) {
        group.writeEntry(QStringLiteral("gamma"), m_gamma);
    }
    if (group.sync()) {
        m_dirtySettings = 0;
        m_settingsError.clear();
    } else {
        m_settingsError = QStringLiteral("Could not save PlasmaGlow settings");
        m_saveTimer->start(2000);
    }
    updateError();
}

void GlowController::updateError()
{
    const QString backendError = m_x11Backend && !m_x11Errors.isEmpty()
        ? m_x11Errors.constBegin().value() : m_backendError;
    setError(m_settingsError.isEmpty() ? backendError : m_settingsError);
}

void GlowController::setError(const QString &error)
{
    if (m_error == error) {
        return;
    }
    m_error = error;
    Q_EMIT errorChanged();
}

void GlowController::handleOutputsReady(const QStringList &outputs, bool success, const QString &error)
{
    Q_UNUSED(error)
    if (!success) {
        if (m_saturationAvailable) {
            m_saturationAvailable = false;
            Q_EMIT capabilitiesChanged();
        }
        return;
    }

    const bool wasAvailable = m_saturationAvailable;
    if (m_outputs != outputs) {
        m_outputs = outputs;
        Q_EMIT outputsChanged();
    }
    const bool saturationAvailable = m_hasSaturation && !m_outputs.isEmpty();
    if (m_saturationAvailable != saturationAvailable) {
        m_saturationAvailable = saturationAvailable;
        Q_EMIT capabilitiesChanged();
    }
    if (m_outputs.isEmpty()) {
        return;
    }

    const bool selectedOutputMissing = !m_outputs.contains(m_output);
    if (selectedOutputMissing) {
        m_output = m_outputs.first();
        Q_EMIT outputChanged();
        saveSettings(outputSetting);
    }

    if (!m_initialSaturationApplied || !wasAvailable || selectedOutputMissing) {
        applyLoadedSaturation();
        m_initialSaturationApplied = true;
        return;
    }

    if (m_refreshUserActionRevision != m_userActionRevision) {
        return;
    }

    if (!m_hasSaturation) {
        return;
    }

    if (m_x11Backend->hasPendingApplies()) {
        m_refreshPendingX11 = true;
        return;
    }

    m_x11Backend->querySaturation(m_output, ++m_saturationReadRequestId);
}

void GlowController::applyLoadedSaturation()
{
    applySaturation(m_saturation);
}

void GlowController::applyWaylandParameters(double saturation, double gamma)
{
    if (!m_kwinBackend) {
        return;
    }
    m_localApplyPending = true;
    m_latestApplyRequestId = m_kwinBackend->applyParameters(saturation, gamma);
}
