#include "devicepage.h"
#include "audio.h"
#include "audiowidgets.h"
#include "theme.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using hidpp::ButtonAction;
using hidpp::Effect;

namespace {

QByteArray bytes(std::initializer_list<int> l)
{
    QByteArray b;
    for (int v : l)
        b.append(char(v));
    return b;
}

QString effectName(quint8 id)
{
    switch (id) {
    case Effect::Off: return QObject::tr("Wyłączone");
    case Effect::Static: return QObject::tr("Stały kolor");
    case Effect::Breathe: return QObject::tr("Oddychanie");
    case Effect::Cycle:
    case Effect::CycleSat: return QObject::tr("Cykl kolorów");
    case Effect::Wave: return QObject::tr("Fala");
    default: return QObject::tr("Efekt z G Hub (bez zmian)");
    }
}

QString zoneName(int location, int index)
{
    switch (location) {
    case 1: return QObject::tr("Strefa główna");
    case 2: return QObject::tr("Logo");
    case 3: return QObject::tr("Lewy bok");
    case 4: return QObject::tr("Prawy bok");
    default: return QObject::tr("Strefa %1").arg(index + 1);
    }
}

// Domyślne nazwy przycisków G502 X / G502 X PLUS w kolejności z profilu.
QStringList buttonNames(const QString &device, int count)
{
    QStringList names;
    if (device.startsWith("G502")) {
        names = {QObject::tr("Lewy przycisk"), QObject::tr("Prawy przycisk"), QObject::tr("Kółko (środkowy)"),
                 QObject::tr("G4 · boczny tylny"), QObject::tr("G6 · snajperski"), QObject::tr("G5 · boczny przedni"),
                 QObject::tr("Kółko w lewo"), QObject::tr("Kółko w prawo"), QObject::tr("G9 · za kółkiem"),
                 QObject::tr("G8 · DPI w górę"), QObject::tr("G7 · DPI w dół")};
    }
    for (int i = names.size(); i < count; ++i)
        names << QObject::tr("Przycisk %1").arg(i + 1);
    return names.mid(0, count);
}

void fillActionCombo(QComboBox *c, bool gshift)
{
    auto add = [c](const QString &text, ButtonAction::Kind k, quint16 v) {
        ButtonAction a;
        a.kind = k;
        a.value = v;
        c->addItem(text, a.toBytes());
    };
    if (gshift)
        c->addItem(QObject::tr("Nieprzypisany"), QByteArray(4, char(0xff)));
    add(QObject::tr("Lewy klik"), ButtonAction::Mouse, 0x0001);
    add(QObject::tr("Prawy klik"), ButtonAction::Mouse, 0x0002);
    add(QObject::tr("Środkowy klik"), ButtonAction::Mouse, 0x0004);
    add(QObject::tr("Wstecz"), ButtonAction::Mouse, 0x0008);
    add(QObject::tr("Dalej"), ButtonAction::Mouse, 0x0010);
    c->insertSeparator(c->count());
    add(QObject::tr("Przewijanie w lewo"), ButtonAction::Function, hidpp::FnTiltLeft);
    add(QObject::tr("Przewijanie w prawo"), ButtonAction::Function, hidpp::FnTiltRight);
    add(QObject::tr("DPI w górę"), ButtonAction::Function, hidpp::FnNextDpi);
    add(QObject::tr("DPI w dół"), ButtonAction::Function, hidpp::FnPrevDpi);
    add(QObject::tr("DPI – cykl"), ButtonAction::Function, hidpp::FnCycleDpi);
    add(QObject::tr("DPI domyślne"), ButtonAction::Function, hidpp::FnDefaultDpi);
    add(QObject::tr("DPI Shift (snajperski)"), ButtonAction::Function, hidpp::FnShiftDpi);
    add(QObject::tr("Następny profil"), ButtonAction::Function, hidpp::FnNextProfile);
    add(QObject::tr("Poprzedni profil"), ButtonAction::Function, hidpp::FnPrevProfile);
    add(QObject::tr("Cykl profili"), ButtonAction::Function, hidpp::FnCycleProfile);
    add(QObject::tr("G-Shift (przytrzymaj)"), ButtonAction::Function, hidpp::FnGShift);
    add(QObject::tr("Pokaż stan baterii"), ButtonAction::Function, hidpp::FnBattery);
    c->insertSeparator(c->count());
    add(QObject::tr("Odtwórz / pauza"), ButtonAction::Consumer, 0x00cd);
    add(QObject::tr("Następny utwór"), ButtonAction::Consumer, 0x00b5);
    add(QObject::tr("Poprzedni utwór"), ButtonAction::Consumer, 0x00b6);
    add(QObject::tr("Głośniej"), ButtonAction::Consumer, 0x00e9);
    add(QObject::tr("Ciszej"), ButtonAction::Consumer, 0x00ea);
    add(QObject::tr("Wycisz dźwięk"), ButtonAction::Consumer, 0x00e2);
    c->insertSeparator(c->count());
    c->addItem(QObject::tr("Klawisz / skrót…"), QByteArray("key"));
    add(QObject::tr("Wyłączony"), ButtonAction::Disabled, 0);
}

QKeySequence actionToSequence(const ButtonAction &a)
{
    const int qk = hidpp::hidToQtKey(a.value);
    if (!qk)
        return {};
    Qt::KeyboardModifiers m;
    if (a.modifiers & 0x11) m |= Qt::ControlModifier;
    if (a.modifiers & 0x22) m |= Qt::ShiftModifier;
    if (a.modifiers & 0x44) m |= Qt::AltModifier;
    if (a.modifiers & 0x88) m |= Qt::MetaModifier;
    return QKeySequence(QKeyCombination(m, Qt::Key(qk)));
}

std::optional<ButtonAction> sequenceToAction(const QKeySequence &s)
{
    if (s.isEmpty())
        return std::nullopt;
    const QKeyCombination kc = s[0];
    const int hid = hidpp::qtKeyToHid(kc.key());
    if (!hid)
        return std::nullopt;
    ButtonAction a;
    a.kind = ButtonAction::Key;
    a.value = quint16(hid);
    const auto m = kc.keyboardModifiers();
    if (m & Qt::ControlModifier) a.modifiers |= 0x01;
    if (m & Qt::ShiftModifier) a.modifiers |= 0x02;
    if (m & Qt::AltModifier) a.modifiers |= 0x04;
    if (m & Qt::MetaModifier) a.modifiers |= 0x08;
    return a;
}

QString nodeFor(const QString &keyword, bool sink)
{
    for (const AudioNode &n : audio::findNodes(keyword))
        if (n.isSink == sink)
            return n.name;
    return {};
}

const quint32 kSwatches[] = {0x00b4ff, 0x2f6bff, 0x8a3dff, 0xff2fd0, 0xff2f3d, 0xff8a00, 0xffe600, 0x3ddc84, 0xffffff};

} // namespace

