#pragma once
// Głośność / wyciszenie przez PipeWire (pw-dump + wpctl) i sprzętowe wzmocnienie przez ALSA (amixer).
#include <QList>
#include <QString>

struct AudioNode {
    int id = -1;
    QString name;        // node.name
    QString description;
    bool isSink = true;  // false = źródło (mikrofon)
};

// Fizyczne urządzenie audio innej firmy (USB / Bluetooth) - wyjście i/lub mikrofon.
struct AudioDevice {
    QString description;
    QString sinkNode;   // node.name wyjścia (pusty gdy brak)
    QString sourceNode; // node.name mikrofonu (pusty gdy brak)
    bool bluetooth = false;
};

namespace audio {
// Urządzenia USB/Bluetooth spoza Logitecha (Logitech ma własne strony).
QList<AudioDevice> externalDevices();
QList<AudioNode> nodesByName(const QStringList &names);
// Fizyczne urządzenia pasujące do słowa kluczowego (bez węzłów utworzonych przez LogiHub).
QList<AudioNode> findNodes(const QString &keyword);
int nodeIdByName(const QString &nodeName);
// Zwraca głośność 0..150 lub -1; muted ustawiane gdy podano wskaźnik.
int volume(int id, bool *muted = nullptr);
void setVolume(int id, int percent);
void setMuted(int id, bool muted);

// Sprzętowe wzmocnienie mikrofonu (kontrolka ALSA "Mic" karty pasującej do słowa kluczowego).
struct HwGain {
    QString card; // id karty ALSA, np. "GX"
    int value = -1;
    int max = 0;
};
HwGain micHwGain(const QString &keyword);
void setMicHwGain(const QString &card, int value);
}
