#include "devices.h"
#include "hidpp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace {
QHash<QString, QString> readUevent(const QString &dir)
{
    QHash<QString, QString> m;
    QFile f(dir + "/uevent");
    if (!f.open(QIODevice::ReadOnly))
        return m;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const int eq = line.indexOf('=');
        if (eq > 0)
            m.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)));
    }
    return m;
}

QString hidrawOf(const QString &hidDevDir)
{
    const QStringList l = QDir(hidDevDir + "/hidraw").entryList({"hidraw*"}, QDir::Dirs);
    return l.isEmpty() ? QString() : "/dev/" + l.first();
}

QString cleanName(QString n)
{
    if (n.startsWith("Logitech "))
        n = n.mid(9);
    return n.trimmed();
}

// Cache wyniku "czy ten węzeł mówi HID++" - żeby nie pingować co kilka sekund.
QHash<QString, bool> g_isHidpp;
}

QList<DeviceInfo> scanDevices()
{
    QList<DeviceInfo> out;
    QSet<QString> seen;
    const QDir cls("/sys/class/hidraw");
    for (const QString &node : cls.entryList({"hidraw*"}, QDir::Dirs | QDir::System, QDir::Name)) {
        const QString dev = QFileInfo(cls.filePath(node) + "/device").canonicalFilePath();
        const auto ue = readUevent(dev);
        if (!ue.value("HID_ID").toUpper().contains(":0000046D:"))
            continue;
        const QString driver = QFileInfo(dev + "/driver").canonicalFilePath().section('/', -1);
        if (driver == "logitech-djreceiver")
            continue; // sam odbiornik - urządzenia pod nim wykrywamy osobno

        DeviceInfo d;
        d.name = cleanName(ue.value("HID_NAME"));
        const QString uniq = ue.value("HID_UNIQ");
        d.key = uniq.isEmpty() ? d.name : d.name + " " + uniq;

        if (driver == "logitech-hidpp-device") {
            // Urządzenie bezprzewodowe: HID++ idzie przez interfejs odbiornika (katalog nadrzędny).
            const QString phys = ue.value("HID_PHYS");
            d.index = quint8(phys.section(':', -1).toUInt());
            d.hidraw = hidrawOf(QFileInfo(dev).path());
            d.wireless = true;
        } else {
            const QString path = "/dev/" + node;
            auto it = g_isHidpp.constFind(path + d.name);
            bool ok;
            if (it != g_isHidpp.constEnd()) {
                ok = *it;
            } else {
                HidppDevice probe(path, 0xff);
                ok = probe.ping();
                g_isHidpp.insert(path + d.name, ok);
            }
            if (ok)
                d.hidraw = path;
            else if (seen.contains(d.key))
                continue; // kolejny interfejs tego samego urządzenia bez HID++
        }
        if (seen.contains(d.key)) {
            // urządzenie z kilkoma interfejsami - zostaw ten z HID++
            if (d.hidraw.isEmpty())
                continue;
            out.removeIf([&](const DeviceInfo &x) { return x.key == d.key; });
        }
        seen.insert(d.key);
        out.append(d);
    }
    return out;
}

QString audioKeyword(const QString &name)
{
    if (name.startsWith("Yeti"))
        return "Yeti";
    return name.section(' ', 0, 0);
}
