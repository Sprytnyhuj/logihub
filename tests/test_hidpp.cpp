#include "dsp.h"
#include "hidpp.h"

#include <QFile>
#include <QRegularExpression>
#include <QVector>
#include <cmath>
#include <dlfcn.h>
#include <ladspa.h>
#include <QTest>

using namespace hidpp;

// Ładuje wtyczkę LADSPA LogiHub i przepuszcza przez nią sygnał (48 kHz, mono).
struct Plugin {
    void *lib = nullptr;
    const LADSPA_Descriptor *d = nullptr;
    Plugin(const char *file, const char *label)
    {
        lib = dlopen(QByteArray(LADSPA_DIR "/").append(file).constData(), RTLD_NOW);
        if (!lib)
            return;
        auto fn = reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(lib, "ladspa_descriptor"));
        for (unsigned long i = 0; fn && fn(i); ++i)
            if (qstrcmp(fn(i)->Label, label) == 0)
                d = fn(i);
    }
    ~Plugin()
    {
        if (lib)
            dlclose(lib);
    }
    bool ok() const { return d != nullptr; }
    QVector<float> run(QList<float> controls, const QVector<float> &in) const
    {
        LADSPA_Handle h = d->instantiate(d, 48000);
        QVector<float> out(in.size());
        QVector<float> inCopy = in;
        d->connect_port(h, 0, inCopy.data());
        d->connect_port(h, 1, out.data());
        for (int i = 0; i < controls.size(); ++i)
            d->connect_port(h, 2 + i, &controls[i]);
        if (d->activate)
            d->activate(h);
        for (int pos = 0; pos < in.size(); pos += 256) { // jak PipeWire - w blokach
            const int n = qMin(256, int(in.size()) - pos);
            d->connect_port(h, 0, inCopy.data() + pos);
            d->connect_port(h, 1, out.data() + pos);
            d->run(h, n);
        }
        d->cleanup(h);
        return out;
    }
};

static QVector<float> sine(float hz, float dbfs, int n = 48000)
{
    QVector<float> v(n);
    const float a = std::pow(10.0f, dbfs / 20.0f);
    for (int i = 0; i < n; ++i)
        v[i] = a * std::sin(2.0f * float(M_PI) * hz * i / 48000.0f);
    return v;
}

static QVector<float> noise(float dbfs, int n = 96000)
{
    QVector<float> v(n);
    const float a = std::pow(10.0f, dbfs / 20.0f) * 1.732f; // RMS = a/sqrt(3)
    quint32 s = 12345;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        v[i] = a * (float(s >> 8) / float(1 << 24) * 2.0f - 1.0f);
    }
    return v;
}

static float rmsDb(const QVector<float> &v, int from)
{
    double s = 0;
    for (int i = from; i < v.size(); ++i)
        s += double(v[i]) * v[i];
    return float(10.0 * std::log10(s / qMax(1, int(v.size()) - from) + 1e-20));
}

// Zmiana poziomu (dB) po ustabilizowaniu (pomija pierwsze 0,5 s)
static float gainDb(const Plugin &p, const QList<float> &ctl, const QVector<float> &in)
{
    const int skip = int(in.size()) / 2;
    return rmsDb(p.run(ctl, in), skip) - rmsDb(in, skip);
}

