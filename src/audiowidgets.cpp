#include "audiowidgets.h"
#include "audio.h"
#include "theme.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

QWidget *sliderRow(QSlider *s, const QString &suffix, QLabel **valueLabel)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto *val = new QLabel;
    val->setMinimumWidth(64);
    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto upd = [s, val, suffix] { val->setText(QString("%1%2").arg(s->value()).arg(suffix)); };
    QObject::connect(s, &QSlider::valueChanged, val, upd);
    upd();
    h->addWidget(s, 1);
    h->addWidget(val);
    if (valueLabel)
        *valueLabel = val;
    return w;
}

namespace {
QSlider *hslider(int min, int max, int value)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

// Po wyłączeniu efektu: domyślnym urządzeniem znowu jest sprzęt.
void restoreDefault(const QString &nodeName)
{
    const int id = audio::nodeIdByName(nodeName);
    if (id >= 0)
        QProcess::startDetached("wpctl", {"set-default", QString::number(id)});
}
}

// ======================= Presety =======================
PresetBar::PresetBar(const QString &group, const QList<QPair<QString, QString>> &builtins, QWidget *parent)
    : QWidget(parent), m_group(group), m_builtins(builtins)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    m_combo = new QComboBox;
    auto *save = new QPushButton(tr("Zapisz jako…"));
    m_del = new QPushButton(tr("Usuń"));
    h->addWidget(new QLabel(tr("Preset:")));
    h->addWidget(m_combo, 1);
    h->addWidget(save);
    h->addWidget(m_del);
    refill();

    connect(m_combo, &QComboBox::currentIndexChanged, this, [this] {
        const QString id = current();
        m_del->setEnabled(id.startsWith("u:"));
        if (id != "c")
            emit picked(id);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString cur = current();
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("Zapisz preset"), tr("Nazwa presetu:"), QLineEdit::Normal,
                                             cur.startsWith("u:") ? cur.mid(2) : QString(), &ok);
        name.replace('/', '-').replace('\\', '-');
        name = name.trimmed();
        if (!ok || name.isEmpty())
            return;
        QSettings s;
        s.beginGroup(m_group);
        const bool exists = s.childKeys().contains(name) || s.childGroups().contains(name);
        s.endGroup();
        if (exists && QMessageBox::question(this, tr("Zapisz preset"),
                                            tr("Preset „%1” już istnieje. Nadpisać?").arg(name)) != QMessageBox::Yes)
            return;
        emit saveRequested(name);
        refill();
        select("u:" + name);
    });
    connect(m_del, &QPushButton::clicked, this, [this] {
        const QString id = current();
        if (!id.startsWith("u:"))
            return;
        if (QMessageBox::question(this, tr("Usuń preset"), tr("Usunąć preset „%1”?").arg(id.mid(2)))
            != QMessageBox::Yes)
            return;
        QSettings().remove(m_group + "/" + id.mid(2));
        refill();
        select("c");
    });
}

void PresetBar::refill()
{
    QSignalBlocker b(m_combo);
    const QString cur = current();
    m_combo->clear();
    for (const auto &p : m_builtins)
        m_combo->addItem(p.second, p.first);
    QSettings s;
    s.beginGroup(m_group);
    QStringList users = s.childKeys() + s.childGroups();
    s.endGroup();
    users.removeDuplicates();
    users.sort(Qt::CaseInsensitive);
    if (!users.isEmpty())
        m_combo->insertSeparator(m_combo->count());
    for (const QString &u : users)
        m_combo->addItem("★ " + u, "u:" + u);
    m_combo->insertSeparator(m_combo->count());
    m_combo->addItem(tr("Własny (niezapisany)"), "c");
    const int idx = m_combo->findData(cur);
    m_combo->setCurrentIndex(idx >= 0 ? idx : m_combo->count() - 1);
    m_del->setEnabled(current().startsWith("u:"));
}

void PresetBar::select(const QString &id)
{
    QSignalBlocker b(m_combo);
    const int idx = m_combo->findData(id);
    m_combo->setCurrentIndex(idx >= 0 ? idx : m_combo->findData("c"));
    m_del->setEnabled(current().startsWith("u:"));
}

QString PresetBar::current() const
{
    return m_combo->currentData().toString();
}