// ===================================================================
EffectEditor::EffectEditor(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);
    auto *top = new QHBoxLayout;
    m_combo = new QComboBox;
    top->addWidget(new QLabel(tr("Efekt:")));
    top->addWidget(m_combo, 1);
    lay->addLayout(top);

    m_colorRow = new QWidget;
    auto *cr = new QHBoxLayout(m_colorRow);
    cr->setContentsMargins(0, 0, 0, 0);
    cr->setSpacing(8);
    for (quint32 c : kSwatches) {
        auto *b = new QPushButton;
        b->setFixedSize(30, 30);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("rgb", c);
        connect(b, &QPushButton::clicked, this, [this, c] {
            setColor(c);
            emit changed();
        });
        m_swatches << b;
        cr->addWidget(b);
    }
    m_custom = new QPushButton(tr("Własny…"));
    connect(m_custom, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor::fromRgb(m_rgb), this, tr("Kolor podświetlenia"));
        if (!c.isValid())
            return;
        setColor(c.rgb() & 0xffffff);
        emit changed();
    });
    cr->addWidget(m_custom);
    cr->addStretch();
    lay->addWidget(m_colorRow);

    m_speedRow = new QWidget;
    auto *sr = new QHBoxLayout(m_speedRow);
    sr->setContentsMargins(0, 0, 0, 0);
    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setRange(1, 20); // okres w sekundach
    m_speed->setInvertedAppearance(true);
    m_speed->setInvertedControls(true);
    sr->addWidget(new QLabel(tr("Szybkość:")));
    sr->addWidget(new QLabel(tr("wolno")));
    sr->addWidget(m_speed, 1);
    sr->addWidget(new QLabel(tr("szybko")));
    lay->addWidget(m_speedRow);

    connect(m_combo, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        emit changed();
    });
    connect(m_speed, &QSlider::sliderReleased, this, &EffectEditor::changed);
    connect(m_speed, &QSlider::valueChanged, this, [this] {
        if (!m_speed->isSliderDown())
            emit changed();
    });
    setColor(m_rgb);
}

void EffectEditor::setColor(quint32 rgb)
{
    m_rgb = rgb;
    for (QPushButton *b : m_swatches) {
        const quint32 c = b->property("rgb").toUInt();
        const bool sel = c == rgb;
        b->setStyleSheet(QString("QPushButton { background: %1; border-radius: 15px; border: %2; padding: 0; }")
                             .arg(QColor::fromRgb(c).name(), sel ? "3px solid #ffffff" : "1px solid #2c3343"));
    }
    const bool custom = std::find(std::begin(kSwatches), std::end(kSwatches), rgb) == std::end(kSwatches);
    m_custom->setStyleSheet(custom ? QString("QPushButton { border: 2px solid %1; }").arg(QColor::fromRgb(rgb).name())
                                   : QString());
}

void EffectEditor::setAvailable(const QList<quint8> &ids, bool keepUnknown)
{
    QSignalBlocker b(m_combo);
    m_combo->clear();
    for (quint8 id : ids)
        if (Effect::known(id) && m_combo->findData(id) < 0)
            m_combo->addItem(effectName(id), id);
    if (keepUnknown)
        m_combo->addItem(effectName(0xff), 0xff);
    updateVisibility();
}

void EffectEditor::setEffect(const Effect &e)
{
    QSignalBlocker b1(m_combo), b2(m_speed);
    m_original = e;
    int idx = m_combo->findData(e.id);
    if (idx < 0)
        idx = m_combo->findData(0xff);
    m_combo->setCurrentIndex(qMax(0, idx));
    if (e.id == Effect::Static || e.id == Effect::Breathe)
        setColor(e.rgb);
    m_speed->setValue(qBound(1, e.periodMs / 1000, 20));
    updateVisibility();
}

Effect EffectEditor::effect() const
{
    const int id = m_combo->currentData().toInt();
    if (id == 0xff)
        return m_original;
    Effect e;
    e.id = quint8(id);
    e.rgb = m_rgb;
    e.periodMs = m_speed->value() * 1000;
    e.intensity = 100;
    return e;
}

void EffectEditor::updateVisibility()
{
    const int id = m_combo->currentData().toInt();
    m_colorRow->setVisible(id == Effect::Static || id == Effect::Breathe);
    m_speedRow->setVisible(id == Effect::Breathe || id == Effect::Cycle || id == Effect::CycleSat || id == Effect::Wave);
}

// ===================================================================
DpiTrack::DpiTrack(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(86);
}

void DpiTrack::setStages(const QList<int> &dpi, int def, int shift, int min, int max)
{
    m_dpi = dpi;
    m_def = def;
    m_shift = shift;
    m_min = qMax(1, min);
    m_max = qMax(m_min + 1, max);
    update();
}

double DpiTrack::pos(int dpi) const
{
    // skala logarytmiczna - 400..3200 nie zbija się w lewym rogu
    const double t = (std::log(double(qBound(m_min, dpi, m_max))) - std::log(double(m_min)))
                     / (std::log(double(m_max)) - std::log(double(m_min)));
    return 24 + t * (width() - 48);
}

