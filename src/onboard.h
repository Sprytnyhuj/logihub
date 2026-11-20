#pragma once
// Profile wbudowane (HID++ 0x8100) - to samo, co G Hub zapisuje do pamięci myszy.
#include <QByteArray>
#include <QList>
#include <optional>

class HidppDevice;

namespace onboard {

enum Mode { Onboard = 1, Host = 2 };

struct Info {
    int count = 0;   // liczba slotów profili
    int buttons = 0; // liczba przycisków w profilu
    int sectors = 0; // sektory zapisywalne
    int size = 0;    // rozmiar sektora w bajtach
};

struct Header {
    int sector = 0;
    bool enabled = false;
};

std::optional<Info> info(HidppDevice &d);
QList<Header> headers(HidppDevice &d);
std::optional<QByteArray> readSector(HidppDevice &d, int sector, int size);
// Zapisuje i czyta z powrotem; true tylko gdy odczyt zgadza się z zapisem.
bool writeSector(HidppDevice &d, int sector, const QByteArray &bytes);
int mode(HidppDevice &d); // -1 gdy brak odpowiedzi
bool setMode(HidppDevice &d, int mode);
int activeSector(HidppDevice &d);
bool activate(HidppDevice &d, int sector);

} // namespace onboard
