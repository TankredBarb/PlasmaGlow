#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/QQmlEngine>

class GlowController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(QStringList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(bool isX11 READ isX11 CONSTANT)
    Q_PROPERTY(double gamma READ gamma WRITE setGamma NOTIFY gammaChanged)
    Q_PROPERTY(bool hasXGamma READ hasXGamma CONSTANT)

public:
    explicit GlowController(QObject *parent = nullptr);

    bool isX11() const;
    bool hasXGamma() const;

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

public Q_SLOTS:
    void refresh();
    void applySaturation(double value);
    void applyGamma(double value);

private:
    double m_saturation = 1.0;
    double m_gamma = 1.0;
    QString m_output;
    QStringList m_outputs;
    bool m_hasXGamma = false;

    void detectOutputs();
    double querySaturation(const QString &outputName);
    double queryGamma();
};