void DpiTrack::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int y = 44;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#262c3a"));
    p.drawRoundedRect(QRectF(24, y - 3, width() - 48, 6), 3, 3);
    QFont f = font();
    f.setPointSizeF(8.5);
    p.setFont(f);
    p.setPen(QColor("#5f6879"));
    for (int mark : {100, 400, 1600, 6400, 25600})
        if (mark >= m_min && mark <= m_max)
            p.drawText(QRectF(pos(mark) - 30, y + 18, 60, 16), Qt::AlignCenter, QString::number(mark));
    for (int i = 0; i < m_dpi.size(); ++i) {
        if (m_dpi[i] <= 0)
            continue;
        const double x = pos(m_dpi[i]);
        const bool def = i == m_def;
        p.setPen(i == m_shift ? QPen(QColor("#ff9f1a"), 3) : QPen(QColor("#0d0f13"), 2));
        p.setBrush(def ? QColor(theme::Accent) : QColor("#e8ebf2"));
        const double r = def ? 11 : 8;
        p.drawEllipse(QPointF(x, y), r, r);
        p.setPen(def ? QColor(theme::Accent) : QColor("#c9cfdb"));
        QFont bf = f;
        bf.setBold(true);
        bf.setPointSizeF(9);
        p.setFont(bf);
        p.drawText(QRectF(x - 40, y - 38, 80, 18), Qt::AlignCenter, QString::number(m_dpi[i]));
        p.setFont(f);
    }
}

// ===================================================================
MouseProfile::MouseProfile(HidppDevice *dev, const DeviceInfo &info, const onboard::Info &oi, QObject *parent)
    : QObject(parent), m_dev(dev), m_info(info), m_oi(oi)
{
    // ---------- Czułość ----------
    int dpiStep = 50;
    if (auto r = m_dev->call(feat::AdjustableDpi, 0x10, bytes({0}))) {
        const QList<int> l = hidpp::parseDpiList(r->mid(1));
        if (l.size() >= 2) {
            m_dpiMin = l.first();
            m_dpiMax = l.last();
            dpiStep = l[1] - l[0];
        }
    }
    QVBoxLayout *b;
    m_sensCard = theme::card(tr("Czułość (DPI)"),
                             tr("Etapy przełączane przyciskami G7/G8. Niebieski = domyślny, pomarańczowa obwódka "
                                "= DPI Shift (przytrzymanie G6). Zapisywane w pamięci myszy."),
                             &b);
    m_track = new DpiTrack;
    b->addWidget(m_track);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    auto *hdr = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setObjectName("hint");
        return l;
    }(tr("ETAP"));
    grid->addWidget(hdr, 0, 0);
    grid->addWidget(theme::hint(tr("DPI")), 0, 1);
    grid->addWidget(theme::hint(tr("DOMYŚLNY")), 0, 2, Qt::AlignHCenter);
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < 5; ++i) {
        auto *spin = new QSpinBox;
        spin->setRange(0, m_dpiMax);
        spin->setSingleStep(dpiStep);
        spin->setSpecialValueText(tr("wyłączony"));
        spin->setSuffix(" DPI");
        spin->setMinimumWidth(160);
        spin->setToolTip(tr("%1–%2 DPI, co %3; 0 = etap wyłączony").arg(m_dpiMin).arg(m_dpiMax).arg(dpiStep));
        auto *rb = new QRadioButton;
        group->addButton(rb, i);
        auto *num = new QLabel(QString::number(i + 1));
        num->setObjectName("chip");
        num->setAlignment(Qt::AlignCenter);
        num->setFixedWidth(34);
        grid->addWidget(num, i + 1, 0);
        grid->addWidget(spin, i + 1, 1);
        grid->addWidget(rb, i + 1, 2, Qt::AlignHCenter);
        connect(spin, &QSpinBox::valueChanged, this, [this] { markDirty(); });
        connect(rb, &QRadioButton::toggled, this, [this] { markDirty(); });
        m_dpi << spin;
        m_dpiDefault << rb;
    }
    grid->setColumnStretch(3, 1);
    b->addLayout(grid);
    auto *form = new QFormLayout;
    m_shift = new QComboBox;
    for (int i = 0; i < 5; ++i)
        m_shift->addItem(tr("Etap %1").arg(i + 1), i);
    form->addRow(tr("DPI Shift (snajperski):"), m_shift);
    m_rate = new QComboBox;
    int rateFlags = 0x8b;
    if (auto r = m_dev->call(feat::ReportRate, 0x00))
        rateFlags = quint8(r->at(0));
    for (int ms : {1, 2, 4, 8})
        if (rateFlags & (1 << (ms - 1)))
            m_rate->addItem(tr("%1 Hz").arg(1000 / ms), ms);
    form->addRow(tr("Częstotliwość raportowania:"), m_rate);
    b->addLayout(form);
    connect(m_shift, &QComboBox::currentIndexChanged, this, [this] { markDirty(); });
    connect(m_rate, &QComboBox::currentIndexChanged, this, [this] { markDirty(); });

    // ---------- Przyciski ----------
    auto *seg = new QWidget;
    auto *segL = new QHBoxLayout(seg);
    segL->setContentsMargins(0, 0, 0, 0);
    segL->setSpacing(0);
    auto *stdBtn = new QPushButton(tr("Standardowa"));
    auto *gBtn = new QPushButton(tr("G-Shift"));
    for (auto *x : {stdBtn, gBtn}) {
        x->setObjectName("seg");
        x->setCheckable(true);
        x->setMinimumWidth(120);
        segL->addWidget(x);
    }
    stdBtn->setStyleSheet("QPushButton#seg { border-top-left-radius: 9px; border-bottom-left-radius: 9px; }");
    gBtn->setStyleSheet("QPushButton#seg { border-top-right-radius: 9px; border-bottom-right-radius: 9px; }");
    stdBtn->setChecked(true);
    auto *segGroup = new QButtonGroup(this);
    segGroup->addButton(stdBtn, 0);
    segGroup->addButton(gBtn, 1);
    m_btnCard = theme::card(tr("Przypisania przycisków"),
                            tr("Warstwa G-Shift działa, gdy przytrzymasz przycisk z funkcją „G-Shift”."), &b, seg);
    auto *layers = new QStackedWidget;
    const QStringList names = buttonNames(m_info.name, m_oi.buttons);
    layers->addWidget(buttonLayer(false, names));
    layers->addWidget(buttonLayer(true, names));
    b->addWidget(layers);
    connect(segGroup, &QButtonGroup::idClicked, layers, &QStackedWidget::setCurrentIndex);

    // ---------- Podświetlenie ----------
    m_lightCard = theme::card(tr("Podświetlenie w profilu"),
                              tr("Zapisywane w myszy – widoczne w trybie pamięci wbudowanej. OpenRGB i tryb "
                                 "„z aplikacji” je nadpisują."),
                              &b);
    m_light = new EffectEditor;
    m_light->setAvailable({Effect::Off, Effect::Static, Effect::Breathe, Effect::Cycle}, true);
    connect(m_light, &EffectEditor::changed, this, [this] {
        m_lightDirty = true;
        markDirty();
    });
    b->addWidget(m_light);

    // ---------- Profil ----------
    m_profCard = theme::card(tr("Profile w pamięci"),
                             tr("Mysz ma %1 sloty profili. Przycisk „Cykl profili” przełącza między włączonymi.")
                                 .arg(m_oi.count),
                             &b);
    auto *pf = new QFormLayout;
    m_profileCombo = new QComboBox;
    auto *activateBtn = new QPushButton(tr("Ustaw jako aktywny"));
    auto *pr = new QHBoxLayout;
    pr->addWidget(m_profileCombo, 1);
    pr->addWidget(activateBtn);
    pf->addRow(tr("Edytowany profil:"), pr);
    m_name = new QLineEdit;
    m_name->setMaxLength(24);
    m_name->setPlaceholderText(tr("bez nazwy"));
    pf->addRow(tr("Nazwa profilu:"), m_name);
    b->addLayout(pf);
    auto *restore = new QPushButton(tr("Przywróć kopię zapasową…"));
    auto *rr = new QHBoxLayout;
    rr->addWidget(theme::hint(tr("Przed każdym zapisem robiona jest kopia do ~/.local/share/logihub/backups")), 1);
    rr->addWidget(restore);
    b->addLayout(rr);
    connect(m_name, &QLineEdit::textEdited, this, [this] { markDirty(); });

    // ---------- Pasek zapisu ----------
    m_saveBar = new QWidget;
    m_saveBar->setObjectName("saveBar");
    m_saveBar->setAttribute(Qt::WA_StyledBackground);
    auto *sb = new QHBoxLayout(m_saveBar);
    sb->setContentsMargins(24, 12, 24, 12);
    m_status = new QLabel;
    m_status->setObjectName("hint");
    m_status->setWordWrap(true);
    m_save = new QPushButton(tr("Zapisz do myszy"));
    m_save->setObjectName("primary");
    sb->addWidget(m_status, 1);
    sb->addWidget(m_save);

    connect(m_profileCombo, &QComboBox::currentIndexChanged, this,
            [this] { loadSector(m_profileCombo->currentData().toInt()); });
    connect(activateBtn, &QPushButton::clicked, this, [this] {
        if (onboard::mode(*m_dev) != onboard::Onboard) {
            m_status->setText(tr("Profil zostanie użyty po przełączeniu trybu na „Pamięć wbudowana”."));
            return;
        }
        const bool ok = onboard::activate(*m_dev, m_sector);
        m_status->setText(ok ? tr("Profil aktywny.") : tr("Nie udało się aktywować profilu."));
        reload();
    });
    connect(m_save, &QPushButton::clicked, this, &MouseProfile::save);
    connect(restore, &QPushButton::clicked, this, &MouseProfile::restoreBackup);

    reload();
}

