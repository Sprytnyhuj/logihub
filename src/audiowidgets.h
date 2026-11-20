#pragma once
#include "audio.h"
#include "dsp.h"

#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QSlider;
class QTimer;
class QPushButton;
class Toggle;

// Lista presetów: wbudowane + własne (zapisane w QSettings pod `group`) + „Własny”.
// Identyfikatory: "b:<nr>" wbudowany, "u:<nazwa>" użytkownika, "c" niezapisane ustawienia.
class PresetBar : public QWidget {
    Q_OBJECT
public:
    PresetBar(const QString &group, const QList<QPair<QString, QString>> &builtins, QWidget *parent = nullptr);
    void select(const QString &id);
    QString current() const;
signals:
    void picked(const QString &id);           // wybrano preset wbudowany lub własny
    void saveRequested(const QString &name);  // właściciel zapisuje dane pod group/name
private:
    void refill();
    QString m_group;
    QList<QPair<QString, QString>> m_builtins;
    QComboBox *m_combo;
    QPushButton *m_del;
};

// Voice FX (w stylu Blue VO!CE) - łańcuch efektów głosu dla mikrofonu.
class VoiceEditor : public QWidget {
    Q_OBJECT
public:
    VoiceEditor(const QString &devKey, const QString &id, const QString &description, const QString &micNode,
                QWidget *parent = nullptr);
private:
    void syncUi();
    void changed(bool presetPick = false);
    void apply(bool structural);
    QString m_key, m_id, m_desc, m_mic;
    VoiceSettings m_v;
    QString m_lastStructure;
    Toggle *m_enabled;
    PresetBar *m_presets;
    QWidget *m_modules;
    QList<std::function<void()>> m_sync; // odświeżenie kontrolek z m_v
    QTimer *m_debounce;
};

class EqEditor : public QWidget {
    Q_OBJECT
public:
    EqEditor(const QString &devKey, const QString &id, const QString &description, const QString &sinkNode,
             QWidget *parent = nullptr);
private:
    void apply();
    QString m_key, m_id, m_desc, m_sink;
    EqSettings m_e;
    void setGains(const std::array<int, 10> &g);
    Toggle *m_enabled;
    PresetBar *m_presets;
    QList<QSlider *> m_sliders;
    QList<QLabel *> m_values;
    QTimer *m_debounce;
};

class SidetoneCard : public QWidget {
    Q_OBJECT
public:
    // sinkNode pusty = domyślne wyjście (odsłuch mikrofonu)
    SidetoneCard(const QString &devKey, const QString &id, const QString &micNode, const QString &sinkNode,
                 const QString &title, const QString &subtitle, QWidget *parent = nullptr);
private:
    void apply();
    QString m_key, m_id, m_mic, m_sink;
    SidetoneSettings m_s;
};

// Głośność / wyciszenie wszystkich węzłów audio urządzenia.
QWidget *volumeCard(const QString &keyword, bool sinks, bool sources, const QString &title);
QWidget *volumeCardFor(const QList<AudioNode> &nodes, const QString &title);
// Sprzętowe wzmocnienie mikrofonu (ALSA); nullptr gdy brak.
QWidget *hwGainCard(const QString &keyword);
// Wiersz suwaka z etykietą wartości.
QWidget *sliderRow(QSlider *s, const QString &suffix, QLabel **valueLabel = nullptr);