// ======================= Voice FX (w stylu Blue VO!CE) =======================
VoiceEditor::VoiceEditor(const QString &devKey, const QString &id, const QString &description,
                         const QString &micNode, QWidget *parent)
    : QWidget(parent), m_key(devKey + "/voice/"), m_id(id), m_desc(description), m_mic(micNode)
{
    QSettings s;
    m_v.load(s, m_key);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, [this] { apply(false); });

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(14);

    m_enabled = new Toggle;
    QVBoxLayout *body;
    outer->addWidget(theme::card(
        "Voice FX",
        tr("Przetwarzanie głosu w czasie rzeczywistym, w stylu Blue VO!CE z G Hub (własne efekty LogiHub, "
           "nie algorytmy Logitecha). Tworzy mikrofon „%1”, który po włączeniu staje się domyślnym "
           "mikrofonem (Discord, gry, OBS).").arg(description),
        &body, m_enabled));
    QList<QPair<QString, QString>> builtins;
    const QStringList names = VoiceSettings::presetNames();
    for (int i = 1; i < names.size(); ++i) // 0 = „Własny” - obsługuje PresetBar
        builtins << qMakePair(QString("b:%1").arg(i), names[i]);
    m_presets = new PresetBar("voicePresets", builtins);
    body->addWidget(m_presets);
    auto *defRow = new QHBoxLayout;
    auto *def = new QPushButton(tr("Ustaw jako domyślny mikrofon"));
    defRow->addStretch();
    defRow->addWidget(def);
    body->addLayout(defRow);
    QStringList missing;
    if (dsp::dspPluginPath().isEmpty())
        missing << tr("bramka, de-esser, kompresor i limiter – brak wtyczki logihub_dsp.so (przeinstaluj LogiHub)");
    if (dsp::rnnoisePath().isEmpty() && !dsp::webrtcAvailable())
        missing << tr("redukcja szumów – brak RNNoise i modułu PipeWire aec-webrtc");
    if (!missing.isEmpty())
        body->addWidget(theme::hint(tr("⚠ Niedostępne w tym systemie: %1. Pozostałe efekty działają.")
                                        .arg(missing.join("; "))));

    m_modules = new QWidget;
    auto *grid = new QGridLayout(m_modules);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(14);
    outer->addWidget(m_modules);

    int cell = 0;
    // Moduł: karta z przełącznikiem i suwakami
    auto module = [&](const QString &title, const QString &sub, bool VoiceSettings::*flag) -> QVBoxLayout * {
        Toggle *t = nullptr;
        if (flag) {
            t = new Toggle;
            connect(t, &Toggle::toggled, this, [this, flag](bool on) {
                m_v.*flag = on;
                changed();
            });
            m_sync << [this, t, flag] { QSignalBlocker b(t); t->setChecked(m_v.*flag); };
        }
        QVBoxLayout *b;
        auto *c = theme::card(title, sub, &b, t);
        grid->addWidget(c, cell / 2, cell % 2);
        ++cell;
        return b;
    };
    auto slider = [&](QVBoxLayout *into, const QString &label, int min, int max, int VoiceSettings::*field,
                      const QString &suffix) {
        auto *s = hslider(min, max, m_v.*field);
        connect(s, &QSlider::valueChanged, this, [this, field](int v) {
            m_v.*field = v;
            changed();
        });
        m_sync << [this, s, field] { QSignalBlocker b(s); s->setValue(m_v.*field); };
        if (!label.isEmpty())
            into->addWidget(new QLabel(label));
        into->addWidget(sliderRow(s, suffix));
    };

    auto *b = module(tr("Filtr górnoprzepustowy"), tr("Usuwa dudnienie, stuki w biurko i szum wentylatorów."),
                     &VoiceSettings::highpass);
    slider(b, {}, 40, 250, &VoiceSettings::highpassHz, " Hz");
    b = module(tr("Redukcja szumów"), tr("Usuwa szum tła: wentylatory, klawiaturę, otoczenie."),
               &VoiceSettings::noiseReduction);
    {
        auto *nr = new QComboBox;
        if (!dsp::rnnoisePath().isEmpty())
            nr->addItem(tr("RNNoise – sieć neuronowa (najlepsza)"), 0);
        if (dsp::webrtcAvailable())
            nr->addItem(tr("WebRTC – także echo z głośników"), 1);
        b->addWidget(nr);
        connect(nr, &QComboBox::currentIndexChanged, this, [this, nr] {
            m_v.nrMode = nr->currentData().toInt();
            changed();
        });
        m_sync << [this, nr] {
            QSignalBlocker blk(nr);
            const int i = nr->findData(m_v.nrMode);
            nr->setCurrentIndex(i >= 0 ? i : 0);
        };
    }
    b->addStretch();
    b = module(tr("Bramka szumów"), tr("Wycisza mikrofon, gdy nie mówisz."), &VoiceSettings::gate);
    slider(b, tr("Próg otwarcia"), -70, -20, &VoiceSettings::gateOpenDb, " dB");
    b = module(tr("De-esser"), tr("Łagodzi syczące głoski „s”, „sz”, „c”."), &VoiceSettings::deesser);
    slider(b, tr("Redukcja"), 0, 10, &VoiceSettings::deessDb, " dB");
    b = module(tr("Kompresor"), tr("Wyrównuje głośność – cichy i głośny głos brzmią podobnie."),
               &VoiceSettings::compressor);
    slider(b, tr("Próg"), 0, 100, &VoiceSettings::compThreshold, "%");
    slider(b, tr("Siła"), 0, 100, &VoiceSettings::compStrength, "%");
    b = module(tr("Limiter"), tr("Chroni słuchaczy przed przesterowaniem przy krzyku."), &VoiceSettings::limiter);
    b->addStretch();
    b = module(tr("Barwa głosu (EQ)"), tr("Bas, czytelność i „powietrze”."), nullptr);
    slider(b, tr("Bas"), -10, 10, &VoiceSettings::bassDb, " dB");
    slider(b, tr("Obecność"), -10, 10, &VoiceSettings::presenceDb, " dB");
    slider(b, tr("Powietrze"), -10, 10, &VoiceSettings::airDb, " dB");
    b = module(tr("Wzmocnienie wyjścia"), tr("Końcowa głośność przetworzonego głosu."), nullptr);
    slider(b, {}, -6, 12, &VoiceSettings::outGainDb, " dB");
    b->addStretch();

    connect(m_enabled, &Toggle::toggled, this, [this](bool on) {
        m_v.enabled = on;
        QSettings s;
        m_v.save(s, m_key);
        m_modules->setEnabled(on);
        if (on) {
            apply(true);
            DspRunner::instance().makeDefault("logihub." + m_id);
        } else {
            DspRunner::instance().stop(m_id);
            restoreDefault(m_mic);
        }
    });
    connect(m_presets, &PresetBar::picked, this, [this](const QString &id) {
        const bool en = m_v.enabled;
        if (id.startsWith("b:")) {
            m_v = VoiceSettings::fromPreset(id.mid(2).toInt());
        } else {
            QSettings s;
            VoiceSettings v;
            v.load(s, "voicePresets/" + id.mid(2) + "/");
            m_v = v;
            m_v.preset = 0;
        }
        m_v.enabled = en;
        QSettings().setValue(m_key + "presetSel", id);
        syncUi();
        changed(true);
    });
    connect(m_presets, &PresetBar::saveRequested, this, [this](const QString &name) {
        QSettings s;
        m_v.save(s, "voicePresets/" + name + "/");
        s.setValue(m_key + "presetSel", "u:" + name);
    });
    connect(def, &QPushButton::clicked, this, [this] { DspRunner::instance().makeDefault("logihub." + m_id); });

    m_presets->select(s.value(m_key + "presetSel", m_v.preset > 0 ? QString("b:%1").arg(m_v.preset) : "c").toString());
    syncUi();
    if (m_v.enabled)
        apply(true);
}

