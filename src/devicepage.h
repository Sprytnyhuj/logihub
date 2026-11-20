#pragma once
#include "devices.h"
#include "hidpp.h"
#include "onboard.h"

#include <QWidget>
#include <memory>

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class MouseDiagram;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QVBoxLayout;

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
    void setColor(quint32 rgb);
    QComboBox *m_combo;
    QWidget *m_colorRow;
    QList<QPushButton *> m_swatches;
    QPushButton *m_custom;
    QWidget *m_speedRow;
    QSlider *m_speed;
    quint32 m_rgb = 0x00a0ff;
    hidpp::Effect m_original;
};

// Graficzny pasek etapów DPI (jak w G Hub).
class DpiTrack : public QWidget {
    Q_OBJECT
public:
    explicit DpiTrack(QWidget *parent = nullptr);
    void setStages(const QList<int> &dpi, int def, int shift, int min, int max);
    QSize sizeHint() const override { return {500, 86}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    double pos(int dpi) const;
    QList<int> m_dpi;
    int m_def = 0, m_shift = 0, m_min = 100, m_max = 25600;
};

// Edycja profili wbudowanych - dostarcza zawartość kilku zakładek.
class MouseProfile : public QObject {
    Q_OBJECT
public:
    MouseProfile(HidppDevice *dev, const DeviceInfo &info, const onboard::Info &oi, QObject *parent = nullptr);
    QWidget *sensitivityCard() const { return m_sensCard; }
    QWidget *buttonsCard() const { return m_btnCard; }
    QWidget *lightingCard() const { return m_lightCard; }
    QWidget *profileCard() const { return m_profCard; }
    QWidget *saveBar() const { return m_saveBar; }
    void reload();
private:
    struct ButtonRow {
        QComboBox *action = nullptr;
        QKeySequenceEdit *key = nullptr;
        hidpp::ButtonAction original;
        bool gshift = false;
        int index = 0;
    };
    void loadSector(int sector);
    void save();
    void restoreBackup();
    void markDirty();
    void updateTrack();
    QString backupDir() const;
    QWidget *buttonLayer(bool gshift, const QStringList &names);
    void selectButton(int index);
    void updateDiagram();
    MouseDiagram *m_diagram = nullptr;
    QStackedWidget *m_btnEditors = nullptr;
    QLabel *m_btnTitle = nullptr;
    QStringList m_btnNames;
    int m_layer = 0;

    HidppDevice *m_dev;
    DeviceInfo m_info;
    onboard::Info m_oi;
    hidpp::Profile m_profile;
    int m_sector = -1;
    bool m_loading = false;
    int m_dpiMin = 100, m_dpiMax = 25600;
    QWidget *m_sensCard, *m_btnCard, *m_lightCard, *m_profCard, *m_saveBar;
    QComboBox *m_profileCombo;
    QLineEdit *m_name;
    DpiTrack *m_track;
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

class DevicePage : public QWidget {
    Q_OBJECT
public:
    explicit DevicePage(const DeviceInfo &info, QWidget *parent = nullptr);
    ~DevicePage() override;
    const DeviceInfo &info() const { return m_info; }
    QString statusText() const { return m_statusText; }
    QString iconName() const;
    // Okno widoczne: odświeżanie co 20 s; schowane w zasobniku: co 5 min (mniej ruchu radiowego).
    void setActive(bool active);
signals:
    void statusChanged();
private:
    void build();
    void buildMouse();
    void buildMic();
    void buildHeadset();
    void buildGenericAudio();
    void refresh();
    void applySaved();
    QWidget *tab(const QString &title, const QList<QWidget *> &cards);
    QWidget *buildMode();
    QWidget *buildHost();
    QWidget *buildLighting();
    QWidget *buildBrightness();
    void updateModeVisibility();
    QString slug() const;

    DeviceInfo m_info;
    std::unique_ptr<HidppDevice> m_dev;
    QVBoxLayout *m_root = nullptr;
    QLabel *m_tile = nullptr;
    QLabel *m_conn = nullptr;
    QLabel *m_battery = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_waiting = nullptr;
    QComboBox *m_mode = nullptr;
    QWidget *m_hostBox = nullptr;
    QWidget *m_lightBox = nullptr;
    MouseProfile *m_profile = nullptr;
    QTimer *m_timer = nullptr;
    bool m_online = false;
    bool m_built = false;
    bool m_claimed = false;
    QString m_statusText;
};
