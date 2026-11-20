#pragma once
#include "devices.h"
#include "hidpp.h"
#include "onboard.h"

#include <QGroupBox>
#include <QScrollArea>
#include <memory>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QCheckBox;
class QTimer;

// Wybór efektu świetlnego: efekt + kolor + szybkość.
class EffectEditor : public QWidget {
    Q_OBJECT
public:
    explicit EffectEditor(QWidget *parent = nullptr);
    void setAvailable(const QList<quint8> &ids, bool keepUnknown);
    void setEffect(const hidpp::Effect &e);
    hidpp::Effect effect() const;
signals:
    void changed();
private:
    void updateVisibility();
    QComboBox *m_combo;
    QPushButton *m_color;
    QSlider *m_speed;
    quint32 m_rgb = 0x00a0ff;
    hidpp::Effect m_original;
};

// Edytor profilu w pamięci myszy (jak "Pamięć wbudowana" w G Hub).
class ProfileEditor : public QGroupBox {
    Q_OBJECT
public:
    ProfileEditor(HidppDevice *dev, const DeviceInfo &info, const onboard::Info &oi, QWidget *parent = nullptr);
    void reload();
private:
    void loadSector(int sector);
    void save();
    void restoreBackup();
    QString backupDir() const;
    struct ButtonRow {
        QComboBox *action;
        QKeySequenceEdit *key;
        hidpp::ButtonAction original;
    };
    HidppDevice *m_dev;
    DeviceInfo m_info;
    onboard::Info m_oi;
    QList<onboard::Header> m_headers;
    hidpp::Profile m_profile;
    int m_sector = -1;
    QComboBox *m_profileCombo;
    QList<QSpinBox *> m_dpi;
    QList<QRadioButton *> m_dpiDefault;
    QComboBox *m_shift;
    QComboBox *m_rate;
    QList<ButtonRow> m_buttons;
    EffectEditor *m_light;
    bool m_lightDirty = false;
    QLabel *m_status;
    QPushButton *m_save;
};

class DevicePage : public QScrollArea {
    Q_OBJECT
public:
    explicit DevicePage(const DeviceInfo &info, QWidget *parent = nullptr);
    ~DevicePage() override;
    const DeviceInfo &info() const { return m_info; }
    QString statusText() const { return m_statusText; }
signals:
    void statusChanged();
private:
    void build();
    void refresh();
    void applySaved();
    QGroupBox *buildMode();
    QGroupBox *buildHost();
    QGroupBox *buildLighting();
    QGroupBox *buildBrightness();
    QGroupBox *buildAudio();
    void updateModeVisibility();

    DeviceInfo m_info;
    std::unique_ptr<HidppDevice> m_dev;
    QWidget *m_body = nullptr;
    QLabel *m_header = nullptr;
    QProgressBar *m_battery = nullptr;
    QLabel *m_batteryLabel = nullptr;
    QComboBox *m_mode = nullptr;
    QGroupBox *m_hostBox = nullptr;
    QGroupBox *m_lightBox = nullptr;
    ProfileEditor *m_profile = nullptr;
    QTimer *m_timer = nullptr;
    bool m_online = false;
    bool m_built = false;
    bool m_claimed = false;
    QString m_statusText;
};
