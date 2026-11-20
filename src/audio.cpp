#include "audio.h"

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
        if (nm.contains(".monitor"))
            continue;
        out.append({o.value("id").toInt(), desc, cls == "Audio/Sink"});
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