QWidget *MouseProfile::buttonLayer(bool gshift, const QStringList &names)
{
    auto *w = new QWidget;
    auto *grid = new QGridLayout(w);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setVerticalSpacing(8);
    grid->setHorizontalSpacing(14);
    for (int i = 0; i < m_oi.buttons; ++i) {
        ButtonRow row;
        row.gshift = gshift;
        row.index = i;
        row.action = new QComboBox;
        row.action->setMinimumWidth(230);
        fillActionCombo(row.action, gshift);
        row.key = new QKeySequenceEdit;
        row.key->setMaximumSequenceLength(1);
        row.key->setClearButtonEnabled(true);
        row.key->setVisible(false);
        auto *lbl = new QLabel(names.value(i));
        lbl->setMinimumWidth(170);
        grid->addWidget(lbl, i, 0);
        grid->addWidget(row.action, i, 1);
        grid->addWidget(row.key, i, 2);
        QKeySequenceEdit *key = row.key;
        QComboBox *combo = row.action;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key] {
            key->setVisible(combo->currentData().toByteArray() == "key");
            markDirty();
        });
        connect(key, &QKeySequenceEdit::keySequenceChanged, this, [this] { markDirty(); });
        m_buttons << row;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    return w;
}

void MouseProfile::markDirty()
{
    updateTrack();
    if (m_loading)
        return;
    m_status->setText(tr("Niezapisane zmiany"));
}

void MouseProfile::updateTrack()
{
    QList<int> l;
    int def = 0;
    for (int i = 0; i < m_dpi.size(); ++i) {
        l << m_dpi[i]->value();
        if (m_dpiDefault[i]->isChecked())
            def = i;
    }
    m_track->setStages(l, def, m_shift->currentIndex(), m_dpiMin, m_dpiMax);
}

void MouseProfile::reload()
{
    const QList<onboard::Header> headers = onboard::headers(*m_dev);
    const int active = onboard::activeSector(*m_dev);
    QSignalBlocker b(m_profileCombo);
    const int previous = m_profileCombo->currentData().toInt();
    m_profileCombo->clear();
    for (const auto &h : headers) {
        if (!h.enabled)
            continue;
        QString label = tr("Profil %1").arg(h.sector & 0xff);
        if (h.sector >= 0x100)
            label += tr(" (fabryczny)");
        if (h.sector == active)
            label += tr("  ● aktywny");
        m_profileCombo->addItem(label, h.sector);
    }
    int idx = m_profileCombo->findData(m_sector >= 0 ? previous : active);
    if (idx < 0)
        idx = m_profileCombo->findData(active);
    m_profileCombo->setCurrentIndex(qMax(0, idx));
    if (m_profileCombo->count())
        loadSector(m_profileCombo->currentData().toInt());
    else
        m_status->setText(tr("Nie znaleziono profili w pamięci."));
}

