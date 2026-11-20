#include "hidpp.h"

#include <QElapsedTimer>
#include <QtGlobal>
#include <Qt>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {
constexpr quint8 kSwId = 0x0a;    // identyfikator naszej aplikacji w odpowiedziach
constexpr int kTimeoutMs = 600;
}

HidppDevice::HidppDevice(QString path, quint8 index) : m_path(std::move(path)), m_idx(index) {}

HidppDevice::~HidppDevice()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool HidppDevice::open()
{
    if (m_fd >= 0)
        return true;
    m_fd = ::open(m_path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    return m_fd >= 0;
}

std::optional<QByteArray> HidppDevice::rawCall(quint8 featIdx, quint8 fn, const QByteArray &params)
{
    if (!open())
        return std::nullopt;
    unsigned char buf[64];
    while (::read(m_fd, buf, sizeof buf) > 0) {} // wyrzuć stare raporty

    unsigned char req[20] = {0x11, m_idx, featIdx, quint8((fn & 0xf0) | kSwId)};
    for (int i = 0; i < params.size() && i < 16; ++i)
        req[4 + i] = quint8(params[i]);
    if (::write(m_fd, req, sizeof req) != sizeof req)
        return std::nullopt;

    QElapsedTimer t;
    t.start();
    const quint8 fnByte = req[3];
    while (t.elapsed() < kTimeoutMs) {
        pollfd pfd{m_fd, POLLIN, 0};
        if (::poll(&pfd, 1, int(kTimeoutMs - t.elapsed())) <= 0)
            break;
        const ssize_t n = ::read(m_fd, buf, sizeof buf);
        if (n < 7 || buf[1] != m_idx)
            continue;
        if (buf[0] == 0x10 && buf[2] == 0x8f && buf[3] == featIdx)
            return std::nullopt; // HID++ 1.0: urządzenie niepodłączone/uśpione
        if (buf[0] == 0x11 && buf[2] == 0xff && buf[3] == featIdx && buf[4] == fnByte)
            return std::nullopt; // błąd HID++ 2.0
        // odpowiedź może przyjść krótkim (0x10, 7 B - np. Yeti GX) albo długim raportem (0x11, 20 B)
        if ((buf[0] == 0x10 || buf[0] == 0x11) && buf[2] == featIdx && buf[3] == fnByte) {
            QByteArray out(reinterpret_cast<const char *>(buf + 4), int(qMin<ssize_t>(n - 4, 16)));
            out.append(QByteArray(16 - out.size(), char(0)));
            return out;
        }
    }
    return std::nullopt;
}

int HidppDevice::featureIndex(quint16 feature)
{
    if (feature == feat::Root)
        return 0;
    auto it = m_cache.constFind(feature);
    if (it != m_cache.constEnd())
        return *it;
    QByteArray p;
    p.append(char(feature >> 8)).append(char(feature & 0xff));
    auto r = rawCall(0, 0x00, p);
    if (!r)
        return -1; // nie cache'ujemy - urządzenie mogło spać
    const int idx = quint8(r->at(0)) ? quint8(r->at(0)) : -1;
    m_cache.insert(feature, idx);
    return idx;
}

std::optional<QByteArray> HidppDevice::call(quint16 feature, quint8 fn, const QByteArray &params)
{
    const int idx = featureIndex(feature);
    if (idx < 0)
        return std::nullopt;
    return rawCall(quint8(idx), fn, params);
}

bool HidppDevice::ping()
{
    return rawCall(0, 0x10, QByteArray("\x00\x00\xaa", 3)).has_value();
}

bool HidppDevice::has(quint16 feature)
{
    return featureIndex(feature) > 0;
}

QString HidppDevice::name()
{
    auto len = call(feat::DeviceName, 0x00);
    if (!len)
        return {};
    const int n = quint8(len->at(0));
    QByteArray s;
    for (int off = 0; off < n; off += 16) {
        auto chunk = call(feat::DeviceName, 0x10, QByteArray(1, char(off)));
        if (!chunk)
            break;
        s += *chunk;
    }
    return QString::fromUtf8(s.left(n));
}

// ===================================================================
namespace hidpp {

quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0xffff;
    for (char ch : data) {
        crc ^= quint16(quint8(ch)) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

QList<int> parseDpiList(const QByteArray &b)
{
    QList<int> out;
    auto at = [&](int i) { return (quint8(b[i]) << 8) | quint8(b[i + 1]); };
    for (int i = 0; i + 1 < b.size();) {
        const int v = at(i);
        if (v == 0)
            break;
        if ((v >> 13) == 0b111) { // zakres: krok, potem wartość końcowa
            if (out.isEmpty() || i + 3 >= b.size())
                break;
            const int step = v & 0x1fff;
            const int last = at(i + 2);
            if (step <= 0)
                break;
            for (int d = out.last() + step; d <= last; d += step)
                out.append(d);
            i += 4;
        } else {
            out.append(v);
            i += 2;
        }
    }
    return out;
}

ButtonAction ButtonAction::fromBytes(const QByteArray &b)
{
    ButtonAction a;
    a.raw = b;
    if (b.size() < 4)
        return a;
    const quint8 b0 = quint8(b[0]), b1 = quint8(b[1]), b2 = quint8(b[2]), b3 = quint8(b[3]);
    if (b0 == 0x80) { // SEND
        if (b1 == 0x01) {
            a.kind = Mouse;
            a.value = quint16((b2 << 8) | b3);
        } else if (b1 == 0x02) {
            a.kind = Key;
            a.modifiers = b2;
            a.value = b3;
        } else if (b1 == 0x03) {
            a.kind = Consumer;
            a.value = quint16((b2 << 8) | b3);
        } else if (b1 == 0x00) {
            a.kind = Disabled;
        }
    } else if (b0 == 0x90) { // FUNCTION
        a.kind = Function;
        a.value = b1;
    }
    return a;
}

QByteArray ButtonAction::toBytes() const
{
    QByteArray b(4, char(0));
    switch (kind) {
    case Mouse:
        b[0] = char(0x80); b[1] = 0x01; b[2] = char(value >> 8); b[3] = char(value & 0xff);
        break;
    case Key:
        b[0] = char(0x80); b[1] = 0x02; b[2] = char(modifiers); b[3] = char(value & 0xff);
        break;
    case Consumer:
        b[0] = char(0x80); b[1] = 0x03; b[2] = char(value >> 8); b[3] = char(value & 0xff);
        break;
    case Function:
        b[0] = char(0x90); b[1] = char(value); // G Hub zapisuje 90 xx 00 00
        break;
    case Disabled:
        b[0] = char(0x80); b[1] = 0x00; b[2] = char(0xff); b[3] = char(0xff);
        break;
    case Raw:
        return raw.size() == 4 ? raw : QByteArray(4, char(0xff));
    }
    return b;
}

namespace {
struct KeyPair { int qt; int hid; };
const KeyPair kKeys[] = {
    {Qt::Key_Return, 0x28}, {Qt::Key_Enter, 0x28}, {Qt::Key_Escape, 0x29}, {Qt::Key_Backspace, 0x2a},
    {Qt::Key_Tab, 0x2b}, {Qt::Key_Space, 0x2c}, {Qt::Key_Minus, 0x2d}, {Qt::Key_Equal, 0x2e},
    {Qt::Key_BracketLeft, 0x2f}, {Qt::Key_BracketRight, 0x30}, {Qt::Key_Backslash, 0x31},
    {Qt::Key_Semicolon, 0x33}, {Qt::Key_Apostrophe, 0x34}, {Qt::Key_QuoteLeft, 0x35},
    {Qt::Key_Comma, 0x36}, {Qt::Key_Period, 0x37}, {Qt::Key_Slash, 0x38}, {Qt::Key_CapsLock, 0x39},
    {Qt::Key_Print, 0x46}, {Qt::Key_ScrollLock, 0x47}, {Qt::Key_Pause, 0x48}, {Qt::Key_Insert, 0x49},
    {Qt::Key_Home, 0x4a}, {Qt::Key_PageUp, 0x4b}, {Qt::Key_Delete, 0x4c}, {Qt::Key_End, 0x4d},
    {Qt::Key_PageDown, 0x4e}, {Qt::Key_Right, 0x4f}, {Qt::Key_Left, 0x50}, {Qt::Key_Down, 0x51},
    {Qt::Key_Up, 0x52}, {Qt::Key_Menu, 0x65},
};
}

int qtKeyToHid(int k)
{
    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return 0x04 + (k - Qt::Key_A);
    if (k >= Qt::Key_1 && k <= Qt::Key_9)
        return 0x1e + (k - Qt::Key_1);
    if (k == Qt::Key_0)
        return 0x27;
    if (k >= Qt::Key_F1 && k <= Qt::Key_F12)
        return 0x3a + (k - Qt::Key_F1);
    if (k >= Qt::Key_F13 && k <= Qt::Key_F24)
        return 0x68 + (k - Qt::Key_F13);
    for (const auto &p : kKeys)
        if (p.qt == k)
            return p.hid;
    return 0;
}

int hidToQtKey(int h)
{
    if (h >= 0x04 && h <= 0x1d)
        return Qt::Key_A + (h - 0x04);
    if (h >= 0x1e && h <= 0x26)
        return Qt::Key_1 + (h - 0x1e);
    if (h == 0x27)
        return Qt::Key_0;
    if (h >= 0x3a && h <= 0x45)
        return Qt::Key_F1 + (h - 0x3a);
    if (h >= 0x68 && h <= 0x73)
        return Qt::Key_F13 + (h - 0x68);
    for (const auto &p : kKeys)
        if (p.hid == h)
            return p.qt;
    return 0;
}

bool Effect::known(quint8 id)
{
    return id == Off || id == Static || id == Cycle || id == Breathe || id == CycleSat || id == Wave;
}

QByteArray Effect::params() const
{
    if (!known(id) && raw.size() == 11)
        return raw.mid(1);
    QByteArray p(10, char(0));
    const char r = char(rgb >> 16), g = char(rgb >> 8), b = char(rgb);
    const quint16 period = quint16(qBound(100, periodMs, 65535));
    const char inten = char(qBound(1, intensity, 100));
    switch (id) {
    case Static:
        p[0] = r; p[1] = g; p[2] = b;
        break;
    case Breathe:
        p[0] = r; p[1] = g; p[2] = b;
        p[3] = char(period >> 8); p[4] = char(period); p[6] = inten;
        break;
    case Cycle:
        p[5] = char(period >> 8); p[6] = char(period); p[7] = inten;
        break;
    case CycleSat:
    case Wave:
        p[1] = char(0xff); p[6] = char(period >> 8); p[7] = char(period); p[8] = inten;
        break;
    default:
        break;
    }
    return p;
}

Effect Effect::fromBytes(const QByteArray &b)
{
    Effect e;
    if (b.size() < 11)
        return e;
    e.id = quint8(b[0]);
    e.raw = b;
    auto u8 = [&](int off) { return quint8(b[1 + off]); };
    auto u16 = [&](int off) { return (u8(off) << 8) | u8(off + 1); };
    switch (e.id) {
    case Static:
        e.rgb = (u8(0) << 16) | (u8(1) << 8) | u8(2);
        break;
    case Breathe:
        e.rgb = (u8(0) << 16) | (u8(1) << 8) | u8(2);
        e.periodMs = u16(3);
        e.intensity = u8(6);
        break;
    case Cycle:
        e.periodMs = u16(5);
        e.intensity = u8(7);
        break;
    case CycleSat:
    case Wave:
        e.periodMs = u16(6);
        e.intensity = u8(8);
        break;
    default:
        break;
    }
    if (e.intensity <= 0)
        e.intensity = 100;
    if (e.periodMs <= 0)
        e.periodMs = 5000;
    return e;
}

int Profile::dpi(int i) const
{
    return quint8(raw[3 + i * 2]) | (quint8(raw[4 + i * 2]) << 8); // little-endian
}

void Profile::setDpi(int i, int v)
{
    raw[3 + i * 2] = char(v & 0xff);
    raw[4 + i * 2] = char(v >> 8);
}

void Profile::setButton(int i, const ButtonAction &a)
{
    raw.replace(32 + i * 4, 4, a.toBytes());
}

void Profile::setLighting(int i, const Effect &e)
{
    raw.replace(208 + i * 11, 11, e.toBytes());
}

bool Profile::crcValid() const
{
    if (raw.size() < 3)
        return false;
    const quint16 stored = quint16((quint8(raw[raw.size() - 2]) << 8) | quint8(raw[raw.size() - 1]));
    return crc16(raw.left(raw.size() - 2)) == stored;
}

void Profile::fixCrc()
{
    const quint16 c = crc16(raw.left(raw.size() - 2));
    raw[raw.size() - 2] = char(c >> 8);
    raw[raw.size() - 1] = char(c & 0xff);
}

} // namespace hidpp
