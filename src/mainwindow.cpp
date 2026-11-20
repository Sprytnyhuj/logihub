#include "mainwindow.h"
#include "devicepage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>

namespace {
QString autostartFile()
{
    return QDir::homePath() + "/.config/autostart/logihub.desktop";
}

QIcon iconFor(const QString &name)
{
    if (name.startsWith("Yeti"))
        return QIcon::fromTheme("audio-input-microphone");
    if (name.contains("Headset") || name.startsWith("Astro"))
        return QIcon::fromTheme("audio-headset");
    return QIcon::fromTheme("input-mouse");
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("LogiHub");
    setWindowIcon(QIcon::fromTheme("input-gaming"));
    resize(980, 760);

    auto *split = new QSplitter;
    m_list = new QListWidget;
    m_list->setIconSize(QSize(32, 32));
    m_list->setMinimumWidth(220);
    m_list->setMaximumWidth(320);
    m_stack = new QStackedWidget;
    auto *empty = new QLabel(tr("Nie znaleziono urządzeń Logitech.\nPodłącz urządzenie albo wybudź mysz."));
    empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(empty);
    split->addWidget(m_list);
    split->addWidget(m_stack);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);

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
                            "Icon=input-gaming\nX-GNOME-Autostart-enabled=true\n")
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

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::rescan);
    timer->start(4000);
    rescan();
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
        // pełna wysokość strony, nie tylko widoczny fragment
        QWidget *body = m_pages[i]->widget();
        body->adjustSize();
        body->grab().save(QString("%1/page%2.png").arg(dir).arg(i));
        grab().save(QString("%1/window%2.png").arg(dir).arg(i));
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_tray && !m_quitting) {
        hide(); // jak G Hub - działa dalej w zasobniku
        e->ignore();
        return;
    }
    e->accept();
}

void MainWindow::rescan()
{
    const QList<DeviceInfo> found = scanDevices();
    bool changed = false;
    // usuń zniknięte
    for (int i = m_pages.size() - 1; i >= 0; --i) {
        if (!found.contains(m_pages[i]->info())) {
            DevicePage *p = m_pages.takeAt(i);
            m_stack->removeWidget(p);
            p->deleteLater();
            delete m_list->takeItem(i);
            changed = true;
        }
    }
    // dodaj nowe
    for (const DeviceInfo &d : found) {
        bool have = false;
        for (DevicePage *p : m_pages)
            have |= p->info() == d;
        if (have)
            continue;
        auto *page = new DevicePage(d);
        connect(page, &DevicePage::statusChanged, this, &MainWindow::updateList);
        m_pages.append(page);
        m_stack->addWidget(page);
        m_list->addItem(new QListWidgetItem(iconFor(d.name), d.name));
        changed = true;
    }
    if (changed) {
        // kolejność w stosie: 0 = pusta strona, potem strony w kolejności listy
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
    for (int i = 0; i < m_pages.size(); ++i) {
        const QString st = m_pages[i]->statusText();
        const QString name = m_pages[i]->info().name;
        m_list->item(i)->setText(st.isEmpty() ? name : QString("%1\n%2").arg(name, st));
        if (!st.isEmpty())
            tip << QString("%1: %2").arg(name, st);
    }
    if (m_tray)
        m_tray->setToolTip(tip.join('\n'));
}
