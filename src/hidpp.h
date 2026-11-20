#pragma once
// Minimalna implementacja HID++ 2.0 po /dev/hidraw (bez roota - wystarczy ACL uaccess).
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <optional>

namespace feat {
constexpr quint16 Root = 0x0000;
constexpr quint16 FeatureSet = 0x0001;
constexpr quint16 DeviceName = 0x0005;
constexpr quint16 UnifiedBattery = 0x1004;
constexpr quint16 AdjustableDpi = 0x2201;
constexpr quint16 Brightness = 0x8040;
constexpr quint16 ReportRate = 0x8060;
constexpr quint16 RgbEffects = 0x8071;
constexpr quint16 OnboardProfiles = 0x8100;
}

class HidppDevice {
public:
    HidppDevice(QString path, quint8 index);
    ~HidppDevice();
    HidppDevice(const HidppDevice &) = delete;
    HidppDevice &operator=(const HidppDevice &) = delete;

    bool open();
    bool ping();
    // Wywołuje funkcję `fn` (0x00, 0x10, 0x20 ...) cechy `feature`. Zwraca 16 bajtów odpowiedzi.
    std::optional<QByteArray> call(quint16 feature, quint8 fn, const QByteArray &params = {});
    bool has(quint16 feature);
    QString name();

    QString path() const { return m_path; }
    quint8 index() const { return m_idx; }

private:
    int featureIndex(quint16 feature);
    std::optional<QByteArray> rawCall(quint8 featIdx, quint8 fn, const QByteArray &params);

    int m_fd = -1;
    QString m_path;
    quint8 m_idx;
    QHash<quint16, int> m_cache;
};

// ---- Czyste funkcje (testowalne bez sprzętu) ----
namespace hidpp {

quint16 crc16(const QByteArray &data);
QList<int> parseDpiList(const QByteArray &bytes);

// Akcja przycisku w profilu wbudowanym (4 bajty).
struct ButtonAction {
    enum Kind { Mouse, Key, Consumer, Function, Disabled, Raw } kind = Raw;
    quint16 value = 0;    // maska przycisku / kod HID / kod konsumencki / nr funkcji
    quint8 modifiers = 0; // dla Key: bit0 Ctrl, bit1 Shift, bit2 Alt, bit3 Super
    QByteArray raw;       // dla Raw (makra itp.) - zostawiamy bez zmian

    static ButtonAction fromBytes(const QByteArray &b);
    QByteArray toBytes() const;
    bool operator==(const ButtonAction &o) const
    {
        return kind == o.kind && value == o.value && modifiers == o.modifiers && (kind != Raw || raw == o.raw);
    }
};

// Funkcje przycisku (behavior 0x9)
enum Function : quint16 {
    FnTiltLeft = 0x1, FnTiltRight = 0x2, FnNextDpi = 0x3, FnPrevDpi = 0x4, FnCycleDpi = 0x5,
    FnDefaultDpi = 0x6, FnShiftDpi = 0x7, FnNextProfile = 0x8, FnPrevProfile = 0x9,
    FnCycleProfile = 0xA, FnGShift = 0xB, FnBattery = 0xC,
};

// Klawisz Qt <-> kod HID (usage page 0x07). 0 = brak odpowiednika.
int qtKeyToHid(int qtKey);
int hidToQtKey(int hid);

// Efekt świetlny w formacie HID++ (bajt ID + 10 bajtów parametrów).
struct Effect {
    enum Id : quint8 { Off = 0x00, Static = 0x01, Cycle = 0x03, Breathe = 0x0A, CycleSat = 0x15, Wave = 0x16 };
    quint8 id = Off;
    quint32 rgb = 0x00a0ff;
    int periodMs = 5000;
    int intensity = 100;
    QByteArray raw; // gdy efekt nieznany - zachowujemy bajty
    QByteArray params() const; // 10 bajtów
    QByteArray toBytes() const { return QByteArray(1, char(id)) + params(); }
    static Effect fromBytes(const QByteArray &b11); // ID + 10 bajtów
    static bool known(quint8 id);
};

// Profil wbudowany - pracujemy na surowym sektorze i zmieniamy tylko znane pola.
struct Profile {
    QByteArray raw; // cały sektor (razem z CRC na 2 ostatnich bajtach)
    int reportRateMs() const { return quint8(raw[0]); }
    void setReportRateMs(int ms) { raw[0] = char(ms); }
    int defaultDpiIndex() const { return quint8(raw[1]); }
    void setDefaultDpiIndex(int i) { raw[1] = char(i); }
    int shiftDpiIndex() const { return quint8(raw[2]); }
    void setShiftDpiIndex(int i) { raw[2] = char(i); }
    int dpi(int i) const;
    void setDpi(int i, int v);
    ButtonAction button(int i) const { return ButtonAction::fromBytes(raw.mid(32 + i * 4, 4)); }
    void setButton(int i, const ButtonAction &a);
    // Warstwa G-Shift (przyciski przytrzymane z G-Shift); FF FF FF FF = nieprzypisany
    ButtonAction gbutton(int i) const { return ButtonAction::fromBytes(raw.mid(96 + i * 4, 4)); }
    void setGButton(int i, const ButtonAction &a) { raw.replace(96 + i * 4, 4, a.toBytes()); }
    QString name() const;
    void setName(const QString &n);
    Effect lighting(int i) const { return Effect::fromBytes(raw.mid(208 + i * 11, 11)); }
    void setLighting(int i, const Effect &e);
    bool crcValid() const;
    void fixCrc();
};

} // namespace hidpp