void MouseProfile::loadSector(int sector)
{
    auto raw = onboard::readSector(*m_dev, sector, m_oi.size);
    if (!raw) {
        m_status->setText(tr("Nie udało się odczytać profilu – porusz myszą, żeby ją wybudzić."));
        m_save->setEnabled(false);
        return;
    }
    m_loading = true;
    m_sector = sector;
    m_profile.raw = *raw;
    const bool rom = sector >= 0x100 || sector >= m_oi.sectors;
    const bool crcOk = m_profile.crcValid();
    m_save->setEnabled(!rom && crcOk);
    m_status->setText(!crcOk ? tr("Uwaga: suma kontrolna profilu się nie zgadza – zapis zablokowany.")
                      : rom  ? tr("To profil fabryczny (ROM) – nie można go zmienić.")
                             : tr("Profil wczytany z myszy."));

    for (int i = 0; i < 5; ++i) {
        m_dpi[i]->setValue(m_profile.dpi(i) == 0xffff ? 0 : m_profile.dpi(i));
        m_dpiDefault[i]->setChecked(i == m_profile.defaultDpiIndex());
    }
    m_shift->setCurrentIndex(qBound(0, m_profile.shiftDpiIndex(), 4));
    const int ri = m_rate->findData(m_profile.reportRateMs());
    if (ri >= 0)
        m_rate->setCurrentIndex(ri);
    m_name->setText(m_profile.name());

    const QString rawLabel = tr("Makro / inne (bez zmian)");
    for (ButtonRow &row : m_buttons) {
        row.original = row.gshift ? m_profile.gbutton(row.index) : m_profile.button(row.index);
        const int rawIdx = row.action->findText(rawLabel);
        if (rawIdx >= 0)
            row.action->removeItem(rawIdx);
        row.key->clear();
        int idx = -1;
        if (row.original.kind == ButtonAction::Key) {
            idx = row.action->findData(QByteArray("key"));
            row.key->setKeySequence(actionToSequence(row.original));
        } else if (row.original.kind != ButtonAction::Raw) {
            ButtonAction canon = row.original;
            canon.raw.clear();
            idx = row.action->findData(canon.toBytes());
        } else {
            idx = row.action->findData(row.original.raw); // np. FF FF FF FF = nieprzypisany
        }
        if (idx < 0) {
            row.action->addItem(rawLabel, row.original.raw);
            idx = row.action->count() - 1;
        }
        row.action->setCurrentIndex(idx);
        row.key->setVisible(row.action->currentData().toByteArray() == "key");
    }
    m_light->setEffect(m_profile.lighting(0));
    m_lightDirty = false;
    m_loading = false;
    updateTrack();
}

QString MouseProfile::backupDir() const
{
    return QDir::homePath() + "/.local/share/logihub/backups";
}

void MouseProfile::save()
{
    hidpp::Profile p = m_profile;
    const QByteArray before = p.raw;

    int def = 0;
    for (int i = 0; i < 5; ++i) {
        int v = m_dpi[i]->value();
        if (v > 0 && v < m_dpiMin)
            v = m_dpiMin;
        p.setDpi(i, v);
        if (m_dpiDefault[i]->isChecked())
            def = i;
    }
    if (m_dpi[def]->value() == 0) {
        QMessageBox::warning(m_save, tr("DPI"), tr("Domyślny etap DPI musi mieć ustawioną wartość."));
        return;
    }
    if (m_dpi[m_shift->currentIndex()]->value() == 0) {
        QMessageBox::warning(m_save, tr("DPI"), tr("Etap DPI Shift musi mieć ustawioną wartość."));
        return;
    }
    p.setDefaultDpiIndex(def);
    p.setShiftDpiIndex(m_shift->currentIndex());
    p.setReportRateMs(m_rate->currentData().toInt());
    if (m_name->text() != m_profile.name())
        p.setName(m_name->text());

    for (const ButtonRow &row : m_buttons) {
        const QByteArray data = row.action->currentData().toByteArray();
        std::optional<ButtonAction> next;
        if (data == "key") {
            next = sequenceToAction(row.key->keySequence());
            if (!next) {
                QMessageBox::warning(m_save, tr("Przyciski"),
                                     tr("Przycisk %1%2: naciśnij obsługiwany klawisz (litera, cyfra, F1–F24, "
                                        "strzałki…).")
                                         .arg(row.index + 1)
                                         .arg(row.gshift ? " (G-Shift)" : ""));
                return;
            }
            if (row.original.kind == ButtonAction::Key && *next == row.original)
                next.reset();
        } else if (data != row.original.raw) {
            const ButtonAction sel = ButtonAction::fromBytes(data);
            if (!(sel == ButtonAction::fromBytes(row.original.raw)))
                next = sel;
        }
        if (next)
            row.gshift ? p.setGButton(row.index, *next) : p.setButton(row.index, *next);
    }
    if (m_lightDirty) {
        const Effect e = m_light->effect();
        p.setLighting(0, e);
        p.setLighting(1, e);
    }
    p.fixCrc();
    if (p.raw == before) {
        m_status->setText(tr("Brak zmian do zapisania."));
        return;
    }

    QDir().mkpath(backupDir());
    QString safeKey = m_info.key;
    safeKey.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    const QString file = QString("%1/%2-profil%3-%4.bin")
                             .arg(backupDir(), safeKey)
                             .arg(m_sector)
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(before) != before.size()) {
        QMessageBox::critical(m_save, tr("Kopia zapasowa"), tr("Nie udało się zapisać kopii zapasowej – przerywam."));
        return;
    }
    f.close();

    if (!onboard::writeSector(*m_dev, m_sector, p.raw)) {
        m_status->setText(tr("Błąd zapisu! Kopia poprzedniego profilu: %1").arg(file));
        return;
    }
    m_profile = p;
    m_lightDirty = false;
    if (onboard::activeSector(*m_dev) == m_sector)
        onboard::activate(*m_dev, m_sector); // przeładuj aktywny profil
    m_status->setText(tr("✔ Zapisano do myszy (kopia: %1)").arg(QFileInfo(file).fileName()));
}