class TestHidpp : public QObject {
    Q_OBJECT
    // Prawdziwy sektor profilu 1 odczytany z G502 X PLUS (zapisany wcześniej przez G Hub).
    QByteArray sector()
    {
        QFile f(FIXTURE_DIR "/g502xplus-profile1.hex");
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QByteArray::fromHex(f.readAll().trimmed());
    }
private slots:
    void crcMatchesRealProfile()
    {
        Profile p{sector()};
        QCOMPARE(p.raw.size(), 255);
        QVERIFY(p.crcValid());
    }
    void parsesRealProfile()
    {
        Profile p{sector()};
        QCOMPARE(p.reportRateMs(), 1);
        QCOMPARE(p.defaultDpiIndex(), 2);
        QCOMPARE(p.dpi(0), 800);
        QCOMPARE(p.dpi(2), 1600);
        QCOMPARE(p.dpi(4), 3200);
        QCOMPARE(p.button(0).kind, ButtonAction::Mouse);
        QCOMPARE(p.button(0).value, quint16(1));
        QCOMPARE(p.button(4).kind, ButtonAction::Function);
        QCOMPARE(p.button(4).value, quint16(FnShiftDpi));
        QCOMPARE(p.button(9).value, quint16(FnNextDpi));
    }
    void editKeepsOtherBytesAndFixesCrc()
    {
        Profile p{sector()};
        const QByteArray orig = p.raw;
        p.setDpi(1, 1000);
        ButtonAction k;
        k.kind = ButtonAction::Key;
        k.value = 0x04; // A
        k.modifiers = 0x01;
        p.setButton(5, k);
        p.fixCrc();
        QVERIFY(p.crcValid());
        QCOMPARE(p.dpi(1), 1000);
        QCOMPARE(p.button(5).toBytes(), QByteArray::fromHex("80020104"));
        // zmienione mogą być tylko bajty DPI etapu 2 (3..4... -> 5,6) i przycisku 6 (52..55)
        for (int i = 0; i < orig.size() - 2; ++i) {
            const bool allowed = (i == 5 || i == 6) || (i >= 52 && i <= 55);
            if (!allowed)
                QCOMPARE(p.raw[i], orig[i]);
        }
    }
    void functionEncodingMatchesGHub()
    {
        const QByteArray b = QByteArray::fromHex("90070000");
        QCOMPARE(ButtonAction::fromBytes(b).toBytes(), b);
    }
    void dpiList()
    {
        const auto l = parseDpiList(QByteArray::fromHex("0064e0326400000000"));
        QCOMPARE(l.first(), 100);
        QCOMPARE(l.last(), 25600);
        QCOMPARE(l[1], 150);
        QCOMPARE(l.size(), (25600 - 100) / 50 + 1);
    }
    void keyRoundTrip()
    {
        for (int k : {int(Qt::Key_A), int(Qt::Key_Z), int(Qt::Key_0), int(Qt::Key_F5), int(Qt::Key_Space),
                      int(Qt::Key_Left)})
            QCOMPARE(hidToQtKey(qtKeyToHid(k)), k);
    }
    void dspConfigsAreWellFormed()
    {
        QList<QPair<QString, QString>> confs;
        for (int p = 0; p < VoiceSettings::presetNames().size(); ++p) {
            VoiceSettings v = VoiceSettings::fromPreset(p);
            confs << qMakePair(QString("voice%1").arg(p),
                               dsp::voiceConf("t-voice", "Test – Voice FX", "alsa_input.test", v));
        }
        VoiceSettings bare;
        bare.noiseReduction = bare.highpass = bare.gate = bare.deesser = bare.compressor = bare.limiter = false;
        confs << qMakePair(QString("voicebare"), dsp::voiceConf("t-voice", "Test", "alsa_input.test", bare));
        EqSettings e;
        e.gains = EqSettings::presetGains(2);
        confs << qMakePair(QString("eq"), dsp::eqConf("t-eq", "Test – Equalizer", "alsa_output.test", e));
        confs << qMakePair(QString("sidetone"), dsp::sidetoneConf("t-side", "alsa_input.test", "alsa_output.test", 30));
        const QString out = qEnvironmentVariable("LOGIHUB_DSP_OUT");
        for (const auto &c : confs) {
            QCOMPARE(c.second.count('{'), c.second.count('}'));
            QCOMPARE(c.second.count('['), c.second.count(']'));
            QVERIFY2(!c.second.contains(QRegularExpression("%\\d")), qPrintable(c.first));
            if (!out.isEmpty()) {
                QFile f(out + "/" + c.first + ".conf");
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write(c.second.toUtf8());
            }
        }
        // parametry zmieniane na żywo muszą istnieć w grafie
        const QString v = dsp::voiceConf("x", "x", "m", VoiceSettings::fromPreset(1));
        for (const auto &c : VoiceSettings::fromPreset(1).controls())
            QVERIFY2(v.contains("name = " + c.first.section(':', 0, 0) + " "), qPrintable(c.first));
    }
    // ---- Wtyczki Voice FX: zachowanie na sygnałach testowych ----
    void deesserCutsOnlySibilants()
    {
        Plugin p("logihub_dsp.so", "logihub_deesser");
        QVERIFY(p.ok());
        const QList<float> ctl = {5500, -32, 12};
        const float voice = gainDb(p, ctl, sine(1000, -10));
        const float hiss = gainDb(p, ctl, sine(8000, -10));
        QVERIFY2(qAbs(voice) < 0.5f, qPrintable(QString("1 kHz zmienione o %1 dB").arg(voice)));
        QVERIFY2(hiss < -6.0f, qPrintable(QString("8 kHz ściszone tylko o %1 dB").arg(hiss)));
        // cichy sybilant (poniżej progu) nietknięty
        const float quiet = gainDb(p, ctl, sine(8000, -45));
        QVERIFY2(qAbs(quiet) < 0.5f, qPrintable(QString("cichy 8 kHz zmieniony o %1 dB").arg(quiet)));
    }
    void gateSilencesNoiseKeepsVoice()
    {
        Plugin p("logihub_dsp.so", "logihub_gate");
        QVERIFY(p.ok());
        const QList<float> ctl = {-45, -55, 80};
        QVERIFY(gainDb(p, ctl, noise(-70)) < -40.0f);
        QVERIFY(qAbs(gainDb(p, ctl, sine(300, -20))) < 0.5f);
    }
    void compressorFollowsRatio()
    {
        Plugin p("logihub_dsp.so", "logihub_compressor");
        QVERIFY(p.ok());
        // amplituda -5 dBFS, próg -20, 4:1 -> nadwyżka 15 dB zmniejszona do 3.75 => wzmocnienie ~ -11.25 dB
        const float g = gainDb(p, {-20, 4, 5, 100, 0}, sine(500, -5));
        QVERIFY2(qAbs(g + 11.25f) < 1.5f, qPrintable(QString("wzmocnienie %1 dB").arg(g)));
        QVERIFY(qAbs(gainDb(p, {-20, 4, 5, 100, 0}, sine(500, -40))) < 0.5f); // pod progiem bez zmian
    }
    void limiterNeverExceedsCeiling()
    {
        Plugin p("logihub_dsp.so", "logihub_limiter");
        QVERIFY(p.ok());
        const auto out = p.run({-1, 60}, sine(200, +6));
        float peak = 0;
        for (float v : out)
            peak = qMax(peak, qAbs(v));
        QVERIFY2(peak <= std::pow(10.0f, -1.0f / 20.0f) + 1e-4f, qPrintable(QString("szczyt %1").arg(peak)));
    }
    void rnnoiseRemovesNoise()
    {
        Plugin p("logihub_rnnoise.so", "logihub_rnnoise");
        if (!p.ok())
            QSKIP("RNNoise nie zbudowany");
        const float g = gainDb(p, {0}, noise(-30));
        QVERIFY2(g < -10.0f, qPrintable(QString("szum stłumiony tylko o %1 dB").arg(g)));
    }
    void profileName()
    {
        Profile p{sector()};
        QCOMPARE(p.name(), QString());
        p.setName("FPS");
        QCOMPARE(p.name(), QString("FPS"));
        p.setName({});
        QCOMPARE(p.raw.mid(160, 48), QByteArray(48, char(0xff)));
    }
    void effectEncoding()
    {
        Effect e;
        e.id = Effect::Static;
        e.rgb = 0x1020ff;
        QCOMPARE(e.toBytes().toHex(), QByteArray("011020ff00000000000000"));
        Effect b;
        b.id = Effect::Breathe;
        b.rgb = 0xff0000;
        b.periodMs = 3000;
        const Effect back = Effect::fromBytes(b.toBytes());
        QCOMPARE(back.rgb, quint32(0xff0000));
        QCOMPARE(back.periodMs, 3000);
        // nieznany efekt (np. 0x0f zapisany przez G Hub) zostaje bajt w bajt
        const QByteArray raw = QByteArray::fromHex("0f00000000000000640000");
        QCOMPARE(Effect::fromBytes(raw).toBytes(), raw);
    }
};

QTEST_GUILESS_MAIN(TestHidpp)
#include "test_hidpp.moc"
