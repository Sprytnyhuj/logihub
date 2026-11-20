#pragma once
// Klient OpenRGB SDK (TCP, domyślnie 127.0.0.1:6742) - RGB urządzeń innych firm
// (płyty główne, klawiatury, RAM, karty graficzne...) przez działający serwer OpenRGB.
#include <QByteArray>
#include <QList>
#include <QString>
#include <memory>
#include <optional>

class QTcpSocket;

namespace orgb {

enum ColorMode { ColorsNone = 0, ColorsPerLed = 1, ColorsModeSpecific = 2, ColorsRandom = 3 };

struct Mode {
    QString name;
    int value = 0;
    quint32 flags = 0;
    quint32 speedMin = 0, speedMax = 0;
    quint32 colorsMin = 0, colorsMax = 0;
    quint32 speed = 0;
    quint32 direction = 0;
    quint32 colorMode = 0;
    QList<quint32> colors; // 0x00BBGGRR (format OpenRGB)
};

struct Controller {
    int index = -1;
    int type = 0;
    QString name, vendor, description, location;
    QList<Mode> modes;
    int activeMode = 0;
    int ledCount = 0;
    QList<quint32> colors;
};

// OpenRGB przechowuje kolor jako R | G<<8 | B<<16
inline quint32 fromRgb(quint32 rgb) { return ((rgb >> 16) & 0xff) | (rgb & 0xff00) | ((rgb & 0xff) << 16); }
inline quint32 toRgb(quint32 c) { return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff); }

// Parsowanie bloku danych kontrolera (protokół 1-2). Publiczne dla testów.
std::optional<Controller> parseController(const QByteArray &blob, int protocol);
QByteArray encodeMode(const Mode &m);

class Client {
public:
    Client();
    ~Client();
    bool connectToServer(const QString &host = "127.0.0.1", quint16 port = 6742, int timeoutMs = 800);
    bool connected() const;
    int protocol() const { return m_protocol; }
    QList<Controller> controllers();
    // Ustawia tryb (i jego kolory); dla trybów „per LED” dodatkowo maluje wszystkie diody kolorem.
    bool applyColor(const Controller &c, int modeIndex, quint32 rgb);
    bool saveProfile(const QString &name);
    bool loadProfile(const QString &name);
private:
    bool send(quint32 dev, quint32 id, const QByteArray &data = {});
    std::optional<QByteArray> receive(quint32 expectId, int timeoutMs = 1500);
    std::unique_ptr<QTcpSocket> m_sock;
    int m_protocol = 0;
};

// Nazwa profilu, z którym uruchomiono serwer OpenRGB (argument -p / --profile), jeśli jest.
QString serverProfileName();

} // namespace orgb
