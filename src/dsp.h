#pragma once
// Programowe efekty audio (jak w G Hub na Windowsie): Voice FX (w stylu Blue VO!CE), equalizer, sidetone.
// Każdy łańcuch to osobny proces `pipewire -c <conf>` z modułem filter-chain.
#include <QHash>
#include <QObject>
#include <QStringList>
#include <array>

class QProcess;
class QSettings;

struct VoiceSettings {
    bool enabled = false;
    int preset = 1;
    bool highpass = true;    int highpassHz = 80;
    bool noiseReduction = true;
    int nrMode = 0;          // 0 = RNNoise (AI), 1 = WebRTC (+ usuwanie echa z głośników)
    bool gate = true;        int gateOpenDb = -45;
    bool deesser = true;     int deessDb = 4;
    bool compressor = true;  int compThreshold = 40; int compStrength = 50;
    bool limiter = true;
    int bassDb = 0, presenceDb = 0, airDb = 0;
    int outGainDb = 0;

    void load(QSettings &s, const QString &prefix);
    void save(QSettings &s, const QString &prefix) const;
    // Klucz struktury łańcucha - gdy się zmienia, trzeba restartu (a nie tylko zmiany parametrów).
    QString structureKey() const;
    QList<QPair<QString, double>> controls() const;
    static QStringList presetNames();
    static VoiceSettings fromPreset(int preset);
};

struct EqSettings {
    bool enabled = false;
    int preset = 0;
    std::array<int, 10> gains{};
    void load(QSettings &s, const QString &prefix);
    void save(QSettings &s, const QString &prefix) const;
    QList<QPair<QString, double>> controls() const;
    static const std::array<int, 10> &frequencies();
    static QStringList presetNames();
    static std::array<int, 10> presetGains(int preset);
};

struct SidetoneSettings {
    bool enabled = false;
    int volume = 30; // %
    void load(QSettings &s, const QString &prefix);
    void save(QSettings &s, const QString &prefix) const;
};

namespace dsp {
// Wtyczki LogiHub (LADSPA) - gdy brak, odpowiednie moduły Voice FX są pomijane.
QString dspPluginPath(); // bramka, kompresor, de-esser, limiter
QString rnnoisePath();   // redukcja szumów RNNoise (AI)
bool webrtcAvailable();  // moduł redukcji szumów/echa PipeWire (aec-webrtc)
QString voiceConf(const QString &id, const QString &description, const QString &micNode, const VoiceSettings &v);
QString eqConf(const QString &id, const QString &description, const QString &sinkNode, const EqSettings &e);
QString sidetoneConf(const QString &id, const QString &micNode, const QString &sinkNode, int volume);
}

// Uruchamia/zatrzymuje łańcuchy i zmienia ich parametry na żywo.
class DspRunner : public QObject {
    Q_OBJECT
public:
    static DspRunner &instance();
    // Uruchamia (lub restartuje, gdy konfiguracja się zmieniła) łańcuch o danym id.
    void start(const QString &id, const QString &conf);
    void stop(const QString &id);
    void stopAll();
    // Zamyka osierocone łańcuchy z poprzednich uruchomień; zwraca ich liczbę.
    static int killStale();
    bool running(const QString &id) const;
    // Zmiana parametrów bez restartu - node = node.name węzła capture filtra.
    void setControls(const QString &node, const QList<QPair<QString, double>> &controls);
    // Ustawia węzeł jako domyślny (po chwili, gdy już istnieje).
    void makeDefault(const QString &node);
private:
    DspRunner() = default;
    QHash<QString, QProcess *> m_procs;
    QHash<QString, QString> m_confs;
};
