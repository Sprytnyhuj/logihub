#!/usr/bin/env bash
# Uruchamiane W KONTENERZE (fedora:42): buduje LogiHub i pakuje go razem z bibliotekami Qt6,
# żeby działał na systemach bez Qt (np. GNOME na Silverblue / Bluefin). Źródła w /src, wynik w /out.
set -euo pipefail
VERSION="${1:-dev}"

dnf -y -q install cmake ninja-build gcc gcc-c++ qt6-qtbase-devel qt6-qtwayland patchelf file ladspa-devel >/dev/null
dnf -y -q install rnnoise-devel >/dev/null 2>&1 || echo "Uwaga: brak rnnoise-devel - paczka bez RNNoise"

cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
ninja -C /build logihub logihub_dsp
ninja -C /build logihub_rnnoise 2>/dev/null || true

D=/stage/logihub
rm -rf /stage && mkdir -p "$D/bin" "$D/lib/ladspa" "$D/plugins"
cp /build/logihub "$D/bin/"
strip "$D/bin/logihub"
# Wtyczki Voice FX (+ librnnoise.so.0 obok, rpath $ORIGIN)
cp /build/ladspa/* "$D/lib/ladspa/"
strip "$D"/lib/ladspa/logihub_*.so
echo "== wtyczki Voice FX: $(ls "$D/lib/ladspa" | tr '\n' ' ')"

QTPLUG=/usr/lib64/qt6/plugins
cd "$QTPLUG"
# platforms/libqwayland*.so - nazwa pliku zmienia się między wersjami Qt (generic / egl / libqwayland.so)
for p in platforms/libqwayland*.so platforms/libqxcb.so platforms/libqoffscreen.so \
         wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client \
         xcbglintegrations tls; do
    [ -e "$p" ] || continue
    mkdir -p "$D/plugins/$(dirname "$p")"
    cp -r "$p" "$D/plugins/$(dirname "$p")/"
done
cd /
ls "$D/plugins/platforms/" | grep -q wayland || { echo "BŁĄD: brak wtyczki Wayland w paczce"; exit 1; }

# Biblioteki systemowe, których NIE pakujemy (muszą pochodzić z systemu użytkownika:
# glibc, sterowniki grafiki, X11/Wayland, czcionki, D-Bus, glib...).
EXCLUDE='^(ld-linux|libc\.so|libm\.so|libdl|libpthread|librt|libresolv|libutil|libgcc_s|libstdc\+\+|libGL|libEGL|libGLX|libGLdispatch|libOpenGL|libdrm|libgbm|libvulkan|libwayland-|libxcb|libX|libxkbcommon|libfontconfig|libfreetype|libharfbuzz|libgraphite2|libpng|libz\.so|libbz2|libbrotli|libexpat|libdbus-1|libsystemd|libudev|libcap|libglib-2|libgobject|libgio|libgmodule|libgthread|libpcre2-8|libffi|libselinux|libmount|libblkid|libuuid|libgcrypt|libgpg-error|liblzma|libzstd|liblz4|libcrypt|libgssapi|libkrb5|libk5crypto|libcom_err|libkeyutils|libssl|libcrypto|libmtdev|libinput|libevdev|libgudev)'

# ...ale te dodatki X11 bywają nieobecne na czystym GNOME - pakujemy je mimo wzorca wyżej.
INCLUDE='^(libxcb-cursor|libxcb-icccm|libxcb-image|libxcb-keysyms|libxcb-render-util|libxcb-util|libxkbcommon-x11)'

copy_deps() {
    ldd "$1" | awk '/=> \//{print $3}' | while read -r lib; do
        base=$(basename "$lib")
        [[ "$base" =~ $EXCLUDE ]] && ! [[ "$base" =~ $INCLUDE ]] && continue
        [ -e "$D/lib/$base" ] && continue
        cp -L "$lib" "$D/lib/$base"
        copy_deps "$lib"
    done
}
copy_deps "$D/bin/logihub"
find "$D/plugins" -name '*.so' | while read -r so; do copy_deps "$so"; done

# Wszystko szuka bibliotek najpierw w paczce
patchelf --set-rpath '$ORIGIN/../lib' "$D/bin/logihub"
for so in "$D"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$so" 2>/dev/null || true; done
find "$D/plugins" -name '*.so' | while read -r so; do
    depth=$(( $(echo "${so#$D/plugins/}" | tr -cd '/' | wc -c) + 1 ))
    patchelf --set-rpath "\$ORIGIN/$(printf '../%.0s' $(seq $depth))lib" "$so"
done

cat > "$D/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
EOF

cp /src/packaging/bundle-install.sh "$D/install.sh"
chmod +x "$D/install.sh"
cp -r /src/udev "$D/"
cp /src/README.md "$D/"
echo "$VERSION" > "$D/VERSION"

echo "== spakowane biblioteki:"
ls "$D/lib"
mkdir -p /out
tar -C /stage -czf "/out/logihub-$VERSION-x86_64.tar.gz" logihub
echo "== gotowe: /out/logihub-$VERSION-x86_64.tar.gz ($(du -h "/out/logihub-$VERSION-x86_64.tar.gz" | cut -f1))"
