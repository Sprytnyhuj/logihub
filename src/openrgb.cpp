#include "openrgb.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTcpSocket>
#include <QtEndian>

namespace orgb {
namespace {
constexpr quint32 kRequestCount = 0;
constexpr quint32 kRequestData = 1;
constexpr quint32 kRequestProtocol = 40;
constexpr quint32 kSetClientName = 50;
constexpr quint32 kSaveProfile = 151;
constexpr quint32 kUpdateLeds = 1050;
constexpr quint32 kUpdateMode = 1101;
constexpr quint32 kClientProtocol = 2; // format danych kontrolera jak w v1 (+profile)

// Czytnik little-endian z kontrolą zakresu
struct Reader {
    const QByteArray &b;
    int pos = 0;
    bool ok = true;
    template <typename T> T get()
    {
        if (pos + int(sizeof(T)) > b.size()) {
            ok = false;
            return T{};
        }
        T v = qFromLittleEndian<T>(b.constData() + pos);
        pos += sizeof(T);
        return v;
    }
    QString str()
    {
        const quint16 len = get<quint16>();
        if (!ok || pos + len > b.size()) {
            ok = false;
            return {};
        }
        QString s = QString::fromUtf8(b.constData() + pos, len > 0 ? len - 1 : 0); // z kończącym \0
        pos += len;
        return s;
    }
    void skip(int n)
    {
        if (pos + n > b.size())
            ok = false;
        else
            pos += n;
    }
};

template <typename T> void put(QByteArray &b, T v)
{
    char buf[sizeof(T)];
    qToLittleEndian<T>(v, buf);
    b.append(buf, sizeof(T));
}

void putStr(QByteArray &b, const QString &s)
{
    const QByteArray u = s.toUtf8();
    put<quint16>(b, quint16(u.size() + 1));
    b.append(u);
    b.append('\0');
}
}

std::optional<Controller> parseController(const QByteArray &blob, int protocol)
{
    Reader r{blob};
    Controller c;
    r.get<quint32>(); // rozmiar
    c.type = r.get<qint32>();
    c.name = r.str();
    if (protocol >= 1)
        c.vendor = r.str();
    c.description = r.str();
    r.str(); // wersja
    r.str(); // numer seryjny
    c.location = r.str();
    const quint16 numModes = r.get<quint16>();
    c.activeMode = r.get<qint32>();
    for (int i = 0; i < numModes && r.ok; ++i) {
        Mode m;
        m.name = r.str();
        m.value = r.get<qint32>();
        m.flags = r.get<quint32>();
        m.speedMin = r.get<quint32>();
        m.speedMax = r.get<quint32>();
        if (protocol >= 3) { // jasność
            r.get<quint32>();
            r.get<quint32>();
        }
        m.colorsMin = r.get<quint32>();
        m.colorsMax = r.get<quint32>();
        m.speed = r.get<quint32>();
        if (protocol >= 3)
            r.get<quint32>();
        m.direction = r.get<quint32>();
        m.colorMode = r.get<quint32>();
        const quint16 nc = r.get<quint16>();
        for (int k = 0; k < nc && r.ok; ++k)
            m.colors << r.get<quint32>();
        c.modes << m;
    }
    const quint16 numZones = r.get<quint16>();
    for (int z = 0; z < numZones && r.ok; ++z) {
        r.str();               // nazwa
        r.get<qint32>();       // typ
        r.get<quint32>();      // leds_min
        r.get<quint32>();      // leds_max
        r.get<quint32>();      // leds_count
        const quint16 matrixLen = r.get<quint16>();
        if (matrixLen > 0)
            r.skip(matrixLen);
        if (protocol >= 4) { // segmenty
            const quint16 ns = r.get<quint16>();
            for (int s = 0; s < ns && r.ok; ++s) {
                r.str();
                r.get<qint32>();
                r.get<quint32>();
                r.get<quint32>();
            }
        }
    }
    const quint16 numLeds = r.get<quint16>();
    for (int l = 0; l < numLeds && r.ok; ++l) {
        r.str();
        r.get<quint32>();
    }
    c.ledCount = numLeds;
    const quint16 numColors = r.get<quint16>();
    for (int k = 0; k < numColors && r.ok; ++k)
        c.colors << r.get<quint32>();
    if (!r.ok)
        return std::nullopt;
    return c;
}

QByteArray encodeMode(const Mode &m)
{
    QByteArray b;
    putStr(b, m.name);
    put<qint32>(b, m.value);
    put<quint32>(b, m.flags);
    put<quint32>(b, m.speedMin);
    put<quint32>(b, m.speedMax);
    put<quint32>(b, m.colorsMin);
    put<quint32>(b, m.colorsMax);
    put<quint32>(b, m.speed);
    put<quint32>(b, m.direction);
    put<quint32>(b, m.colorMode);
    put<quint16>(b, quint16(m.colors.size()));
    for (quint32 c : m.colors)
        put<quint32>(b, c);
    return b;
}

Client::Client() = default;
Client::~Client() = default;

bool Client::connectToServer(const QString &host, quint16 port, int timeoutMs)
{
    m_sock = std::make_unique<QTcpSocket>();
    m_sock->connectToHost(host, port);
    if (!m_sock->waitForConnected(timeoutMs)) {
        m_sock.reset();
        return false;
    }
    QByteArray v;
    put<quint32>(v, kClientProtocol);
    send(0, kRequestProtocol, v);
    if (auto r = receive(kRequestProtocol, 600); r && r->size() >= 4)
        m_protocol = int(qMin<quint32>(kClientProtocol, qFromLittleEndian<quint32>(r->constData())));
    else
        m_protocol = 0; // bardzo stary serwer - nie odpowiada na pytanie o wersję
    send(0, kSetClientName, QByteArray("LogiHub\0", 8));
    return true;
}

bool Client::connected() const
{
    return m_sock && m_sock->state() == QAbstractSocket::ConnectedState;
}

bool Client::send(quint32 dev, quint32 id, const QByteArray &data)
{
    if (!connected())
        return false;
    QByteArray pkt("ORGB");
    put<quint32>(pkt, dev);
    put<quint32>(pkt, id);
    put<quint32>(pkt, quint32(data.size()));
    pkt += data;
    m_sock->write(pkt);
    return m_sock->waitForBytesWritten(1000);
}

std::optional<QByteArray> Client::receive(quint32 expectId, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    QByteArray buf;
    while (t.elapsed() < timeoutMs) {
        while (buf.size() < 16) {
            if (m_sock->bytesAvailable() == 0 && !m_sock->waitForReadyRead(int(qMax<qint64>(1, timeoutMs - t.elapsed()))))
                return std::nullopt;
            buf += m_sock->read(16 - buf.size());
        }
        if (!buf.startsWith("ORGB"))
            return std::nullopt;
        const quint32 id = qFromLittleEndian<quint32>(buf.constData() + 8);
        const quint32 size = qFromLittleEndian<quint32>(buf.constData() + 12);
        QByteArray data;
        while (quint32(data.size()) < size) {
            if (m_sock->bytesAvailable() == 0 && !m_sock->waitForReadyRead(int(qMax<qint64>(1, timeoutMs - t.elapsed()))))
                return std::nullopt;
            data += m_sock->read(size - data.size());
        }
        buf.clear();
        if (id == expectId)
            return data;
        // inne pakiety (np. powiadomienie o zmianie listy urządzeń) - pomijamy
    }
    return std::nullopt;
}

QList<Controller> Client::controllers()
{
    QList<Controller> out;
    if (!send(0, kRequestCount))
        return out;
    auto r = receive(kRequestCount);
    if (!r || r->size() < 4)
        return out;
    const quint32 count = qFromLittleEndian<quint32>(r->constData());
    for (quint32 i = 0; i < count && i < 64; ++i) {
        QByteArray v;
        if (m_protocol > 0)
            put<quint32>(v, quint32(m_protocol));
        send(i, kRequestData, v);
        auto d = receive(kRequestData, 2500);
        if (!d)
            continue;
        if (auto c = parseController(*d, m_protocol)) {
            c->index = int(i);
            out << *c;
        }
    }
    return out;
}

bool Client::applyColor(const Controller &c, int modeIndex, quint32 rgb)
{
    if (modeIndex < 0 || modeIndex >= c.modes.size())
        return false;
    Mode m = c.modes[modeIndex];
    const quint32 col = fromRgb(rgb);
    if (m.colorMode == ColorsModeSpecific) {
        const int n = qMax<int>(1, qMax<int>(int(m.colorsMin), m.colors.size()));
        m.colors = QList<quint32>(n, col);
    }
    QByteArray body;
    put<qint32>(body, modeIndex);
    body += encodeMode(m);
    QByteArray pkt;
    put<quint32>(pkt, quint32(body.size() + 4));
    pkt += body;
    if (!send(quint32(c.index), kUpdateMode, pkt))
        return false;
    if (m.colorMode == ColorsPerLed && c.ledCount > 0) {
        QByteArray leds;
        put<quint16>(leds, quint16(c.ledCount));
        for (int i = 0; i < c.ledCount; ++i)
            put<quint32>(leds, col);
        QByteArray p2;
        put<quint32>(p2, quint32(leds.size() + 4));
        p2 += leds;
        return send(quint32(c.index), kUpdateLeds, p2);
    }
    return true;
}

bool Client::loadProfile(const QString &name)
{
    if (m_protocol < 2 || name.isEmpty())
        return false;
    QByteArray n = name.toUtf8();
    n.append('\0');
    return send(0, 152 /* REQUEST_LOAD_PROFILE */, n);
}

bool Client::saveProfile(const QString &name)
{
    if (m_protocol < 2 || name.isEmpty())
        return false;
    QByteArray n = name.toUtf8();
    n.append('\0');
    return send(0, kSaveProfile, n);
}

QString serverProfileName()
{
    const QStringList pids = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool num = false;
        pid.toInt(&num);
        if (!num)
            continue;
        QFile f("/proc/" + pid + "/cmdline");
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> args = f.readAll().split('\0');
        bool isOpenRgb = false, server = false;
        QString profile;
        for (int i = 0; i < args.size(); ++i) {
            const QByteArray a = args[i];
            if (a.endsWith("openrgb"))
                isOpenRgb = true;
            if (a == "--server")
                server = true;
            if ((a == "-p" || a == "--profile") && i + 1 < args.size())
                profile = QString::fromUtf8(args[i + 1]);
        }
        if (isOpenRgb && server && !profile.isEmpty())
            return profile;
    }
    return {};
}

} // namespace orgb