void VoiceEditor::syncUi()
{
    {
        QSignalBlocker b1(m_enabled);
        m_enabled->setChecked(m_v.enabled);
    }
    m_modules->setEnabled(m_v.enabled);
    for (auto &f : m_sync)
        f();
}

void VoiceEditor::changed(bool presetPick)
{
    QSettings s;
    if (!presetPick && m_presets->current() != "c") {
        m_v.preset = 0;
        m_presets->select("c");
        s.setValue(m_key + "presetSel", "c");
    }
    m_v.save(s, m_key);
    if (!m_v.enabled)
        return;
    if (m_v.structureKey() != m_lastStructure)
        apply(true);
    else
        m_debounce->start();
}

void VoiceEditor::apply(bool structural)
{
    if (!m_v.enabled)
        return;
    if (structural || !DspRunner::instance().running(m_id)) {
        m_lastStructure = m_v.structureKey();
        DspRunner::instance().start(m_id, dsp::voiceConf(m_id, m_desc, m_mic, m_v));
    } else {
        // tylko parametry węzłów obecnych w łańcuchu (moduł mógł zostać pominięty z braku wtyczki)
        const QString conf = dsp::voiceConf(m_id, m_desc, m_mic, m_v);
        QList<QPair<QString, double>> ctl;
        for (const auto &c : m_v.controls())
            if (conf.contains("name = " + c.first.section(':', 0, 0) + " "))
                ctl << c;
        DspRunner::instance().setControls("logihub." + m_id + ".in", ctl);
    }
}

