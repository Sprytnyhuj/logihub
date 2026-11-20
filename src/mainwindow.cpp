#include "mainwindow.h"
#include "devicepage.h"
#include "dsp.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString autostartFile()
{
    return QDir::homePath() + "/.config/autostart/logihub.desktop";
}

// Karta urządzenia w panelu bocznym.
QWidget *deviceItem(const QString &icon, const QString &name, QLabel **status)
{
    auto *w = new QWidget;
    w->setAttribute(Qt::WA_TranslucentBackground);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(12, 10, 12, 10);
    h->setSpacing(12);
    auto *tile = new QLabel;
    tile->setPixmap(theme::deviceTile(icon, 44));
    h->addWidget(tile);
    auto *v = new QVBoxLayout;
    v->setSpacing(2);
    auto *n = new QLabel(name);
    n->setObjectName("devName");
    auto *s = new QLabel;
    s->setObjectName("devStatus");
    v->addWidget(n);
    v->addWidget(s);
    h->addLayout(v, 1);
    *status = s;
    return w;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("LogiHub");
    setWindowIcon(theme::appIcon());
    resize(1240, 1000);

    auto *central = new QWidget;
    auto *h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(280);
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(0, 24, 0, 16);
    sv->setSpacing(4);
    auto *brand = new QLabel("LOGIHUB");
    brand->setObjectName("brand");
    auto *brandSub = new QLabel(tr("urządzenia Logitech G na Linuksie"));
    brandSub->setObjectName("brandSub");
    auto *title = new QLabel(tr("URZĄDZENIA"));
    title->setObjectName("sidebarTitle");
    for (auto *l : {brandSub, title})
        l->setContentsMargins(22, 0, 22, 0);
    auto *brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(20, 0, 20, 0);
    brandRow->setSpacing(10);
    auto *logo = new QLabel;
    const qreal dpr = devicePixelRatioF();
    QPixmap lp = theme::appIconPixmap(qRound(38 * dpr));
    lp.setDevicePixelRatio(dpr);
    logo->setPixmap(lp);
    brandRow->addWidget(logo);
    brandRow->addWidget(brand, 1);
    sv->addLayout(brandRow);
    sv->addSpacing(4);
    sv->addWidget(brandSub);
    sv->addSpacing(22);
    sv->addWidget(title);
    sv->addSpacing(4);
    m_list = new QListWidget;
    m_list->setObjectName("devices");
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sv->addWidget(m_list, 1);

    m_stack = new QStackedWidget;
    m_stack->setObjectName("mainStack");
    auto *empty = new QLabel(tr("Nie znaleziono urządzeń Logitech.\nPodłącz urządzenie albo wybudź mysz."));
    empty->setObjectName("hint");
    empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(empty);
    h->addWidget(sidebar);
    h->addWidget(m_stack, 1);
    setCentralWidget(central);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_stack->setCurrentIndex(row >= 0 && row < m_pages.size() ? row + 1 : 0);
    });

    auto *menu = menuBar()->addMenu(tr("&Aplikacja"));
    auto *autostart = menu->addAction(tr("Uruchamiaj przy logowaniu"));
    autostart->setCheckable(true);
    autostart->setChecked(QFile::exists(autostartFile()));
    connect(autostart, &QAction::toggled, this, [](bool on) {
        if (!on) {
            QFile::remove(autostartFile());
            return;
        }
        QDir().mkpath(QFileInfo(autostartFile()).path());
        QFile f(autostartFile());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QString("[Desktop Entry]\nType=Application\nName=LogiHub\n"
                            "Comment=Sterowanie urządzeniami Logitech\nExec=%1 --hidden\n"
                            "Icon=logihub\nX-GNOME-Autostart-enabled=true\n")
                        .arg(QApplication::applicationFilePath())
                        .toUtf8());
        }
    });
    menu->addAction(tr("Odśwież urządzenia"), this, &MainWindow::rescan);
    menu->addSeparator();
    menu->addAction(tr("Zakończ"), this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    connect(qApp, &QApplication::aboutToQuit, this, [] { DspRunner::instance().stopAll(); });

    // Wykrywanie podłączenia: powiadomienia z /dev (inotify) zamiast odpytywania co kilka sekund.
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(800);
    connect(debounce, &QTimer::timeout, this, &MainWindow::rescan);
    auto *watcher = new QFileSystemWatcher({"/dev"}, this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, debounce, qOverload<>(&QTimer::start));
    auto *fallback = new QTimer(this); // zapas, gdyby inotify coś przegapił
    connect(fallback, &QTimer::timeout, this, &MainWindow::rescan);
    fallback->start(60000);
    rescan();
}

