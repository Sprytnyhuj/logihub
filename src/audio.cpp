#include "audio.h"

#include <QFile>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace {
QByteArray run(const QString &prog, const QStringList &args, int timeoutMs = 2000)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    return p.readAllStandardOutput();
}
}

namespace audio {

QList<AudioDevice> externalDevices()
{
    const auto doc = QJsonDocument::fromJson(run("pw-dump", {}, 4000));
    QHash<int, QString> deviceDesc;
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() == "PipeWire:Interface:Device")
            deviceDesc.insert(o.value("id").toInt(),
                              o.value("info").toObject().value("props").toObject().value("device.description").toString());
    }
    QMap<int, AudioDevice> byDevice;
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != "PipeWire:Interface:Node")
            continue;
        const QJsonObject props = o.value("info").toObject().value("props").toObject();
        const QString nm = props.value("node.name").toString();
        const QString cls = props.value("media.class").toString();
        const bool bt = nm.startsWith("bluez_");
        // LOGIHUB_AUDIO_ALL=1 - także karty wbudowane / HDMI (np. do testów albo EQ na głośnikach)
        static const bool all = qEnvironmentVariableIntValue("LOGIHUB_AUDIO_ALL") == 1;
        const bool usb = nm.startsWith("alsa_") && (all || nm.contains(".usb-"));
        if ((!bt && !usb) || nm.contains("Logitech", Qt::CaseInsensitive) || nm.contains(".monitor"))
            continue;
        if (cls != "Audio/Sink" && cls != "Audio/Source")
            continue;
        const int dev = props.value("device.id").toInt(-1);
        AudioDevice &d = byDevice[dev >= 0 ? dev : -o.value("id").toInt()];
        d.bluetooth = bt;
        if (d.description.isEmpty())
            d.description = deviceDesc.value(dev, props.value("node.description").toString());
        (cls == "Audio/Sink" ? d.sinkNode : d.sourceNode) = nm;
    }
    return byDevice.values();
}

QList<AudioNode> nodesByName(const QStringList &names)
{
    QList<AudioNode> out;
    const auto doc = QJsonDocument::fromJson(run("pw-dump", {}, 4000));
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QJsonObject props = o.value("info").toObject().value("props").toObject();
        const QString nm = props.value("node.name").toString();
        if (!nm.isEmpty() && names.contains(nm))
            out.append({o.value("id").toInt(), nm, props.value("node.description").toString(),
                        props.value("media.class").toString() == "Audio/Sink"});
    }
    return out;
}

int nodeIdByName(const QString &nodeName)
{
    const auto doc = QJsonDocument::fromJson(run("pw-dump", {}, 4000));
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("info").toObject().value("props").toObject().value("node.name").toString() == nodeName)
            return o.value("id").toInt();
    }
    return -1;
}

HwGain micHwGain(const QString &keyword)
{
    HwGain g;
    QFile f("/proc/asound/cards");
    if (!f.open(QIODevice::ReadOnly))
        return g;
    // " 4 [GX             ]: USB-Audio - Yeti GX"
    static const QRegularExpression re("^\\s*\\d+\\s+\\[(\\S+)\\s*\\]:.*- (.*)$");
    for (const QByteArray &line : f.readAll().split('\n')) {
        const auto m = re.match(QString::fromUtf8(line));
        if (m.hasMatch() && m.captured(2).contains(keyword, Qt::CaseInsensitive)) {
            g.card = m.captured(1);
            break;
        }
    }
    if (g.card.isEmpty())
        return g;
    const QString out = QString::fromUtf8(run("amixer", {"-c", g.card, "sget", "Mic"}));
    static const QRegularExpression lim("Limits: Capture (\\d+) - (\\d+)");
    static const QRegularExpression cur("Capture (\\d+) \\[");
    const auto lm = lim.match(out);
    const auto cm = cur.match(out);
    if (lm.hasMatch() && cm.hasMatch()) {
        g.max = lm.captured(2).toInt();
        g.value = cm.captured(1).toInt();
    }
    return g;
}

void setMicHwGain(const QString &card, int value)
{
    QProcess::startDetached("amixer", {"-q", "-c", card, "sset", "Mic", QString::number(value)});
}

QList<AudioNode> findNodes(const QString &keyword)
{
    QList<AudioNode> out;
    const auto doc = QJsonDocument::fromJson(run("pw-dump", {}, 4000));
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("type").toString() != "PipeWire:Interface:Node")
            continue;
        const QJsonObject props = o.value("info").toObject().value("props").toObject();
        const QString cls = props.value("media.class").toString();
        if (cls != "Audio/Sink" && cls != "Audio/Source")
            continue;
        const QString desc = props.value("node.description").toString();
        const QString nm = props.value("node.name").toString();
        if (!desc.contains(keyword, Qt::CaseInsensitive) && !nm.contains(keyword, Qt::CaseInsensitive))
            continue;
        if (nm.contains(".monitor") || nm.startsWith("logihub."))
            continue;
        out.append({o.value("id").toInt(), nm, desc, cls == "Audio/Sink"});
    }
    return out;
}

int volume(int id, bool *muted)
{
    // "Volume: 0.45 [MUTED]"
    const QString s = QString::fromUtf8(run("wpctl", {"get-volume", QString::number(id)}));
    static const QRegularExpression re("Volume:\\s*([0-9.]+)");
    const auto m = re.match(s);
    if (!m.hasMatch())
        return -1;
    if (muted)
        *muted = s.contains("MUTED");
    return qRound(m.captured(1).toDouble() * 100.0);
}

void setVolume(int id, int percent)
{
    QProcess::startDetached("wpctl", {"set-volume", QString::number(id), QString::number(percent / 100.0, 'f', 2)});
}

void setMuted(int id, bool muted)
{
    QProcess::startDetached("wpctl", {"set-mute", QString::number(id), muted ? "1" : "0"});
}

} // namespace audio
