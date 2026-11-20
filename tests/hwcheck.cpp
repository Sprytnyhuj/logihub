// Ręczny test na sprzęcie: hwcheck /dev/hidrawN <index-hex> [--rewrite-same]
// Odczytuje profile myszy; z --rewrite-same zapisuje aktywny/pierwszy profil z powrotem bez zmian
// (sprawdza ścieżkę zapisu, nie zmieniając ustawień).
#include "hidpp.h"
#include "onboard.h"
#include "openrgb.h"

#include <QStringList>

#include <cstdio>

int main(int argc, char **argv)
{
    if (argc >= 2 && QByteArray(argv[1]) == "--openrgb") { // tylko odczyt listy urządzeń
        orgb::Client c;
        if (!c.connectToServer()) {
            std::puts("brak serwera OpenRGB");
            return 1;
        }
        std::printf("protokół %d, profil serwera: %s\n", c.protocol(), qPrintable(orgb::serverProfileName()));
        for (const auto &ctl : c.controllers()) {
            std::printf("[%d] %s (%s) typ=%d ledy=%d tryb=%d\n", ctl.index, qPrintable(ctl.name),
                        qPrintable(ctl.vendor), ctl.type, ctl.ledCount, ctl.activeMode);
            QStringList modes;
            for (const auto &m : ctl.modes)
                modes << QString("%1(c%2)").arg(m.name).arg(m.colorMode);
            std::printf("     tryby: %s\n", qPrintable(modes.join(", ")));
        }
        if (argc >= 4 && QByteArray(argv[2]) == "--load-profile") {
            const bool ok = c.loadProfile(QString::fromLocal8Bit(argv[3]));
            std::printf("wczytanie profilu %s: %s\n", argv[3], ok ? "wysłano" : "BŁĄD");
            for (const auto &ctl : c.controllers()) {
                const auto &m = ctl.modes.value(ctl.activeMode);
                std::printf("  %s: tryb %s kolor trybu #%06x\n", qPrintable(ctl.name), qPrintable(m.name),
                            m.colors.isEmpty() ? 0u : orgb::toRgb(m.colors.first()));
            }
            return ok ? 0 : 1;
        }
        if (argc >= 3 && QByteArray(argv[2]) == "--reapply-same") {
            // Test zapisu bez widocznej zmiany: wysyła obecny kolor obecnego trybu.
            // Tryb „mode specific” - kolor trybu; tryb „per LED” - tylko gdy wszystkie diody mają ten sam kolor.
            const auto before = c.controllers();
            for (const auto &ctl : before) {
                const auto &m = ctl.modes.value(ctl.activeMode);
                quint32 col;
                if (m.colorMode == orgb::ColorsModeSpecific && m.colors.size() == 1)
                    col = m.colors.first();
                else if (m.colorMode == orgb::ColorsPerLed && !ctl.colors.isEmpty()
                         && ctl.colors.count(ctl.colors.first()) == ctl.colors.size())
                    col = ctl.colors.first();
                else
                    continue; // brak jednoznacznego „obecnego koloru” - nie ruszamy
                const quint32 rgb = orgb::toRgb(col);
                const bool ok = c.applyColor(ctl, ctl.activeMode, rgb);
                std::printf("  %s: tryb %d kolor #%06x -> %s\n", qPrintable(ctl.name), ctl.activeMode, rgb,
                            ok ? "wysłano" : "BŁĄD");
            }
            const auto after = c.controllers();
            for (int i = 0; i < before.size() && i < after.size(); ++i)
                std::printf("  %s: stan po zapisie %s\n", qPrintable(after[i].name),
                            before[i].colors == after[i].colors && before[i].activeMode == after[i].activeMode
                                    && before[i].modes.value(before[i].activeMode).colors
                                           == after[i].modes.value(after[i].activeMode).colors
                                ? "identyczny"
                                : "INNY");
        }
        return 0;
    }
    if (argc < 3) {
        std::fprintf(stderr, "użycie: hwcheck /dev/hidrawN index [--rewrite-same]\n");
        return 2;
    }
    HidppDevice d(QString::fromLocal8Bit(argv[1]), quint8(QByteArray(argv[2]).toUInt(nullptr, 16)));
    if (!d.ping()) {
        std::puts("brak odpowiedzi (uśpione?)");
        return 1;
    }
    std::printf("nazwa: %s\n", qPrintable(d.name()));
    auto oi = onboard::info(d);
    if (!oi) {
        std::puts("brak profili wbudowanych");
        return 0;
    }
    std::printf("profile: %d, przyciski: %d, sektor: %d B, tryb: %d, aktywny: %d\n", oi->count, oi->buttons,
                oi->size, onboard::mode(d), onboard::activeSector(d));
    int target = -1;
    for (const auto &h : onboard::headers(d)) {
        auto raw = onboard::readSector(d, h.sector, oi->size);
        hidpp::Profile p{raw.value_or(QByteArray())};
        std::printf("  sektor %d %s crc=%s dpi=%d/%d/%d/%d/%d rate=%dms\n", h.sector, h.enabled ? "wł." : "wył.",
                    raw && p.crcValid() ? "OK" : "ZŁE", p.dpi(0), p.dpi(1), p.dpi(2), p.dpi(3), p.dpi(4),
                    raw ? p.reportRateMs() : -1);
        if (target < 0 && h.enabled && h.sector < oi->sectors && raw && p.crcValid())
            target = h.sector;
    }
    if (argc > 3 && QByteArray(argv[3]) == "--rewrite-same" && target >= 0) {
        auto raw = onboard::readSector(d, target, oi->size);
        const bool ok = raw && onboard::writeSector(d, target, *raw);
        auto again = onboard::readSector(d, target, oi->size);
        std::printf("zapis tego samego do sektora %d: %s, odczyt po zapisie identyczny: %s\n", target,
                    ok ? "OK" : "BŁĄD", again && raw && *again == *raw ? "tak" : "NIE");
        return ok ? 0 : 1;
    }
    return 0;
}
