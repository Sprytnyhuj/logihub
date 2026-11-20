#include "dsp.h"
#include "audio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <cmath>
#include <csignal>
#include <sys/prctl.h>

namespace {
QString num(double v)
{
    return QString::number(v, 'f', 4); // zawsze z kropką
}

const char *kHeader = R"PW(context.properties = { log.level = 0 }
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.*       = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-rt flags = [ ifexists nofail ] }
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-adapter }
)PW";

// Przeliczenie suwaków UI (0-100 %) na parametry efektów
double compThresholdDb(const VoiceSettings &v) { return -5.0 - v.compThreshold * 0.4; } // 0% -> -5 dB, 100% -> -45 dB
double compRatio(const VoiceSettings &v) { return 1.0 + v.compStrength * 0.09; }        // 0% -> 1:1, 100% -> 10:1
double deessMaxReduction(const VoiceSettings &v) { return v.deessDb * 2.0; }           // suwak 0-10 -> 0-20 dB
bool useRnnoise(const VoiceSettings &v) { return v.nrMode == 0 && !dsp::rnnoisePath().isEmpty(); }

QString quoteDesc(QString s)
{
    s.replace('"', '\'');
    return '"' + s + '"';
}
}

// ================= VoiceSettings =================
void VoiceSettings::load(QSettings &s, const QString &p)
{
    const VoiceSettings d;
    enabled = s.value(p + "enabled", d.enabled).toBool();
    preset = s.value(p + "preset", d.preset).toInt();
    highpass = s.value(p + "highpass", d.highpass).toBool();
    highpassHz = s.value(p + "highpassHz", d.highpassHz).toInt();
    noiseReduction = s.value(p + "noiseReduction", d.noiseReduction).toBool();
    nrMode = s.value(p + "nrMode", d.nrMode).toInt();
    gate = s.value(p + "gate", d.gate).toBool();
    gateOpenDb = s.value(p + "gateOpenDb", d.gateOpenDb).toInt();
    deesser = s.value(p + "deesser", d.deesser).toBool();
    deessDb = s.value(p + "deessDb", d.deessDb).toInt();
    compressor = s.value(p + "compressor", d.compressor).toBool();
    compThreshold = s.value(p + "compThreshold", d.compThreshold).toInt();
    compStrength = s.value(p + "compStrength", d.compStrength).toInt();
    limiter = s.value(p + "limiter", d.limiter).toBool();
    bassDb = s.value(p + "bassDb", d.bassDb).toInt();
    presenceDb = s.value(p + "presenceDb", d.presenceDb).toInt();
    airDb = s.value(p + "airDb", d.airDb).toInt();
    outGainDb = s.value(p + "outGainDb", d.outGainDb).toInt();
}

void VoiceSettings::save(QSettings &s, const QString &p) const
{
    s.setValue(p + "enabled", enabled);
    s.setValue(p + "preset", preset);
    s.setValue(p + "highpass", highpass);
    s.setValue(p + "highpassHz", highpassHz);
    s.setValue(p + "noiseReduction", noiseReduction);
    s.setValue(p + "nrMode", nrMode);
    s.setValue(p + "gate", gate);
    s.setValue(p + "gateOpenDb", gateOpenDb);
    s.setValue(p + "deesser", deesser);
    s.setValue(p + "deessDb", deessDb);
    s.setValue(p + "compressor", compressor);
    s.setValue(p + "compThreshold", compThreshold);
    s.setValue(p + "compStrength", compStrength);
    s.setValue(p + "limiter", limiter);
    s.setValue(p + "bassDb", bassDb);
    s.setValue(p + "presenceDb", presenceDb);
    s.setValue(p + "airDb", airDb);
    s.setValue(p + "outGainDb", outGainDb);
}

QString VoiceSettings::structureKey() const
{
    return QString("%1%2%3%4%5%6%7")
        .arg(highpass).arg(noiseReduction).arg(nrMode).arg(gate).arg(deesser).arg(compressor).arg(limiter);
}

