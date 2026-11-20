#pragma once
#include <QList>
#include <QString>

// Urządzenie Logitech znalezione w /sys/class/hidraw.
struct DeviceInfo {
    QString key;      // stały identyfikator (seria albo nazwa) - klucz ustawień
    QString name;     // np. "G502 X PLUS"
    QString hidraw;   // /dev/hidrawN do HID++ (pusty gdy brak HID++)
    quint8 index = 0xff;
    bool wireless = false; // przez odbiornik Lightspeed / Bluetooth
    // "logitech" (HID / audio Logitech), "audio" (słuchawki/mikrofon innej firmy), "openrgb" (RGB przez OpenRGB)
    QString kind = "logitech";
    QString sinkNode, sourceNode; // dla kind == "audio"
    bool operator==(const DeviceInfo &o) const { return key == o.key && hidraw == o.hidraw && index == o.index; }
};

QList<DeviceInfo> scanDevices();
// Słowo kluczowe do dopasowania urządzenia audio w PipeWire ("G735", "Yeti").
QString audioKeyword(const QString &name);