// ======================= Equalizer =======================
EqEditor::EqEditor(const QString &devKey, const QString &id, const QString &description, const QString &sinkNode,
                   QWidget *parent)
    : QWidget(parent), m_key(devKey + "/eq/"), m_id(id), m_desc(description), m_sink(sinkNode)
{
    QSettings s;
    m_e.load(s, m_key);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (m_e.enabled)
            DspRunner::instance().setControls("logihub." + m_id, m_e.controls());
    });

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_enabled = new Toggle;
    m_enabled->setChecked(m_e.enabled);
    QVBoxLayout *body;
    outer->addWidget(theme::card(tr("Equalizer"),
                                 tr("10-pasmowy korektor dźwięku słuchawek. Po włączeniu dźwięk systemu "
                                    "przechodzi przez wyjście „%1”.").arg(description),
                                 &body, m_enabled));
    QList<QPair<QString, QString>> builtins;
    const QStringList names = EqSettings::presetNames();
    for (int i = 0; i < names.size() - 1; ++i) // ostatni = „Własny” - obsługuje PresetBar
        builtins << qMakePair(QString("b:%1").arg(i), names[i]);
    m_presets = new PresetBar("eqPresets", builtins);
    body->addWidget(m_presets);

    auto *bands = new QWidget;
    auto *grid = new QGridLayout(bands);
    grid->setHorizontalSpacing(10);
    const auto &f = EqSettings::frequencies();
    for (int i = 0; i < 10; ++i) {
        auto *sl = new QSlider(Qt::Vertical);
        sl->setRange(-12, 12);
        sl->setValue(m_e.gains[i]);
        sl->setMinimumHeight(220);
        auto *val = new QLabel;
        val->setAlignment(Qt::AlignCenter);
        auto *freq = new QLabel(f[i] >= 1000 ? QString("%1k").arg(f[i] / 1000) : QString::number(f[i]));
        freq->setAlignment(Qt::AlignCenter);
        freq->setObjectName("hint");
        auto upd = [val, sl] { val->setText(QString("%1%2").arg(sl->value() > 0 ? "+" : "").arg(sl->value())); };
        upd();
        grid->addWidget(val, 0, i, Qt::AlignHCenter);
        grid->addWidget(sl, 1, i, Qt::AlignHCenter);
        grid->addWidget(freq, 2, i, Qt::AlignHCenter);
        connect(sl, &QSlider::valueChanged, this, [this, i, upd](int v) {
            upd();
            m_e.gains[i] = v;
            QSettings s;
            if (m_presets->current() != "c") {
                m_presets->select("c");
                m_e.preset = 6;
                s.setValue(m_key + "presetSel", "c");
            }
            m_e.save(s, m_key);
            m_debounce->start();
        });
        m_sliders << sl;
        m_values << val;
    }
    body->addWidget(bands);
    body->addWidget(theme::hint(tr("dB, zakres ±12. Hz: 32 · 64 · 125 · 250 · 500 · 1k · 2k · 4k · 8k · 16k")));
    bands->setEnabled(m_e.enabled);

    connect(m_presets, &PresetBar::picked, this, [this](const QString &id) {
        QSettings s;
        s.setValue(m_key + "presetSel", id);
        if (id.startsWith("b:")) {
            m_e.preset = id.mid(2).toInt();
            setGains(EqSettings::presetGains(m_e.preset));
        } else {
            const QStringList l = s.value("eqPresets/" + id.mid(2)).toStringList();
            std::array<int, 10> g{};
            for (int i = 0; i < 10 && i < l.size(); ++i)
                g[i] = qBound(-12, l[i].toInt(), 12);
            m_e.preset = 6;
            setGains(g);
        }
    });
    connect(m_presets, &PresetBar::saveRequested, this, [this](const QString &name) {
        QStringList l;
        for (int g : m_e.gains)
            l << QString::number(g);
        QSettings s;
        s.setValue("eqPresets/" + name, l);
        s.setValue(m_key + "presetSel", "u:" + name);
    });
    m_presets->select(s.value(m_key + "presetSel", m_e.preset < 6 ? QString("b:%1").arg(m_e.preset) : "c").toString());
    connect(m_enabled, &Toggle::toggled, this, [this, bands](bool on) {
        m_e.enabled = on;
        QSettings s;
        m_e.save(s, m_key);
        bands->setEnabled(on);
        if (on) {
            apply();
            DspRunner::instance().makeDefault("logihub." + m_id);
        } else {
            DspRunner::instance().stop(m_id);
            restoreDefault(m_sink);
        }
    });
    if (m_e.enabled)
        apply();
}