QList<QPair<QString, double>> VoiceSettings::controls() const
{
    QList<QPair<QString, double>> c;
    if (highpass)
        c << qMakePair(QString("hp:Freq"), double(highpassHz));
    if (gate) {
        c << qMakePair(QString("gate:Open (dB)"), double(gateOpenDb));
        c << qMakePair(QString("gate:Close (dB)"), double(qMax(-90, gateOpenDb - 10)));
    }
    if (deesser)
        c << qMakePair(QString("deess:Max reduction (dB)"), deessMaxReduction(*this));
    if (compressor) {
        c << qMakePair(QString("comp:Threshold (dB)"), compThresholdDb(*this));
        c << qMakePair(QString("comp:Ratio"), compRatio(*this));
    }
    c << qMakePair(QString("bass:Gain"), double(bassDb));
    c << qMakePair(QString("pres:Gain"), double(presenceDb));
    c << qMakePair(QString("air:Gain"), double(airDb));
    c << qMakePair(QString("gain:Mult"), std::pow(10.0, outGainDb / 20.0));
    return c;
}

QStringList VoiceSettings::presetNames()
{
    return {QObject::tr("Własny"), QObject::tr("Broadcast"), QObject::tr("Ciepły i głęboki"),
            QObject::tr("Jasny i wyraźny"), QObject::tr("Gaming – czysty głos"), QObject::tr("Podcast"),
            QObject::tr("Naturalny (tylko redukcja szumów)")};
}

VoiceSettings VoiceSettings::fromPreset(int p)
{
    VoiceSettings v;
    v.preset = p;
    switch (p) {
    case 1: // Broadcast
        v.highpassHz = 80; v.gateOpenDb = -45; v.deessDb = 4; v.compThreshold = 45; v.compStrength = 60;
        v.bassDb = 2; v.presenceDb = 3; v.airDb = 2; v.outGainDb = 3;
        break;
    case 2: // Ciepły
        v.highpassHz = 60; v.gateOpenDb = -48; v.deessDb = 3; v.compThreshold = 40; v.compStrength = 50;
        v.bassDb = 4; v.presenceDb = 1; v.airDb = -1; v.outGainDb = 2;
        break;
    case 3: // Jasny
        v.highpassHz = 100; v.gateOpenDb = -45; v.deessDb = 5; v.compThreshold = 40; v.compStrength = 45;
        v.bassDb = -1; v.presenceDb = 4; v.airDb = 4; v.outGainDb = 2;
        break;
    case 4: // Gaming
        v.highpassHz = 120; v.gateOpenDb = -40; v.deessDb = 3; v.compThreshold = 50; v.compStrength = 55;
        v.bassDb = -1; v.presenceDb = 3; v.airDb = 1; v.outGainDb = 3;
        break;
    case 5: // Podcast
        v.highpassHz = 70; v.gateOpenDb = -50; v.deessDb = 5; v.compThreshold = 55; v.compStrength = 70;
        v.bassDb = 3; v.presenceDb = 2; v.airDb = 1; v.outGainDb = 4;
        break;
    case 6: // Naturalny
        v.highpass = true; v.highpassHz = 60; v.gate = false; v.deesser = false; v.compressor = false;
        v.limiter = true;
        break;
    default:
        break;
    }
    return v;
}

// ================= EqSettings =================
void EqSettings::load(QSettings &s, const QString &p)
{
    enabled = s.value(p + "enabled", false).toBool();
    preset = s.value(p + "preset", 0).toInt();
    const QStringList l = s.value(p + "gains").toStringList();
    for (int i = 0; i < 10; ++i)
        gains[i] = i < l.size() ? l[i].toInt() : 0;
}

void EqSettings::save(QSettings &s, const QString &p) const
{
    s.setValue(p + "enabled", enabled);
    s.setValue(p + "preset", preset);
    QStringList l;
    for (int g : gains)
        l << QString::number(g);
    s.setValue(p + "gains", l);
}

