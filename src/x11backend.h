#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <functional>

class X11Backend : public QObject
{
    Q_OBJECT

public:
    explicit X11Backend(QObject *parent = nullptr);

    bool hasSaturationTool() const;
    bool hasGammaTool() const;
    bool hasPendingApplies() const;
    void refreshTools();

    void refreshOutputs();
    void querySaturation(const QString &output, quint64 requestId);
    void queryGamma(quint64 requestId);
    void applySaturation(const QString &output, double value);
    void applyGamma(double value);

Q_SIGNALS:
    void outputsReady(const QStringList &outputs, bool success, const QString &error);
    void saturationReady(const QString &output, double value, bool success, const QString &error, quint64 requestId);
    void gammaReady(double value, bool success, const QString &error, quint64 requestId);
    void saturationApplied(double value, bool success);
    void gammaApplied(double value, bool success);
    void commandResult(const QString &action, bool success, const QString &error);
    void applyQueueIdle();

private:
    struct CommandResult {
        bool success = false;
        QByteArray standardOutput;
        QByteArray standardError;
        QString error;
    };

    struct ApplyRequest {
        QString executable;
        QStringList arguments;
        QString action;
        double value = 1.0;
    };

    QString m_xrandr;
    QString m_vibrant;
    QString m_xgamma;
    QHash<QString, ApplyRequest> m_pendingApplies;
    QSet<QString> m_runningApplies;

    void runCommand(const QString &executable,
                    const QStringList &arguments,
                    const std::function<void(const CommandResult &)> &completion);
    void enqueueApply(const QString &key, const ApplyRequest &request);
    void startNextApply(const QString &key);
};