void MouseProfile::restoreBackup()
{
    const QString file = QFileDialog::getOpenFileName(m_save, tr("Przywróć profil z kopii"), backupDir(),
                                                      tr("Kopie profili (*.bin)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    hidpp::Profile p;
    p.raw = f.readAll();
    if (p.raw.size() != m_oi.size || !p.crcValid()) {
        QMessageBox::warning(m_save, tr("Kopia"), tr("Ten plik nie jest poprawną kopią profilu tej myszy."));
        return;
    }
    if (QMessageBox::question(m_save, tr("Przywróć"),
                              tr("Nadpisać edytowany profil zawartością kopii?\n%1").arg(QFileInfo(file).fileName()))
        != QMessageBox::Yes)
        return;
    if (onboard::writeSector(*m_dev, m_sector, p.raw)) {
        loadSector(m_sector);
        if (onboard::activeSector(*m_dev) == m_sector)
            onboard::activate(*m_dev, m_sector);
        m_status->setText(tr("✔ Przywrócono profil z kopii."));
    } else {
        m_status->setText(tr("Błąd zapisu kopii do myszy."));
    }
}

// ===================================================================
DevicePage::DevicePage(const DeviceInfo &info, QWidget *parent) : QWidget(parent), m_info(info)
{
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(0, 0, 0, 0);
    m_root->setSpacing(0);

    auto *header = new QWidget;
    auto *h = new QHBoxLayout(header);
    h->setContentsMargins(28, 22, 28, 10);
    h->setSpacing(18);
    m_tile = new QLabel;
    m_tile->setPixmap(theme::deviceTile(iconName(), 76));
    h->addWidget(m_tile);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(6);
    auto *title = new QLabel(info.name);
    title->setObjectName("pageTitle");
    texts->addWidget(title);
    auto *chips = new QHBoxLayout;
    chips->setSpacing(8);
    m_conn = theme::chip(info.wireless ? tr("LIGHTSPEED") : tr("USB"));
    m_battery = theme::chip({});
    m_battery->setVisible(false);
    chips->addWidget(m_conn);
    chips->addWidget(m_battery);
    chips->addStretch();
    texts->addLayout(chips);
    h->addLayout(texts, 1);
    m_root->addWidget(header);

    m_waiting = new QLabel(tr("Łączenie z urządzeniem…"));
    m_waiting->setObjectName("hint");
    m_waiting->setAlignment(Qt::AlignCenter);
    m_root->addWidget(m_waiting, 1);

    if (!info.hidraw.isEmpty())
        m_dev = std::make_unique<HidppDevice>(info.hidraw, info.index);

    m_timer = new QTimer(this);
    m_timer->setInterval(20000);
    connect(m_timer, &QTimer::timeout, this, &DevicePage::refresh);
    m_timer->start();
    QTimer::singleShot(0, this, &DevicePage::refresh);
}

DevicePage::~DevicePage() = default;

QString DevicePage::iconName() const
{
    if (m_info.name.startsWith("Yeti"))
        return "audio-input-microphone";
    if (m_info.name.contains("Headset") || m_info.name.startsWith("Astro"))
        return "audio-headset";
    return "input-mouse";
}

QString DevicePage::slug() const
{
    QString s = m_info.name.toLower();
    s.replace(QRegularExpression("[^a-z0-9]+"), "-");
    return s;
}

QWidget *DevicePage::tab(const QString &title, const QList<QWidget *> &cards)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(28, 18, 28, 28);
    v->setSpacing(16);
    for (QWidget *c : cards)
        if (c)
            v->addWidget(c);
    v->addStretch();
    scroll->setWidget(body);
    m_tabs->addTab(scroll, title);
    return scroll;
}

void DevicePage::build()
{
    m_built = true;
    m_waiting->hide();
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    auto *tabsWrap = new QWidget;
    auto *tw = new QVBoxLayout(tabsWrap);
    tw->setContentsMargins(16, 0, 16, 0);
    tw->addWidget(m_tabs);
    m_root->addWidget(tabsWrap, 1);

    const bool isMouse = m_dev && (m_dev->has(feat::OnboardProfiles) || m_dev->has(feat::AdjustableDpi));
    if (isMouse)
        buildMouse();
    else if (m_info.name.startsWith("Yeti"))
        buildMic();
    else
        buildHeadset();
    updateModeVisibility();
}

void DevicePage::buildMouse()
{
    QWidget *mode = m_dev->has(feat::OnboardProfiles) ? buildMode() : nullptr;
    if (m_dev->has(feat::AdjustableDpi) || m_dev->has(feat::ReportRate))
        m_hostBox = buildHost();
    if (m_dev->has(feat::OnboardProfiles))
        if (auto oi = onboard::info(*m_dev))
            m_profile = new MouseProfile(m_dev.get(), m_info, *oi, this);
    if (m_dev->has(feat::RgbEffects))
        m_lightBox = buildLighting();

    tab(tr("Czułość"), {m_profile ? m_profile->sensitivityCard() : nullptr, m_hostBox});
    if (m_profile)
        tab(tr("Przyciski"), {m_profile->buttonsCard()});
    tab(tr("Podświetlenie"), {m_profile ? m_profile->lightingCard() : nullptr, m_lightBox});
    tab(tr("Profile i tryb"), {mode, m_profile ? m_profile->profileCard() : nullptr});
    if (m_profile)
        m_root->addWidget(m_profile->saveBar());
}

void DevicePage::buildMic()
{
    const QString kw = audioKeyword(m_info.name);
    const QString mic = nodeFor(kw, false);
    QWidget *voice = mic.isEmpty() ? nullptr
                                   : new VoiceEditor(m_info.key, "voice-" + slug(), m_info.name + " – Blue VO!CE", mic);
    tab("Blue VO!CE", {voice ? voice : theme::hint(tr("Nie znaleziono mikrofonu w PipeWire."))});
    QWidget *monitor = mic.isEmpty() ? nullptr
                                     : new SidetoneCard(m_info.key, "monitor-" + slug(), mic, {},
                                                        tr("Odsłuch mikrofonu"),
                                                        tr("Słyszysz własny głos w domyślnym wyjściu (słuchawki), "
                                                           "z małym opóźnieniem."));
    tab(tr("Mikrofon"), {volumeCard(kw, false, true, tr("Głośność")), hwGainCard(kw), monitor});
    if (m_dev) {
        QWidget *light = m_dev->has(feat::RgbEffects) ? (m_lightBox = buildLighting()) : nullptr;
        QWidget *bright = m_dev->has(feat::Brightness) ? buildBrightness() : nullptr;
        if (light || bright)
            tab(tr("Podświetlenie"), {light, bright});
    }
}

void DevicePage::buildHeadset()
{
    const QString kw = audioKeyword(m_info.name);
    const QString sink = nodeFor(kw, true);
    const QString mic = nodeFor(kw, false);
    if (!sink.isEmpty())
        tab(tr("Equalizer"), {new EqEditor(m_info.key, "eq-" + slug(), m_info.name + " – Equalizer", sink)});
    if (!mic.isEmpty())
        tab("Blue VO!CE", {new VoiceEditor(m_info.key, "voice-" + slug(), m_info.name + " – Blue VO!CE", mic)});
    QWidget *side = (mic.isEmpty() || sink.isEmpty())
                        ? nullptr
                        : new SidetoneCard(m_info.key, "sidetone-" + slug(), mic, sink, tr("Sidetone"),
                                           tr("Słyszysz swój głos w słuchawkach – mówisz naturalniej i ciszej. "
                                              "(Programowy, bo sprzętowy protokół G735 nie jest jeszcze znany.)"));
    tab(tr("Mikrofon"), {volumeCard(kw, false, true, tr("Głośność mikrofonu")), hwGainCard(kw), side});
    QVBoxLayout *b;
    auto *info = theme::card(tr("Sterowanie sprzętowe"), {}, &b);
    b->addWidget(theme::hint(tr("Podświetlenie LIGHTSYNC, sprzętowy sidetone i wbudowany EQ słuchawek używają "
                                "nieopisanego protokołu Logitecha (raporty 0xF0–0xF3). Equalizer, Blue VO!CE i "
                                "sidetone działają tu programowo – jak w G Hub.")));
    tab(tr("Dźwięk"), {volumeCard(kw, true, false, tr("Głośność słuchawek")), info});
}

QWidget *DevicePage::buildMode()
{
    QVBoxLayout *b;
    auto *box = theme::card(tr("Tryb pracy"),
                            tr("Pamięć wbudowana: mysz używa zapisanych profili (działają przyciski DPI). "
                               "Z aplikacji: DPI i RGB ustawiane na żywo z tego programu."),
                            &b);
    m_mode = new QComboBox;
    m_mode->addItem(tr("Pamięć wbudowana"), onboard::Onboard);
    m_mode->addItem(tr("Sterowanie z aplikacji"), onboard::Host);
    m_mode->setCurrentIndex(qMax(0, m_mode->findData(onboard::mode(*m_dev))));
    b->addWidget(m_mode);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        onboard::setMode(*m_dev, m_mode->currentData().toInt());
        updateModeVisibility();
        if (m_mode->currentData().toInt() == onboard::Host)
            applySaved();
        if (m_profile)
            m_profile->reload();
    });
    return box;
}