const std::array<int, 10> &EqSettings::frequencies()
{
    static const std::array<int, 10> f{32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    return f;
}

QList<QPair<QString, double>> EqSettings::controls() const
{
    QList<QPair<QString, double>> c;
    for (int i = 0; i < 10; ++i)
        c << qMakePair(QString("eq%1:Gain").arg(i), double(gains[i]));
    return c;
}

QStringList EqSettings::presetNames()
{
    return {QObject::tr("Płaski"), QObject::tr("Wzmocnienie basu"), QObject::tr("FPS (kroki i strzały)"),
            QObject::tr("Filmy"), QObject::tr("Muzyka"), QObject::tr("Głos / rozmowy"), QObject::tr("Własny")};
}

std::array<int, 10> EqSettings::presetGains(int p)
{
    switch (p) {
    case 1: return {6, 5, 4, 2, 0, 0, 0, 0, 0, 0};
    case 2: return {-4, -3, -2, -1, 0, 2, 4, 5, 4, 2};
    case 3: return {4, 3, 2, 0, -1, 0, 2, 3, 2, 1};
    case 4: return {3, 2, 1, 0, -1, 0, 1, 2, 3, 3};
    case 5: return {-3, -2, -1, 1, 3, 4, 3, 1, 0, -1};
    default: return {};
    }
}

// ================= SidetoneSettings =================
void SidetoneSettings::load(QSettings &s, const QString &p)
{
    enabled = s.value(p + "enabled", false).toBool();
    volume = s.value(p + "volume", 30).toInt();
}

void SidetoneSettings::save(QSettings &s, const QString &p) const
{
    s.setValue(p + "enabled", enabled);
    s.setValue(p + "volume", volume);
}

// ================= Generowanie konfiguracji =================
namespace dsp {

// Wtyczki LogiHub: paczka (bin/../lib/ladspa), katalog budowania (build/ladspa),
// instalacja ze źródeł (~/.local/lib/logihub/ladspa).
static QString pluginPath(const QString &file)
{
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &dir : {app + "/../lib/ladspa", app + "/ladspa", app + "/../ladspa",
                               QDir::homePath() + "/.local/lib/logihub/ladspa"}) {
        const QString p = QDir::cleanPath(dir + "/" + file);
        if (QFile::exists(p))
            return p;
    }
    return {};
}

QString dspPluginPath()
{
    return pluginPath("logihub_dsp.so");
}

QString rnnoisePath()
{
    return pluginPath("logihub_rnnoise.so");
}

bool webrtcAvailable()
{
    for (const char *p : {"/usr/lib64/spa-0.2/aec/libspa-aec-webrtc.so", "/usr/lib/spa-0.2/aec/libspa-aec-webrtc.so",
                          "/usr/lib/x86_64-linux-gnu/spa-0.2/aec/libspa-aec-webrtc.so"})
        if (QFile::exists(p))
            return true;
    return false;
}

QString voiceConf(const QString &id, const QString &description, const QString &micNode, const VoiceSettings &v)
{
    const QString base = "logihub." + id;
    QString c = kHeader;
    QString source = micNode;
    const QString fx = dspPluginPath();
    if (v.noiseReduction && !useRnnoise(v) && dsp::webrtcAvailable()) {
        c += QString(R"PW(    { name = libpipewire-module-echo-cancel
      args = {
        library.name = aec/libspa-aec-webrtc
        monitor.mode = true
        audio.channels = 1
        audio.position = [ MONO ]
        aec.args = {
            webrtc.noise_suppression = true
            webrtc.high_pass_filter = false
            webrtc.gain_control = false
            webrtc.extended_filter = false
            webrtc.delay_agnostic = true
        }
        capture.props = { node.name = "%1.ns.in" target.object = "%2" node.passive = true node.dont-reconnect = true }
        source.props  = { node.name = "%1.ns" node.description = "LogiHub (wewnętrzny etap redukcji szumów)" node.virtual = true }
      }
    }
)PW").arg(base, micNode);
        source = base + ".ns";
    }

    QStringList nodes, chain;
    auto add = [&](const QString &name, const QString &def, const QString &in, const QString &out) {
        nodes << def;
        chain << name + ":" + in << name + ":" + out;
    };
    if (v.noiseReduction && useRnnoise(v)) // redukcja szumów siecią neuronową (RNNoise)
        add("nr", QString(R"PW({ type = ladspa name = nr plugin = "%1" label = logihub_rnnoise control = { "VAD threshold (%)" = 0.0 } })PW")
                      .arg(rnnoisePath()), "In", "Out");
    if (v.highpass)
        add("hp", QString(R"PW({ type = builtin name = hp label = bq_highpass control = { "Freq" = %1 "Q" = 0.707 } })PW")
                      .arg(num(v.highpassHz)), "In", "Out");
    if (v.gate && !fx.isEmpty())
        add("gate", QString(R"PW({ type = ladspa name = gate plugin = "%1" label = logihub_gate control = { "Open (dB)" = %2 "Close (dB)" = %3 "Release (ms)" = 120.0 } })PW")
                        .arg(fx, num(v.gateOpenDb), num(qMax(-90, v.gateOpenDb - 10))), "In", "Out");
    if (v.deesser && !fx.isEmpty()) // dynamiczny: ścisza sybilanty tylko gdy się pojawią
        add("deess", QString(R"PW({ type = ladspa name = deess plugin = "%1" label = logihub_deesser control = { "Frequency (Hz)" = 5500.0 "Threshold (dB)" = -32.0 "Max reduction (dB)" = %2 } })PW")
                         .arg(fx, num(deessMaxReduction(v))), "In", "Out");
    add("bass", QString(R"PW({ type = builtin name = bass label = bq_lowshelf control = { "Freq" = 150.0 "Q" = 0.707 "Gain" = %1 } })PW")
                    .arg(num(v.bassDb)), "In", "Out");
    add("pres", QString(R"PW({ type = builtin name = pres label = bq_peaking control = { "Freq" = 3500.0 "Q" = 1.0 "Gain" = %1 } })PW")
                    .arg(num(v.presenceDb)), "In", "Out");
    add("air", QString(R"PW({ type = builtin name = air label = bq_highshelf control = { "Freq" = 10000.0 "Q" = 0.707 "Gain" = %1 } })PW")
                   .arg(num(v.airDb)), "In", "Out");
    if (v.compressor && !fx.isEmpty())
        add("comp", QString(R"PW({ type = ladspa name = comp plugin = "%1" label = logihub_compressor control = { "Threshold (dB)" = %2 "Ratio" = %3 "Attack (ms)" = 8.0 "Release (ms)" = 150.0 "Makeup (dB)" = 0.0 } })PW")
                        .arg(fx, num(compThresholdDb(v)), num(compRatio(v))), "In", "Out");
    add("gain", QString(R"PW({ type = builtin name = gain label = linear control = { "Mult" = %1 "Add" = 0.0 } })PW")
                    .arg(num(std::pow(10.0, v.outGainDb / 20.0))), "In", "Out");
    if (v.limiter && !fx.isEmpty()) // twardy sufit -1 dBFS
        add("lim", QString(R"PW({ type = ladspa name = lim plugin = "%1" label = logihub_limiter control = { "Ceiling (dB)" = -1.0 "Release (ms)" = 60.0 } })PW")
                       .arg(fx), "In", "Out");

    QStringList links;
    for (int i = 1; i + 1 < chain.size(); i += 2)
        links << QString(R"PW({ output = "%1" input = "%2" })PW").arg(chain[i], chain[i + 1]);

    c += QString(R"PW(    { name = libpipewire-module-filter-chain
      args = {
        node.description = %1
        media.name = %1
        audio.channels = 1
        audio.position = [ MONO ]
        filter.graph = {
          nodes = [
            %2
          ]
          links = [
            %3
          ]
        }
        capture.props = { node.name = "%4.in" target.object = "%5" node.passive = true }
        playback.props = { node.name = "%4" media.class = Audio/Source node.description = %1 }
      }
    }
]
)PW").arg(quoteDesc(description), nodes.join("\n            "), links.join("\n            "), base, source);
    return c;
}

