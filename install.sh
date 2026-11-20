#!/usr/bin/env bash
# LogiHub - budowanie (w distroboxie "builder") i instalacja dla bieżącego użytkownika.
#   ./install.sh              budowanie + instalacja / aktualizacja
#   ./install.sh --autostart  dodatkowo uruchamianie przy logowaniu (w zasobniku)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
SRC="$PWD"
BIN_DIR="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"

build() {
    cmake -S "$SRC" -B "$SRC/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    ninja -C "$SRC/build"
}

if command -v cmake >/dev/null && [ -f /usr/lib64/cmake/Qt6/Qt6Config.cmake ]; then
    build
else
    distrobox enter builder -- bash -c "cd '$SRC' && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && ninja -C build"
fi

mkdir -p "$BIN_DIR" "$APPS"
install -m 755 "$SRC/build/logihub" "$BIN_DIR/logihub"
cat > "$APPS/logihub.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LogiHub
GenericName=Ustawienia urządzeń Logitech
Comment=Mysz, mikrofon i słuchawki Logitech – DPI, przyciski, RGB, bateria
Exec=$BIN_DIR/logihub
Icon=input-gaming
Categories=Settings;HardwareSettings;
Keywords=logitech;ghub;mouse;dpi;rgb;
EOF

if [[ "${1:-}" == "--autostart" ]]; then
    mkdir -p "$HOME/.config/autostart"
    sed "s|^Exec=.*|Exec=$BIN_DIR/logihub --hidden|" "$APPS/logihub.desktop" > "$HOME/.config/autostart/logihub.desktop"
    echo "Autostart włączony."
fi
echo "Zainstalowano: $BIN_DIR/logihub (menu: LogiHub)"
