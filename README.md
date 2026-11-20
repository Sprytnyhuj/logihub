# LogiHub

Linuksowy odpowiednik Logitech G Hub w C++/Qt6. Rozmawia z urządzeniami bezpośrednio przez
`/dev/hidraw` (HID++ 2.0), bez roota, bez Pythona i bez sterowników producenta.

## Co obsługuje

| Urządzenie | Funkcje |
|---|---|
| **G502 X PLUS** (Lightspeed) | bateria, tryb pracy (pamięć wbudowana / z aplikacji), **edycja profili w pamięci myszy**: 5 etapów DPI, domyślny etap, DPI Shift, częstotliwość raportowania, **przypisania 11 przycisków** (przyciski myszy, DPI, profile, multimedia, dowolny klawisz ze skrótem), podświetlenie zapisywane w profilu; na żywo: DPI, polling, RGB |
| **Yeti GX** | podświetlenie RGB (2 strefy), jasność, głośność / wyciszenie mikrofonu |
| **G735** | głośność i wyciszenie wyjścia i mikrofonu (PipeWire). Sterowanie sprzętowe (RGB, sidetone, EQ) – jeszcze nie: słuchawki używają nieopisanego protokołu (raporty 0xF0–0xF3) |

Inne myszy i klawiatury HID++ z tymi samymi funkcjami (0x1004, 0x2201, 0x8060, 0x8071, 0x8100) też powinny działać.

## Instalacja

```bash
./install.sh              # buduje w distroboxie "builder" i instaluje do ~/.local/bin
./install.sh --autostart  # + start przy logowaniu, schowany w zasobniku
```

Zamknięcie okna chowa aplikację do zasobnika (jak G Hub). Autostart można też włączyć w menu *Aplikacja*.

## Bezpieczeństwo zapisu profili

- zapisywane są tylko znane pola profilu, reszta bajtów zostaje bez zmian, CRC jest przeliczane,
- przed każdym zapisem robiona jest kopia sektora do `~/.local/share/logihub/backups/`,
- po zapisie profil jest odczytywany z powrotem i porównywany,
- „Przywróć kopię…” wgrywa dowolną kopię z powrotem.

## OpenRGB

Jeśli działa serwer OpenRGB, przejmuje on podświetlenie myszy (tryb software) – wtedy kolor
z profilu nie będzie widoczny. Tryb „Sterowanie z aplikacji” wyłącza fizyczne przyciski DPI.

## Testy

```bash
QT_QPA_PLATFORM=offscreen ./build/test_hidpp          # testy jednostkowe (bez sprzętu)
./build/hwcheck /dev/hidraw6 01                        # odczyt profili z myszy
./build/hwcheck /dev/hidraw6 01 --rewrite-same         # test ścieżki zapisu (zapis bez zmian)
```