void DevicePage::updateModeVisibility()
{
    const bool host = !m_mode || m_mode->currentData().toInt() == onboard::Host;
    if (m_hostBox)
        m_hostBox->setVisible(host);
    if (m_lightBox && m_mode)
        m_lightBox->setVisible(host);
}

QWidget *DevicePage::buildHost()
{
    QVBoxLayout *b;
    auto *box = theme::card(tr("Na żywo (tryb z aplikacji)"),
                            tr("W tym trybie fizyczne przyciski DPI nie zmieniają czułości. Ustawienia są "
                               "przywracane przy starcie LogiHub."),
                            &b);
    auto *form = new QFormLayout;
    b->addLayout(form);
    if (m_dev->has(feat::AdjustableDpi)) {
        auto *spin = new QSpinBox;
        QList<int> list;
        if (auto r = m_dev->call(feat::AdjustableDpi, 0x10, bytes({0})))
            list = hidpp::parseDpiList(r->mid(1));
        spin->setRange(list.isEmpty() ? 100 : list.first(), list.isEmpty() ? 25600 : list.last());
        spin->setSingleStep(list.size() >= 2 ? list[1] - list[0] : 50);
        spin->setSuffix(" DPI");
        if (auto r = m_dev->call(feat::AdjustableDpi, 0x20, bytes({0})))
            spin->setValue((quint8(r->at(1)) << 8) | quint8(r->at(2)));
        spin->setKeyboardTracking(false);
        form->addRow(tr("DPI:"), spin);
        connect(spin, &QSpinBox::valueChanged, this, [this](int v) {
            m_dev->call(feat::AdjustableDpi, 0x30, bytes({0, v >> 8, v & 0xff}));
            QSettings().setValue(m_info.key + "/host/dpi", v);
        });
    }
    if (m_dev->has(feat::ReportRate)) {
        auto *rate = new QComboBox;
        int flags = 0x8b;
        if (auto r = m_dev->call(feat::ReportRate, 0x00))
            flags = quint8(r->at(0));
        for (int ms = 1; ms <= 8; ++ms)
            if (flags & (1 << (ms - 1)))
                rate->addItem(tr("%1 Hz").arg(1000 / ms), ms);
        if (auto r = m_dev->call(feat::ReportRate, 0x10))
            rate->setCurrentIndex(qMax(0, rate->findData(quint8(r->at(0)))));
        form->addRow(tr("Częstotliwość raportowania:"), rate);
        connect(rate, &QComboBox::currentIndexChanged, this, [this, rate] {
            const int ms = rate->currentData().toInt();
            m_dev->call(feat::ReportRate, 0x20, bytes({ms}));
            QSettings().setValue(m_info.key + "/host/rate", ms);
        });
    }
    return box;
}

