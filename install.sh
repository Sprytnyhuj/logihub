#!/usr/bin/env bash
# LogiHub - budowanie i instalacja dla bieżącego użytkownika (bez roota).
#   ./install.sh              budowanie + instalacja / aktualizacja
#   ./install.sh --autostart  dodatkowo uruchamianie przy logowaniu (w zasobniku)
# Systemy niezmienne (Bazzite, Silverblue): gdy brak Qt6 na hoście, budowanie w distroboxie "builder".
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
SRC="$PWD"
BIN_DIR="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"

has_qt6_dev() {
    command -v cmake >/dev/null && command -v ninja >/dev/null \
        && ls /usr/lib*/cmake/Qt6/Qt6Config.cmake /usr/lib/*/cmake/Qt6/Qt6Config.cmake >/dev/null 2>&1
}

if has_qt6_dev; then
    cmake -S "$SRC" -B "$SRC/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    ninja -C "$SRC/build"
elif command -v distrobox >/dev/null && distrobox list 2>/dev/null | grep -q " builder "; then
    distrobox enter builder -- bash -c "cd '$SRC' && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && ninja -C build"
else
    echo "Brak narzędzi do budowania (cmake, ninja, Qt6). Zainstaluj:"
    echo "  Fedora:         sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel"
    echo "  Ubuntu/Debian:  sudo apt install cmake ninja-build g++ qt6-base-dev"
    echo "  Arch:           sudo pacman -S cmake ninja qt6-base"
    exit 1
fi

mkdir -p "$BIN_DIR" "$APPS"
install -m 755 "$SRC/build/logihub" "$BIN_DIR/logihub"
# Wtyczki Voice FX (bramka, kompresor, de-esser, limiter, RNNoise)
if [ -d "$SRC/build/ladspa" ]; then
    LADSPA_DIR="$HOME/.local/lib/logihub/ladspa"
    mkdir -p "$LADSPA_DIR"
    for f in "$SRC"/build/ladspa/*.so*; do install -m 755 "$f" "$LADSPA_DIR/"; done
fi
# Ikona (rysowana przez samą aplikację) w rozmiarach 16-256 px
QT_QPA_PLATFORM=offscreen "$BIN_DIR/logihub" --export-icons "$HOME/.local/share/icons/hicolor"
touch "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
cat > "$APPS/logihub.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LogiHub
GenericName=Ustawienia urządzeń Logitech
Comment=Mysz, mikrofon i słuchawki Logitech – DPI, przyciski, RGB, bateria
Exec=$BIN_DIR/logihub
Icon=logihub
StartupWMClass=logihub
SingleMainWindow=true
Categories=Settings;HardwareSettings;
Keywords=logitech;ghub;mouse;dpi;rgb;
EOF

if [[ "${1:-}" == "--autostart" ]]; then
    mkdir -p "$HOME/.config/autostart"
    sed "s|^Exec=.*|Exec=$BIN_DIR/logihub --hidden|" "$APPS/logihub.desktop" > "$HOME/.config/autostart/logihub.desktop"
    echo "Autostart włączony."
fi
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true # odśwież menu KDE
# istniejący wpis autostartu - podmień ikonę
[ -f "$HOME/.config/autostart/logihub.desktop" ] && sed -i 's/^Icon=.*/Icon=logihub/' "$HOME/.config/autostart/logihub.desktop"
echo "Zainstalowano: $BIN_DIR/logihub (menu: LogiHub)"

# Uprawnienia do urządzeń Logitech (/dev/hidraw) - na części dystrybucji trzeba dodać regułę udev
for d in /sys/class/hidraw/*; do
    [ -e "$d" ] || continue
    if grep -qi "HID_ID=.*:0000046D:" "$d/device/uevent" 2>/dev/null && [ ! -r "/dev/$(basename "$d")" ]; then
        echo
        echo "Uwaga: brak dostępu do /dev/$(basename "$d") (urządzenie Logitech). Dodaj regułę udev (jednorazowo):"
        echo "  sudo cp '$SRC/udev/70-logihub.rules' /etc/udev/rules.d/"
        echo "  sudo udevadm control --reload && sudo udevadm trigger"
        break
    fi
done
if [ ! -f "$SRC/build/ladspa/logihub_dsp.so" ]; then
    echo
    echo "Uwaga: wtyczki Voice FX nie zostały zbudowane (brak ladspa.h). Doinstaluj i uruchom ./install.sh ponownie:"
    echo "  Fedora: sudo dnf install ladspa-devel rnnoise-devel   Ubuntu: sudo apt install ladspa-sdk librnnoise-dev"
    echo "  Arch:   sudo pacman -S ladspa rnnoise"
fi
