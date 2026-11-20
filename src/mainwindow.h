#pragma once
#include "devices.h"

#include <QMainWindow>

class QLabel;
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
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
private:
    void rescan();
    void setPagesActive(bool active);
    void updateList();
    QListWidget *m_list;
    QStackedWidget *m_stack;
    QSystemTrayIcon *m_tray = nullptr;
    QList<DevicePage *> m_pages;
    QList<QLabel *> m_status;
    bool m_quitting = false;
};
