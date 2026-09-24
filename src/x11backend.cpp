#include "x11backend.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <cmath>
#include <functional>
#include <memory>

namespace
{
constexpr int commandTimeoutMs = 1500;

bool parseNumber(const QString &text, double minimum, double maximum, double *value)
{
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok || !std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }

    *value = parsed;
    return true;
}
}

X11Backend::X11Backend(QObject *parent)
    : QObject(parent)
{
    refreshTools();
}

void X11Backend::refreshTools()
{
    m_xrandr = QStandardPaths::findExecutable(QStringLiteral("xrandr"));
    m_vibrant = QStandardPaths::findExecutable(QStringLiteral("vibrant-cli"));
    m_xgamma = QStandardPaths::findExecutable(QStringLiteral("xgamma"));
}

bool X11Backend::hasPendingApplies() const
{
    return !m_runningApplies.isEmpty() || !m_pendingApplies.isEmpty();
}

bool X11Backend::hasSaturationTool() const
{
    return !m_vibrant.isEmpty();
}

bool X11Backend::hasGammaTool() const
{
    return !m_xgamma.isEmpty();
}

void X11Backend::refreshOutputs()
{
    if (m_xrandr.isEmpty()) {
        const QString error = QStringLiteral("xrandr is not installed");
        Q_EMIT commandResult(QStringLiteral("List displays"), false, error);
        Q_EMIT outputsReady({}, false, error);
        return;
    }

    runCommand(m_xrandr, {QStringLiteral("--query")}, [this](const CommandResult &result) {
        Q_EMIT commandResult(QStringLiteral("List displays"), result.success, result.error);
        QStringList outputs;
        if (result.success) {
            const QRegularExpression connectedOutput(QStringLiteral("^([^\\s]+)\\s+connected(?:\\s|$)"));
            const QStringList lines = QString::fromUtf8(result.standardOutput).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QRegularExpressionMatch match = connectedOutput.match(line);
                if (match.hasMatch()) {
                    outputs.append(match.captured(1));
                }
            }
        }

        Q_EMIT outputsReady(outputs, result.success, result.error);
    });
}

void X11Backend::querySaturation(const QString &output, quint64 requestId)
{
    if (m_vibrant.isEmpty()) {
        const QString error = QStringLiteral("vibrant-cli is not installed");
        Q_EMIT commandResult(QStringLiteral("Read saturation"), false, error);
        Q_EMIT saturationReady(output, 1.0, false, error, requestId);
        return;
    }

    runCommand(m_vibrant, {output}, [this, output, requestId](const CommandResult &command) {
        double value = 1.0;
        bool success = command.success;
        QString error = command.error;
        if (success) {
            static const QRegularExpression saturationValue(QStringLiteral("is\\s+([-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))"));
            const QRegularExpressionMatch match = saturationValue.match(QString::fromUtf8(command.standardOutput));
            success = match.hasMatch() && parseNumber(match.captured(1), 0.0, 4.0, &value);
            if (!success) {
                error = QStringLiteral("Could not parse saturation from vibrant-cli output");
            }
        }

        Q_EMIT commandResult(QStringLiteral("Read saturation"), success, error);
        Q_EMIT saturationReady(output, value, success, error, requestId);
    });
}

void X11Backend::queryGamma(quint64 requestId)
{
    if (m_xgamma.isEmpty()) {
        const QString error = QStringLiteral("xgamma is not installed");
        Q_EMIT commandResult(QStringLiteral("Read gamma"), false, error);
        Q_EMIT gammaReady(1.0, false, error, requestId);
        return;
    }

    runCommand(m_xgamma, {}, [this, requestId](const CommandResult &command) {
        double value = 1.0;
        bool success = command.success;
        QString error = command.error;
        if (success) {
            static const QRegularExpression gammaValue(QStringLiteral("Red\\s+([0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)"));
            const QRegularExpressionMatch match = gammaValue.match(QString::fromUtf8(command.standardError));
            success = match.hasMatch() && parseNumber(match.captured(1), 0.1, 5.0, &value);
            if (!success) {
                error = QStringLiteral("Could not parse gamma from xgamma output");
            }
        }

        Q_EMIT commandResult(QStringLiteral("Read gamma"), success, error);
        Q_EMIT gammaReady(value, success, error, requestId);
    });
}

void X11Backend::applySaturation(const QString &output, double value)
{
    if (output.isEmpty() || m_vibrant.isEmpty()) {
        const QString error = output.isEmpty() ? QStringLiteral("No display output is available")
                                               : QStringLiteral("vibrant-cli is not installed");
        Q_EMIT commandResult(QStringLiteral("Apply saturation"), false, error);
        return;
    }

    enqueueApply(QStringLiteral("saturation"), {
        m_vibrant,
        {output, QString::number(value, 'f', 6)},
        QStringLiteral("Apply saturation"),
        value
    });
}

void X11Backend::applyGamma(double value)
{
    if (m_xgamma.isEmpty()) {
        const QString error = QStringLiteral("xgamma is not installed");
        Q_EMIT commandResult(QStringLiteral("Apply gamma"), false, error);
        return;
    }

    enqueueApply(QStringLiteral("gamma"), {
        m_xgamma,
        {QStringLiteral("-gamma"), QString::number(value, 'f', 3)},
        QStringLiteral("Apply gamma"),
        value
    });
}

void X11Backend::runCommand(const QString &executable,
                            const QStringList &arguments,
                            const std::function<void(const CommandResult &)> &completion)
{
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    const auto completed = std::make_shared<bool>(false);

    const auto finish = [process, timer, completed, completion](bool success, const QString &error) {
        if (*completed) {
            return;
        }
        *completed = true;
        timer->stop();

        CommandResult result;
        result.success = success;
        result.standardOutput = process->readAllStandardOutput();
        result.standardError = process->readAllStandardError();
        result.error = error;
        completion(result);
        process->deleteLater();
    };

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [process, finish](int exitCode, QProcess::ExitStatus exitStatus) {
        const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
        const QString error = success ? QString() : QStringLiteral("Process exited with code %1").arg(exitCode);
        finish(success, error);
    });

    connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        const QString message = process->errorString();
        finish(false, message);
    });

    connect(timer, &QTimer::timeout, this, [process, finish] {
        process->kill();
        const QString error = QStringLiteral("Command timed out");
        finish(false, error);
    });

    process->start(executable, arguments);
    timer->start(commandTimeoutMs);
}

void X11Backend::enqueueApply(const QString &key, const ApplyRequest &request)
{
    m_pendingApplies.insert(key, request);
    startNextApply(key);
}

void X11Backend::startNextApply(const QString &key)
{
    if (m_runningApplies.contains(key) || !m_pendingApplies.contains(key)) {
        return;
    }

    const ApplyRequest request = m_pendingApplies.take(key);
    m_runningApplies.insert(key);
    runCommand(request.executable, request.arguments, [this, key, request](const CommandResult &result) {
        Q_EMIT commandResult(request.action, result.success, result.error);
        if (key == QLatin1String("saturation")) {
            Q_EMIT saturationApplied(request.value, result.success);
        } else {
            Q_EMIT gammaApplied(request.value, result.success);
        }
        m_runningApplies.remove(key);
        startNextApply(key);
        if (!hasPendingApplies()) {
            Q_EMIT applyQueueIdle();
        }
    });
}