QString eqConf(const QString &id, const QString &description, const QString &sinkNode, const EqSettings &e)
{
    const QString base = "logihub." + id;
    QStringList nodes, links;
    const auto &f = EqSettings::frequencies();
    for (int i = 0; i < 10; ++i) {
        const QString label = i == 0 ? "bq_lowshelf" : i == 9 ? "bq_highshelf" : "bq_peaking";
        nodes << QString(R"PW({ type = builtin name = eq%1 label = %2 control = { "Freq" = %3 "Q" = 1.0 "Gain" = %4 } })PW")
                     .arg(i).arg(label, num(f[i]), num(e.gains[i]));
        if (i > 0)
            links << QString(R"PW({ output = "eq%1:Out" input = "eq%2:In" })PW").arg(i - 1).arg(i);
    }
    return QString(kHeader) + QString(R"PW(    { name = libpipewire-module-filter-chain
      args = {
        node.description = %1
        media.name = %1
        audio.channels = 2
        audio.position = [ FL FR ]
        filter.graph = {
          nodes = [
            %2
          ]
          links = [
            %3
          ]
        }
        capture.props = { node.name = "%4" media.class = Audio/Sink node.description = %1 }
        playback.props = { node.name = "%4.out" target.object = "%5" node.passive = true stream.dont-remix = true }
      }
    }
]
)PW").arg(quoteDesc(description), nodes.join("\n            "), links.join("\n            "), base, sinkNode);
}

