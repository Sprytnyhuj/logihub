#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("logihub");
    QApplication::setOrganizationName("logihub");
    QApplication::setApplicationDisplayName("LogiHub");
    QApplication::setDesktopFileName("logihub");
    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow w;
    // Tryb testowy: LOGIHUB_SCREENSHOT=katalog - zrzut każdej strony i wyjście (do testów offscreen).
    const QString shots = qEnvironmentVariable("LOGIHUB_SCREENSHOT");
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
