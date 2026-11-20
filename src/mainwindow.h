#pragma once
#include "devices.h"

#include <QMainWindow>

class QListWidget;
class QStackedWidget;
class QSystemTrayIcon;
class DevicePage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void showTray();
    void screenshotAll(const QString &dir);
protected:
    void closeEvent(QCloseEvent *e) override;
private:
    void rescan();
    void updateList();
    QListWidget *m_list;
    QStackedWidget *m_stack;
    QSystemTrayIcon *m_tray = nullptr;
    QList<DevicePage *> m_pages;
    bool m_quitting = false;
};
