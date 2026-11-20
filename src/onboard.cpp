#include "onboard.h"
#include "hidpp.h"

namespace onboard {
namespace {
QByteArray bytes(std::initializer_list<int> l)
{
    QByteArray b;
    for (int v : l)
        b.append(char(v));
    return b;
}
}

std::optional<Info> info(HidppDevice &d)
{
    auto r = d.call(feat::OnboardProfiles, 0x00);
    if (!r)
        return std::nullopt;
    const auto u = [&](int i) { return quint8(r->at(i)); };
    if (u(0) != 0x01) // nieznany model pamięci
        return std::nullopt;
    Info i;
    i.count = u(3);
    i.buttons = u(5);
    i.sectors = u(6);
    i.size = (u(7) << 8) | u(8);
    if (i.size < 64 || i.size > 1024 || i.buttons > 16)
        return std::nullopt;
    return i;
}

QList<Header> headers(HidppDevice &d)
{
    QList<Header> out;
    int page = 0x00;
    auto chunk = d.call(feat::OnboardProfiles, 0x50, bytes({0, 0, 0, 0}));
    if (!chunk)
        return out;
    if (chunk->left(4) == QByteArray(4, char(0)) || chunk->left(4) == QByteArray(4, char(0xff))) {
        page = 0x01; // brak profili użytkownika - tylko ROM
        chunk = d.call(feat::OnboardProfiles, 0x50, bytes({1, 0, 0, 0}));
    }
    for (int i = 0; chunk && i < 16; ++i) {
        // 4 bajty na wpis; w jednym odczycie 16 bajtów = 4 wpisy, ale czytamy po kolei jak Solaar
        const QByteArray e = chunk->left(4);
        if (quint8(e[0]) == 0xff && quint8(e[1]) == 0xff)
            break;
        out.append({(quint8(e[0]) << 8) | quint8(e[1]), quint8(e[2]) != 0});
        chunk = d.call(feat::OnboardProfiles, 0x50, bytes({page, 0, 0, (i + 1) * 4}));
    }
    return out;
}

std::optional<QByteArray> readSector(HidppDevice &d, int sector, int size)
{
    QByteArray out;
    int o = 0;
    for (; o < size - 15; o += 16) {
        auto c = d.call(feat::OnboardProfiles, 0x50, bytes({sector >> 8, sector & 0xff, o >> 8, o & 0xff}));
        if (!c)
            return std::nullopt;
        out += *c;
    }
    if (out.size() < size) { // ostatni kawałek czytany "od tyłu"
        const int last = size - 16;
        auto c = d.call(feat::OnboardProfiles, 0x50, bytes({sector >> 8, sector & 0xff, last >> 8, last & 0xff}));
        if (!c)
            return std::nullopt;
        out += c->mid(16 + o - size);
    }
    return out.left(size);
}

bool writeSector(HidppDevice &d, int sector, const QByteArray &bs)
{
    const int len = int(bs.size());
    if (!d.call(feat::OnboardProfiles, 0x60, bytes({sector >> 8, sector & 0xff, 0, 0, len >> 8, len & 0xff})))
        return false;
    for (int o = 0; o < len; o += 16) {
        QByteArray c = bs.mid(o, 16);
        c.append(QByteArray(16 - c.size(), char(0xff)));
        if (!d.call(feat::OnboardProfiles, 0x70, c))
            return false;
    }
    if (!d.call(feat::OnboardProfiles, 0x80))
        return false;
    auto back = readSector(d, sector, len);
    return back && *back == bs;
}

int mode(HidppDevice &d)
{
    auto r = d.call(feat::OnboardProfiles, 0x20);
    return r ? quint8(r->at(0)) : -1;
}

bool setMode(HidppDevice &d, int m)
{
    return d.call(feat::OnboardProfiles, 0x10, bytes({m})).has_value();
}

int activeSector(HidppDevice &d)
{
    auto r = d.call(feat::OnboardProfiles, 0x40);
    return r ? (quint8(r->at(0)) << 8) | quint8(r->at(1)) : -1;
}

bool activate(HidppDevice &d, int sector)
{
    return d.call(feat::OnboardProfiles, 0x30, bytes({sector >> 8, sector & 0xff})).has_value();
}

} // namespace onboard