void EqEditor::setGains(const std::array<int, 10> &g)
{
    for (int i = 0; i < 10; ++i) {
        QSignalBlocker b(m_sliders[i]);
        m_sliders[i]->setValue(g[i]);
        m_values[i]->setText(QString("%1%2").arg(g[i] > 0 ? "+" : "").arg(g[i]));
    }
    m_e.gains = g;
    QSettings s;
    m_e.save(s, m_key);
    m_debounce->start();
}

void EqEditor::apply()
{
    DspRunner::instance().start(m_id, dsp::eqConf(m_id, m_desc, m_sink, m_e));
}

// ======================= Sidetone / odsłuch =======================
SidetoneCard::SidetoneCard(const QString &devKey, const QString &id, const QString &micNode,
                           const QString &sinkNode, const QString &title, const QString &subtitle, QWidget *parent)
    : QWidget(parent), m_key(devKey + "/sidetone/"), m_id(id), m_mic(micNode), m_sink(sinkNode)
{
    QSettings s;
    m_s.load(s, m_key);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *t = new Toggle;
    t->setChecked(m_s.enabled);
    QVBoxLayout *body;
    outer->addWidget(theme::card(title, subtitle, &body, t));
    auto *sl = hslider(0, 100, m_s.volume);
    sl->setEnabled(m_s.enabled);
    body->addWidget(sliderRow(sl, "%"));
    connect(t, &Toggle::toggled, this, [this, sl](bool on) {
        m_s.enabled = on;
        sl->setEnabled(on);
        QSettings s;
        m_s.save(s, m_key);
        if (on)
            apply();
        else
            DspRunner::instance().stop(m_id);
    });
    connect(sl, &QSlider::valueChanged, this, [this](int v) {
        m_s.volume = v;
        QSettings s;
        m_s.save(s, m_key);
        if (m_s.enabled)
            DspRunner::instance().setControls("logihub." + m_id + ".in", {qMakePair(QString("gain:Mult"), v / 100.0)});
    });
    if (m_s.enabled)
        apply();
}

void SidetoneCard::apply()
{
    DspRunner::instance().start(m_id, dsp::sidetoneConf(m_id, m_mic, m_sink, m_s.volume));
}

// ======================= Głośność / wzmocnienie =======================
QWidget *volumeCard(const QString &keyword, bool sinks, bool sources, const QString &title)
{
    QList<AudioNode> nodes;
    for (const AudioNode &n : audio::findNodes(keyword))
        if ((n.isSink && sinks) || (!n.isSink && sources))
            nodes << n;
    return volumeCardFor(nodes, title);
}

QWidget *volumeCardFor(const QList<AudioNode> &nodes, const QString &title)
{
    if (nodes.isEmpty())
        return nullptr;
    QVBoxLayout *body;
    auto *card = theme::card(title, {}, &body);
    for (const AudioNode &n : nodes) {
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(n.isSink ? QObject::tr("Wyjście") : QObject::tr("Mikrofon"));
        name->setMinimumWidth(80);
        bool muted = false;
        auto *sl = hslider(0, 100, qMax(0, audio::volume(n.id, &muted)));
        auto *mute = new Toggle;
        mute->setChecked(!muted);
        mute->setToolTip(QObject::tr("Wł./wył. (wyciszenie)"));
        const int id = n.id;
        QObject::connect(sl, &QSlider::valueChanged, sl, [id](int v) { audio::setVolume(id, v); });
        QObject::connect(mute, &Toggle::toggled, mute, [id](bool on) { audio::setMuted(id, !on); });
        row->addWidget(name);
        row->addWidget(sliderRow(sl, "%"), 1);
        row->addWidget(mute);
        body->addLayout(row);
    }
    return card;
}

QWidget *hwGainCard(const QString &keyword)
{
    const audio::HwGain g = audio::micHwGain(keyword);
    if (g.card.isEmpty() || g.value < 0 || g.max <= 0)
        return nullptr;
    QVBoxLayout *body;
    auto *card = theme::card(QObject::tr("Wzmocnienie mikrofonu (sprzętowe)"),
                             QObject::tr("Czułość przetwornika w samym mikrofonie – ustaw tak, by głośny głos "
                                         "nie przesterowywał."),
                             &body);
    auto *sl = hslider(0, g.max, g.value);
    const QString cardId = g.card;
    QObject::connect(sl, &QSlider::valueChanged, sl, [cardId](int v) { audio::setMicHwGain(cardId, v); });
    body->addWidget(sliderRow(sl, QString(" / %1").arg(g.max)));
    return card;
}
