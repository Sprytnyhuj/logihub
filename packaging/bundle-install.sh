#!/usr/bin/env bash
# Instalator gotowej paczki LogiHub (z dołączonym Qt) - bez kompilowania i bez roota.
#   ./install.sh              instalacja / aktualizacja
#   ./install.sh --autostart  dodatkowo uruchamianie przy logowaniu
#   ./install.sh --uninstall  usunięcie
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
APP="$HOME/.local/share/logihub-app"
BIN="$HOME/.local/bin/logihub"
APPS="$HOME/.local/share/applications"

if [[ "${1:-}" == "--uninstall" ]]; then
    pkill -x logihub 2>/dev/null || true
    rm -rf "$APP" "$BIN" "$APPS/logihub.desktop" "$HOME/.config/autostart/logihub.desktop"
    find "$HOME/.local/share/icons/hicolor" -name logihub.png -delete 2>/dev/null || true
    echo "Usunięto LogiHub (ustawienia zostały w ~/.config/logihub)."
    exit 0
fi

pkill -x logihub 2>/dev/null || true
rm -rf "$APP"
mkdir -p "$APP" "$(dirname "$BIN")" "$APPS"
cp -r "$HERE/bin" "$HERE/lib" "$HERE/plugins" "$APP/"
cat > "$BIN" <<EOF
#!/bin/sh
exec "$APP/bin/logihub" "\$@"
EOF
chmod +x "$BIN"

QT_QPA_PLATFORM=offscreen "$APP/bin/logihub" --export-icons "$HOME/.local/share/icons/hicolor"
cat > "$APPS/logihub.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LogiHub
GenericName=Ustawienia urządzeń Logitech
Comment=Mysz, mikrofon i słuchawki – DPI, przyciski, RGB, Blue VO!CE, equalizer
Exec=$BIN
Icon=logihub
StartupWMClass=logihub
SingleMainWindow=true
Categories=Settings;HardwareSettings;
Keywords=logitech;ghub;mouse;dpi;rgb;equalizer;
EOF
if [[ "${1:-}" == "--autostart" ]]; then
    mkdir -p "$HOME/.config/autostart"
    sed "s|^Exec=.*|Exec=$BIN --hidden|" "$APPS/logihub.desktop" > "$HOME/.config/autostart/logihub.desktop"
    echo "Autostart włączony."
fi
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
echo "Zainstalowano LogiHub $(cat "$HERE/VERSION" 2>/dev/null). Uruchom z menu albo poleceniem: logihub"

# Dostęp do urządzeń Logitech bez roota
for d in /sys/class/hidraw/*; do
    [ -e "$d" ] || continue
    if grep -qi "HID_ID=.*:0000046D:" "$d/device/uevent" 2>/dev/null && [ ! -r "/dev/$(basename "$d")" ]; then
        echo
        echo "Uwaga: brak dostępu do urządzeń Logitech. Jednorazowo (wymaga hasła):"
        echo "  sudo cp '$HERE/udev/70-logihub.rules' /etc/udev/rules.d/"
        echo "  sudo udevadm control --reload && sudo udevadm trigger"
        break
    fi
done
if ! ls /usr/lib64/ladspa/caps.so /usr/lib/ladspa/caps.so /usr/lib/x86_64-linux-gnu/ladspa/caps.so >/dev/null 2>&1; then
    echo
    echo "Info: bez wtyczek CAPS Blue VO!CE działa bez bramki, kompresora i limitera."
    echo "  Fedora: sudo dnf install ladspa-caps-plugins  (Silverblue/Bluefin: rpm-ostree install ladspa-caps-plugins)"
fi
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo; echo "Info: dodaj ~/.local/bin do PATH, albo uruchamiaj z menu." ;; esac

# Aktualizacja z poziomu aplikacji: uruchom nową wersję
if [[ "${1:-}" == "--restart" ]]; then
    setsid "$BIN" >/dev/null 2>&1 < /dev/null &
fi
