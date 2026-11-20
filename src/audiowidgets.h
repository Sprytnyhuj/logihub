#pragma once
#include "dsp.h"

#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QSlider;
class QTimer;
class Toggle;

// Blue VO!CE - łańcuch efektów głosu dla mikrofonu.
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
    QComboBox *m_preset;
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
    Toggle *m_enabled;
    QComboBox *m_preset;
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
// Sprzętowe wzmocnienie mikrofonu (ALSA); nullptr gdy brak.
QWidget *hwGainCard(const QString &keyword);
// Wiersz suwaka z etykietą wartości.
QWidget *sliderRow(QSlider *s, const QString &suffix, QLabel **valueLabel = nullptr);
