/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QFileDevice>
#include <QSaveFile>

#include <cmath>
#include <pwd.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (geteuid() != 0 || app.arguments().size() != 1) {
        return 1;
    }
    // A damaged user config must not hold up SDDM's PAM session.
    alarm(5);

    const QString service = qEnvironmentVariable("PAM_SERVICE");
    const QString operation = qEnvironmentVariable("PAM_TYPE");
    if ((service != QLatin1String("sddm") && service != QLatin1String("sddm-autologin"))
        || (operation != QLatin1String("open_session") && operation != QLatin1String("close_session"))) {
        return 1;
    }

    const QByteArray user = qgetenv("PAM_USER");
    const passwd *account = getpwnam(user.constData());
    if (!account || account->pw_uid < 1000 || !account->pw_dir) {
        return 1;
    }

    const QString configPath = QString::fromLocal8Bit(account->pw_dir)
        + QStringLiteral("/.config/plasmaglowrc");
    const KConfigGroup settings(KSharedConfig::openConfig(configPath, KConfig::SimpleConfig),
                                QStringLiteral("General"));
    const bool enabled = settings.readEntry(QStringLiteral("applyToLogin"), true);
    const double savedSaturation = settings.readEntry(QStringLiteral("saturation"), 1.0);
    const double savedGamma = settings.readEntry(QStringLiteral("gamma"), 1.0);
    const double saturation = std::isfinite(savedSaturation) && savedSaturation >= 0.0 && savedSaturation <= 4.0
        ? savedSaturation : 1.0;
    const double gamma = std::isfinite(savedGamma) && savedGamma >= 0.1 && savedGamma <= 5.0
        ? savedGamma : 1.0;

    QSaveFile file(QStringLiteral("/etc/xdg/plasmaglow-loginrc"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return 1;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                             | QFileDevice::ReadGroup | QFileDevice::ReadOther)) {
        return 1;
    }
    const QByteArray contents = QByteArray("[General]\nenabled=") + (enabled ? "true" : "false")
        + "\nsaturation=" + QByteArray::number(enabled ? saturation : 1.0, 'g', 17)
        + "\ngamma=" + QByteArray::number(enabled ? gamma : 1.0, 'g', 17) + '\n';
    return file.write(contents) == contents.size() && file.commit() ? 0 : 1;
}
