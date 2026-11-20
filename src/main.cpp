#include "mainwindow.h"
#include "theme.h"
#include "updater.h"

#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("logihub");
    QApplication::setOrganizationName("logihub");
    QApplication::setApplicationDisplayName("LogiHub");
    QApplication::setDesktopFileName("logihub");
    QApplication::setQuitOnLastWindowClosed(false);

    // logihub --export-icons <katalog hicolor>  (używane przez install.sh)
    const int ei = app.arguments().indexOf("--export-icons");
    if (ei >= 0 && ei + 1 < app.arguments().size())
        return theme::exportIcons(app.arguments().at(ei + 1)) > 0 ? 0 : 1;

    // logihub --check-update  - sprawdza GitHub Releases i wypisuje wynik (bez okna)
    if (app.arguments().contains("--check-update")) {
        Updater u;
        int rc = 2;
        QObject::connect(&u, &Updater::updateAvailable, [&](const QString &v, const QString &, bool) {
            std::printf("dostępna aktualizacja: %s (obecna %s)\n", qPrintable(v), qPrintable(Updater::currentVersion()));
            rc = 0;
            app.quit();
        });
        QObject::connect(&u, &Updater::upToDate, [&](bool) {
            std::printf("aktualna wersja: %s (najnowsza %s)\n", qPrintable(Updater::currentVersion()),
                        qPrintable(u.latestVersion()));
            rc = 0;
            app.quit();
        });
        QObject::connect(&u, &Updater::failed, [&](const QString &m, bool) {
            std::printf("błąd: %s\n", qPrintable(m));
            app.quit();
        });
        u.check(true);
        app.exec();
        return rc;
    }

    // Tryb testowy: LOGIHUB_SCREENSHOT=katalog - zrzut każdej strony i wyjście (do testów offscreen).
    const QString shots = qEnvironmentVariable("LOGIHUB_SCREENSHOT");

    // Jedna instancja: ponowne uruchomienie pokazuje okno już działającego LogiHub
    // (ważne np. na GNOME bez zasobnika, gdzie nie ma ikony do kliknięcia).
    // Gniazdo w XDG_RUNTIME_DIR (prywatny katalog użytkownika), osobne dla każdego katalogu konfiguracji.
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString sockName = QDir(qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()))
                                 .filePath(QString("logihub-%1.sock")
                                               .arg(QString::number(qHash(cfg), 16)));
    QLocalServer server;
    if (shots.isEmpty()) {
        QLocalSocket probe;
        probe.connectToServer(sockName);
        if (probe.waitForConnected(300)) {
            // Token aktywacji Wayland (od menu / paska zadań) - bez niego kompozytor nie pozwoli
            // działającemu oknu wyjść na wierzch.
            probe.write("show " + qgetenv("XDG_ACTIVATION_TOKEN") + "\n");
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
            return 0;
        }
        QLocalServer::removeServer(sockName);
        server.listen(sockName);
    }

    QApplication::setWindowIcon(theme::appIcon());
    theme::apply();

    MainWindow w;
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&] {
        QLocalSocket *c = server.nextPendingConnection();
        if (!c)
            return;
        auto handle = [&w, c] {
            const QByteArray msg = c->readAll().trimmed();
            c->deleteLater();
            const QByteArray token = msg.startsWith("show ") ? msg.mid(5).trimmed() : QByteArray();
            if (!token.isEmpty()) {
                // Qt Wayland używa tego tokenu przy tworzeniu powierzchni okna - dlatego okno
                // zminimalizowane / w tle chowamy i pokazujemy ponownie, żeby wyszło na wierzch.
                qputenv("XDG_ACTIVATION_TOKEN", token);
                if (w.isVisible())
                    w.hide();
            }
            w.showNormal();
            w.raise();
            w.activateWindow();
        };
        if (c->bytesAvailable() > 0 || c->waitForReadyRead(300))
            handle();
        else
            QObject::connect(c, &QLocalSocket::readyRead, &w, handle);
    });
    if (!shots.isEmpty()) {
        w.show();
        QTimer::singleShot(5000, &app, [&] {
            w.screenshotAll(shots);
            app.quit();
        });
        return app.exec();
    }
    w.showTray();
    if (!app.arguments().contains("--hidden"))
        w.show();
    return app.exec();
}
