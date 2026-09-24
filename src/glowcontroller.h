#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QtQml/QQmlEngine>

#include <QtTypes>

class X11Backend;
class KWinBackend;
class QTimer;

class GlowController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(QStringList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(bool isX11 READ isX11 CONSTANT)
    Q_PROPERTY(bool hasSaturation READ hasSaturation NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool saturationAvailable READ saturationAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool gammaAvailable READ gammaAvailable NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool backendReady READ backendReady NOTIFY capabilitiesChanged)
    Q_PROPERTY(double gamma READ gamma WRITE setGamma NOTIFY gammaChanged)
    Q_PROPERTY(bool hasXGamma READ hasXGamma NOTIFY capabilitiesChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(double appliedSaturation READ appliedSaturation NOTIFY appliedStateChanged)
    Q_PROPERTY(double appliedGamma READ appliedGamma NOTIFY appliedStateChanged)

public:
    explicit GlowController(QObject *parent = nullptr);
    ~GlowController() override;

    bool isX11() const;
    bool hasSaturation() const;
    bool saturationAvailable() const;
    bool gammaAvailable() const;
    bool backendReady() const;
    bool hasXGamma() const;
    QString error() const;
    double appliedSaturation() const;
    double appliedGamma() const;

    double saturation() const;
    void setSaturation(double value);

    double gamma() const;
    void setGamma(double value);

    QString output() const;
    void setOutput(const QString &output);

    QStringList outputs() const;

Q_SIGNALS:
    void saturationChanged();
    void outputChanged();
    void outputsChanged();
    void gammaChanged();
    void errorChanged();
    void capabilitiesChanged();
    void appliedStateChanged();

public Q_SLOTS:
    void refresh();
    void reset();
    void applySaturation(double value);
    void applyGamma(double value);

private:
    double m_saturation = 1.0;
    double m_gamma = 1.0;
    QString m_output;
    QStringList m_outputs;
    QString m_error;
    QString m_backendError;
    QString m_settingsError;
    QHash<QString, QString> m_x11Errors;
    QTimer *m_saveTimer = nullptr;
    unsigned m_dirtySettings = 0;
    X11Backend *m_x11Backend = nullptr;
    KWinBackend *m_kwinBackend = nullptr;
    bool m_hasSaturation = false;
    bool m_hasXGamma = false;
    bool m_saturationAvailable = false;
    bool m_gammaAvailable = false;
    bool m_backendReady = false;
    bool m_initialSaturationApplied = false;
    bool m_localApplyPending = false;
    bool m_refreshPendingX11 = false;
    double m_appliedSaturation = 1.0;
    double m_appliedGamma = 1.0;
    quint64 m_latestApplyRequestId = 0;
    quint64 m_userActionRevision = 0;
    quint64 m_refreshUserActionRevision = 0;
    quint64 m_saturationReadRequestId = 0;
    quint64 m_gammaReadRequestId = 0;

    void saveSettings(unsigned fields);
    void flushSettings();
    void updateError();
    void setError(const QString &error);
    void handleOutputsReady(const QStringList &outputs, bool success, const QString &error);
    void applyLoadedSaturation();
    void applyWaylandParameters(double saturation, double gamma);
};