void MainWindow::setPagesActive(bool active)
{
    for (DevicePage *p : m_pages)
        p->setActive(active);
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    setPagesActive(true);
}

void MainWindow::hideEvent(QHideEvent *e)
{
    QMainWindow::hideEvent(e);
    setPagesActive(false);
}

void MainWindow::showTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(windowIcon(), this);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Pokaż LogiHub"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(tr("Zakończ"), this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger)
            isVisible() ? hide() : (showNormal(), raise(), activateWindow());
    });
    m_tray->show();
    updateList();
}

void MainWindow::screenshotAll(const QString &dir)
{
    QDir().mkpath(dir);
    for (int i = 0; i < m_pages.size(); ++i) {
        m_list->setCurrentRow(i);
        QApplication::processEvents();
        auto *tabs = m_pages[i]->findChild<QTabWidget *>();
        const int n = tabs ? tabs->count() : 1;
        for (int t = 0; t < n; ++t) {
            if (tabs)
                tabs->setCurrentIndex(t);
            QApplication::processEvents();
            grab().save(QString("%1/dev%2-tab%3.png").arg(dir).arg(i).arg(t));
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_tray && !m_quitting) {
        hide(); // jak G Hub - działa dalej w zasobniku (efekty audio też)
        e->ignore();
        return;
    }
    e->accept();
}

void MainWindow::rescan()
{
    const QList<DeviceInfo> found = scanDevices();
    bool changed = false;
    for (int i = m_pages.size() - 1; i >= 0; --i) {
        if (!found.contains(m_pages[i]->info())) {
            DevicePage *p = m_pages.takeAt(i);
            m_stack->removeWidget(p);
            p->deleteLater();
            delete m_list->takeItem(i);
            m_status.removeAt(i);
            changed = true;
        }
    }
    for (const DeviceInfo &d : found) {
        bool have = false;
        for (DevicePage *p : m_pages)
            have |= p->info() == d;
        if (have)
            continue;
        auto *page = new DevicePage(d);
        connect(page, &DevicePage::statusChanged, this, &MainWindow::updateList);
        page->setActive(isVisible());
        m_pages.append(page);
        m_stack->addWidget(page);
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(260, 68));
        m_list->addItem(item);
        QLabel *status;
        m_list->setItemWidget(item, deviceItem(page->iconName(), d.name, &status));
        m_status.append(status);
        changed = true;
    }
    if (changed) {
        for (int i = 0; i < m_pages.size(); ++i) {
            m_stack->removeWidget(m_pages[i]);
            m_stack->insertWidget(i + 1, m_pages[i]);
        }
        if (m_list->currentRow() < 0 && !m_pages.isEmpty())
            m_list->setCurrentRow(0);
        m_stack->setCurrentIndex(m_list->currentRow() + 1);
        updateList();
    }
}

void MainWindow::updateList()
{
    QStringList tip{"LogiHub"};
    for (int i = 0; i < m_pages.size() && i < m_status.size(); ++i) {
        const QString st = m_pages[i]->statusText();
        const DeviceInfo &d = m_pages[i]->info();
        m_status[i]->setText(st.isEmpty() ? (d.wireless ? tr("Lightspeed") : tr("USB"))
                                          : QString("🔋 %1").arg(st));
        if (!st.isEmpty())
            tip << QString("%1: %2").arg(d.name, st);
    }
    if (m_tray)
        m_tray->setToolTip(tip.join('\n'));
}
