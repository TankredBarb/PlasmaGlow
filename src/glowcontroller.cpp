#include "glowcontroller.h"
#include <QProcess>
#include <QDebug>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QStandardPaths>
#include <KSharedConfig>
#include <KConfigGroup>

GlowController::GlowController(QObject *parent)
    : QObject(parent)
{
    // Check if xgamma is installed on the system
    m_hasXGamma = !QStandardPaths::findExecutable(QStringLiteral("xgamma")).isEmpty();

    // Load saved configuration from plasmaglowrc
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    KConfigGroup group(config, QStringLiteral("General"));
    m_output = group.readEntry(QStringLiteral("output"), QString());
    m_saturation = group.readEntry(QStringLiteral("saturation"), 1.0);
    m_gamma = group.readEntry(QStringLiteral("gamma"), 1.0);

    // Scan for current active outputs
    detectOutputs();

    if (!m_outputs.isEmpty()) {
        if (m_output.isEmpty() || !m_outputs.contains(m_output)) {
            m_output = m_outputs.first();
        }
    } else {
        m_output = QStringLiteral("DisplayPort-0");
    }

    // Apply the loaded saturation and gamma values at startup
    applySaturation(m_saturation);
    if (m_hasXGamma) {
        applyGamma(m_gamma);
    }
}

bool GlowController::isX11() const
{
    return QGuiApplication::platformName() == QLatin1String("xcb");
}

bool GlowController::hasXGamma() const
{
    return m_hasXGamma;
}

double GlowController::saturation() const
{
    return m_saturation;
}

void GlowController::setSaturation(double value)
{
    if (value < 0.0) value = 0.0;
    if (value > 4.0) value = 4.0;

    if (qFuzzyCompare(m_saturation, value)) {
        return;
    }

    m_saturation = value;
    Q_EMIT saturationChanged();

    // Save saturation to config
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    KConfigGroup group(config, QStringLiteral("General"));
    group.writeEntry(QStringLiteral("saturation"), m_saturation);
    group.sync();

    applySaturation(m_saturation);
}

double GlowController::gamma() const
{
    return m_gamma;
}

void GlowController::setGamma(double value)
{
    if (value < 0.1) value = 0.1;
    if (value > 5.0) value = 5.0; // standard xgamma range limit

    if (qFuzzyCompare(m_gamma, value)) {
        return;
    }

    m_gamma = value;
    Q_EMIT gammaChanged();

    // Save gamma to config
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    KConfigGroup group(config, QStringLiteral("General"));
    group.writeEntry(QStringLiteral("gamma"), m_gamma);
    group.sync();

    applyGamma(m_gamma);
}

QString GlowController::output() const
{
    return m_output;
}

void GlowController::setOutput(const QString &output)
{
    if (m_output == output || output.isEmpty()) {
        return;
    }

    m_output = output;
    Q_EMIT outputChanged();

    // Save output to config
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
    KConfigGroup group(config, QStringLiteral("General"));
    group.writeEntry(QStringLiteral("output"), m_output);
    group.sync();

    double currentSat = querySaturation(m_output);
    if (!qFuzzyCompare(m_saturation, currentSat)) {
        m_saturation = currentSat;
        Q_EMIT saturationChanged();

        group.writeEntry(QStringLiteral("saturation"), m_saturation);
        group.sync();
    }
}

QStringList GlowController::outputs() const
{
    return m_outputs;
}

void GlowController::refresh()
{
    detectOutputs();

    if (!m_outputs.isEmpty()) {
        if (m_output.isEmpty() || !m_outputs.contains(m_output)) {
            m_output = m_outputs.first();
            Q_EMIT outputChanged();

            // Save output to config
            auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
            KConfigGroup group(config, QStringLiteral("General"));
            group.writeEntry(QStringLiteral("output"), m_output);
            group.sync();
        }
    } else {
        m_output = QStringLiteral("DisplayPort-0");
        Q_EMIT outputChanged();
    }

    double currentSat = querySaturation(m_output);
    if (!qFuzzyCompare(m_saturation, currentSat)) {
        m_saturation = currentSat;
        Q_EMIT saturationChanged();

        // Save saturation to config
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
        KConfigGroup group(config, QStringLiteral("General"));
        group.writeEntry(QStringLiteral("saturation"), m_saturation);
        group.sync();
    }

    if (m_hasXGamma) {
        double currentGamma = queryGamma();
        if (!qFuzzyCompare(m_gamma, currentGamma)) {
            m_gamma = currentGamma;
            Q_EMIT gammaChanged();

            // Save gamma to config
            auto config = KSharedConfig::openConfig(QStringLiteral("plasmaglowrc"));
            KConfigGroup group(config, QStringLiteral("General"));
            group.writeEntry(QStringLiteral("gamma"), m_gamma);
            group.sync();
        }
    }
}

void GlowController::applySaturation(double value)
{
    if (m_output.isEmpty()) {
        return;
    }

    QStringList args;
    args << m_output << QString::number(value, 'f', 6);
    
    QProcess::startDetached(QStringLiteral("vibrant-cli"), args);
}

void GlowController::applyGamma(double value)
{
    if (!m_hasXGamma) {
        return;
    }

    QStringList args;
    args << QStringLiteral("-gamma") << QString::number(value, 'f', 3);
    
    QProcess::startDetached(QStringLiteral("xgamma"), args);
}

void GlowController::detectOutputs()
{
    m_outputs.clear();
    QProcess process;
    process.start(QStringLiteral("xrandr"), QStringList() << QStringLiteral("--query"));
    if (process.waitForFinished(1000)) {
        QString out = QString::fromUtf8(process.readAllStandardOutput());
        QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.contains(QStringLiteral(" connected"))) {
                QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (!parts.isEmpty()) {
                    m_outputs.append(parts.first());
                }
            }
        }
    }

    if (m_outputs.isEmpty()) {
        m_outputs.append(QStringLiteral("DisplayPort-0"));
    }

    Q_EMIT outputsChanged();
}

double GlowController::querySaturation(const QString &outputName)
{
    if (outputName.isEmpty()) {
        return 1.0;
    }

    QProcess process;
    process.start(QStringLiteral("vibrant-cli"), QStringList() << outputName);
    if (process.waitForFinished(1000)) {
        QString out = QString::fromUtf8(process.readAllStandardOutput());
        QRegularExpression re(QStringLiteral("is\\s+([0-9.]+)"));
        QRegularExpressionMatch match = re.match(out);
        if (match.hasMatch()) {
            bool ok = false;
            double val = match.captured(1).toDouble(&ok);
            if (ok) {
                return val;
            }
        }
    }
    return 1.0;
}

double GlowController::queryGamma()
{
    if (!m_hasXGamma) {
        return 1.0;
    }

    QProcess process;
    process.start(QStringLiteral("xgamma"), QStringList());
    if (process.waitForFinished(1000)) {
        // xgamma prints output to stderr
        QString out = QString::fromUtf8(process.readAllStandardError());
        QRegularExpression re(QStringLiteral("Red\\s+([0-9.]+)"));
        QRegularExpressionMatch match = re.match(out);
        if (match.hasMatch()) {
            bool ok = false;
            double val = match.captured(1).toDouble(&ok);
            if (ok) {
                return val;
            }
        }
    }
    return 1.0;
}
