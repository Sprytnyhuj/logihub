#pragma once
// Podświetlenie urządzeń innych firm przez serwer OpenRGB + synchronizacja kolorów (jak LIGHTSYNC).
#include "openrgb.h"

#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QVBoxLayout;

// Rząd próbek kolorów + „Własny…”
class SwatchRow : public QWidget {
    Q_OBJECT
public:
    explicit SwatchRow(quint32 rgb = 0x00b4ff, QWidget *parent = nullptr);
    quint32 color() const { return m_rgb; }
    void setColor(quint32 rgb);
signals:
    void picked(quint32 rgb);
private:
    QList<QPushButton *> m_swatches;
    QPushButton *m_custom;
    quint32 m_rgb;
};

class RgbPanel : public QWidget {
    Q_OBJECT
public:
    explicit RgbPanel(QWidget *parent = nullptr);
    static bool serverAvailable();
private:
    void reload();
    bool ensureConnected();
    bool apply(int controllerPos, int mode, quint32 rgb);
    int syncMode(const orgb::Controller &c) const;
    std::unique_ptr<orgb::Client> m_client;
    QList<orgb::Controller> m_ctrls;
    QVBoxLayout *m_list;
    QLabel *m_status;
};
