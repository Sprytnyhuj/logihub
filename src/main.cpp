#include "mainwindow.h"
#include "theme.h"

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

    // logihub --export-icons <katalog hicolor>  (używane przez install.sh)
    const int ei = app.arguments().indexOf("--export-icons");
    if (ei >= 0 && ei + 1 < app.arguments().size())
        return theme::exportIcons(app.arguments().at(ei + 1)) > 0 ? 0 : 1;

    QApplication::setWindowIcon(theme::appIcon());
    theme::apply();

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
