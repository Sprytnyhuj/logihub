// Ręczny test na sprzęcie: hwcheck /dev/hidrawN <index-hex> [--rewrite-same]
// Odczytuje profile myszy; z --rewrite-same zapisuje aktywny/pierwszy profil z powrotem bez zmian
// (sprawdza ścieżkę zapisu, nie zmieniając ustawień).
#include "hidpp.h"
#include "onboard.h"

#include <cstdio>

int main(int argc, char **argv)
{
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