QString sidetoneConf(const QString &id, const QString &micNode, const QString &sinkNode, int volume)
{
    const QString base = "logihub." + id;
    const QString target = sinkNode.isEmpty() ? QString() : QString("target.object = \"%1\"").arg(sinkNode);
    return QString(kHeader) + QString(R"PW(    { name = libpipewire-module-filter-chain
      args = {
        node.description = "LogiHub sidetone"
        audio.channels = 1
        audio.position = [ MONO ]
        filter.graph = {
          nodes = [
            { type = builtin name = gain label = linear control = { "Mult" = %1 "Add" = 0.0 } }
          ]
        }
        capture.props = { node.name = "%2.in" target.object = "%3" node.passive = true node.latency = "128/48000" }
        playback.props = { node.name = "%2" %4 node.latency = "128/48000" media.role = "Communication" }
      }
    }
]
)PW").arg(num(volume / 100.0), base, micNode, target);
}

} // namespace dsp

// ================= DspRunner =================
DspRunner &DspRunner::instance()
{
    static DspRunner r;
    return r;
}

void DspRunner::start(const QString &id, const QString &conf)
{
    if (m_confs.value(id) == conf && running(id))
        return;
    stop(id);
    const QString dir = QDir::homePath() + "/.local/state/logihub";
    QDir().mkpath(dir);
    const QString path = dir + "/" + id + ".conf";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(conf.toUtf8());
    f.close();
    auto *p = new QProcess(this);
    // Proces efektu ginie razem z LogiHub (także po awarii / kill -9) - bez osieroconych łańcuchów.
    p->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    p->setStandardOutputFile(QProcess::nullDevice());
    p->start("pipewire", {"-c", path});
    m_procs.insert(id, p);
    m_confs.insert(id, conf);
}

void DspRunner::stop(const QString &id)
{
    if (QProcess *p = m_procs.take(id)) {
        p->terminate();
        if (!p->waitForFinished(1500))
            p->kill();
        p->deleteLater();
    }
    m_confs.remove(id);
}

int DspRunner::killStale()
{
    // Łańcuchy pozostawione przez wcześniejsze uruchomienia (np. starsza wersja zamknięta sygnałem).
    const QString stateDir = QDir::homePath() + "/.local/state/logihub/";
    int killed = 0;
    for (const QString &pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool num = false;
        const int n = pid.toInt(&num);
        if (!num || n == QCoreApplication::applicationPid())
            continue;
        QFile f("/proc/" + pid + "/cmdline");
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> args = f.readAll().split('\0');
        if (args.size() >= 3 && args[0].endsWith("pipewire") && args[1] == "-c"
            && QString::fromLocal8Bit(args[2]).startsWith(stateDir)) {
            if (::kill(n, SIGTERM) == 0)
                ++killed;
        }
    }
    return killed;
}

void DspRunner::stopAll()
{
    for (const QString &id : m_procs.keys())
        stop(id);
}

bool DspRunner::running(const QString &id) const
{
    QProcess *p = m_procs.value(id);
    return p && p->state() != QProcess::NotRunning;
}

void DspRunner::setControls(const QString &node, const QList<QPair<QString, double>> &controls)
{
    const int id = audio::nodeIdByName(node);
    if (id < 0)
        return;
    QStringList parts;
    for (const auto &c : controls)
        parts << QString("\"%1\" %2").arg(c.first, num(c.second));
    QProcess::startDetached("pw-cli", {"set-param", QString::number(id), "Props",
                                       QString("{ params = [ %1 ] }").arg(parts.join(' '))});
}

void DspRunner::makeDefault(const QString &node)
{
    QTimer::singleShot(1500, this, [node] {
        const int id = audio::nodeIdByName(node);
        if (id >= 0)
            QProcess::startDetached("wpctl", {"set-default", QString::number(id)});
    });
}