QWidget *DevicePage::buildLighting()
{
    QVBoxLayout *b;
    auto *box = theme::card(tr("Podświetlenie na żywo"),
                            tr("Zmiany widać od razu. LogiHub przywraca je przy starcie."), &b);
    auto info = m_dev->call(feat::RgbEffects, 0x00, bytes({0xff, 0xff, 0}));
    const int zones = info ? quint8(info->at(2)) : 0;
    for (int z = 0; z < zones; ++z) {
        auto zi = m_dev->call(feat::RgbEffects, 0x00, bytes({z, 0xff, 0}));
        if (!zi)
            continue;
        const int location = (quint8(zi->at(2)) << 8) | quint8(zi->at(3));
        const int count = quint8(zi->at(4));
        QList<quint8> ids;
        for (int e = 0; e < count; ++e) {
            auto ei = m_dev->call(feat::RgbEffects, 0x00, bytes({z, e, 0}));
            ids << (ei ? quint8(ei->at(3)) : 0xfe);
        }
        auto *title = new QLabel(zoneName(location, z));
        title->setStyleSheet("font-weight: 700; margin-top: 6px;");
        b->addWidget(title);
        auto *ed = new EffectEditor;
        ed->setAvailable(ids, false);
        QSettings s;
        const QString k = QString("%1/zone%2/").arg(m_info.key).arg(z);
        Effect saved;
        saved.id = quint8(s.value(k + "id", Effect::Static).toInt());
        saved.rgb = s.value(k + "rgb", 0x00b4ff).toUInt();
        saved.periodMs = s.value(k + "period", 5000).toInt();
        ed->setEffect(saved);
        b->addWidget(ed);
        connect(ed, &EffectEditor::changed, this, [this, ed, ids, z, k] {
            const Effect e = ed->effect();
            const int idx = ids.indexOf(e.id);
            if (idx < 0)
                return;
            if (!m_claimed) // przejmij sterowanie LED od firmware
                m_claimed = m_dev->call(feat::RgbEffects, 0x50, bytes({0x01, 0x03, 0x04})).has_value();
            m_dev->call(feat::RgbEffects, 0x10, QByteArray(1, char(z)) + char(idx) + e.params() + char(0x01));
            QSettings s;
            s.setValue(k + "id", e.id);
            s.setValue(k + "rgb", e.rgb);
            s.setValue(k + "period", e.periodMs);
            s.setValue(k + "set", true);
        });
        box->setProperty(QString("zone%1").arg(z).toUtf8(), QVariant::fromValue<QObject *>(ed));
    }
    box->setProperty("zones", zones);
    if (zones == 0)
        b->addWidget(theme::hint(tr("Nie udało się odczytać stref LED.")));
    return box;
}

QWidget *DevicePage::buildBrightness()
{
    QVBoxLayout *b;
    auto *box = theme::card(tr("Jasność"), {}, &b);
    auto *slider = new QSlider(Qt::Horizontal);
    int max = 255;
    if (auto r = m_dev->call(feat::Brightness, 0x00)) {
        const int m = (quint8(r->at(0)) << 8) | quint8(r->at(1));
        if (m > 0)
            max = m;
    }
    slider->setRange(0, max);
    if (auto r = m_dev->call(feat::Brightness, 0x10))
        slider->setValue((quint8(r->at(0)) << 8) | quint8(r->at(1)));
    QLabel *val;
    b->addWidget(sliderRow(slider, "", &val));
    auto pct = [val, max](int v) { val->setText(QString::number(v * 100 / max) + "%"); };
    pct(slider->value());
    connect(slider, &QSlider::valueChanged, this, [this, slider, pct](int v) {
        pct(v);
        if (!slider->isSliderDown())
            m_dev->call(feat::Brightness, 0x20, bytes({v >> 8, v & 0xff}));
    });
    connect(slider, &QSlider::sliderReleased, this, [this, slider] {
        const int v = slider->value();
        m_dev->call(feat::Brightness, 0x20, bytes({v >> 8, v & 0xff}));
    });
    return box;
}

void DevicePage::applySaved()
{
    if (!m_dev || !m_online)
        return;
    QSettings s;
    const bool host = !m_mode || m_mode->currentData().toInt() == onboard::Host;
    if (host && m_mode) {
        if (s.contains(m_info.key + "/host/dpi")) {
            const int v = s.value(m_info.key + "/host/dpi").toInt();
            m_dev->call(feat::AdjustableDpi, 0x30, bytes({0, v >> 8, v & 0xff}));
        }
        if (s.contains(m_info.key + "/host/rate"))
            m_dev->call(feat::ReportRate, 0x20, bytes({s.value(m_info.key + "/host/rate").toInt()}));
    }
    if (m_lightBox && (!m_mode || host)) {
        const int zones = m_lightBox->property("zones").toInt();
        for (int z = 0; z < zones; ++z) {
            if (!s.value(QString("%1/zone%2/set").arg(m_info.key).arg(z)).toBool())
                continue;
            if (auto *ed = qobject_cast<EffectEditor *>(
                    m_lightBox->property(QString("zone%1").arg(z).toUtf8()).value<QObject *>()))
                emit ed->changed();
        }
    }
}

void DevicePage::refresh()
{
    QString status;
    if (!m_dev) {
        m_online = true;
        if (!m_built)
            build();
    } else {
        const bool was = m_online;
        m_online = m_dev->ping();
        if (!m_online) {
            m_battery->setVisible(false);
            m_conn->setText(m_info.wireless ? tr("LIGHTSPEED · uśpiona") : tr("USB · brak odpowiedzi"));
            if (!m_built)
                m_waiting->setText(tr("Urządzenie śpi – porusz myszą, żeby je wybudzić."));
            status = tr("uśpiona");
        } else {
            m_conn->setText(m_info.wireless ? tr("LIGHTSPEED") : tr("USB"));
            if (!m_built)
                build();
            if (!was)
                applySaved();
            if (auto r = m_dev->call(feat::UnifiedBattery, 0x10)) {
                const int pct = quint8(r->at(0));
                const int st = quint8(r->at(2));
                const bool charging = st == 1 || st == 2 || st == 4;
                m_battery->setVisible(true);
                m_battery->setObjectName(pct > 20 ? "chipGood" : "chip");
                m_battery->setStyleSheet({}); // przeładuj styl po zmianie objectName
                m_battery->setText(QString("%1 %2%").arg(charging ? "⚡" : "🔋").arg(pct));
                status = QString("%1%%2").arg(pct).arg(charging ? " · ładowanie" : "");
            }
        }
    }
    if (status != m_statusText) {
        m_statusText = status;
        emit statusChanged();
    }
}
