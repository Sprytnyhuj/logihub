#include "devicepage.h"
#include "audio.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

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
                 QObject::tr("G4 – boczny tylny"), QObject::tr("G6 – snajperski (DPI Shift)"),
                 QObject::tr("G5 – boczny przedni"), QObject::tr("Kółko w lewo"), QObject::tr("Kółko w prawo"),
                 QObject::tr("G9 – za kółkiem"), QObject::tr("G8 – DPI w górę"), QObject::tr("G7 – DPI w dół")};
    }
    for (int i = names.size(); i < count; ++i)
        names << QObject::tr("Przycisk %1").arg(i + 1);
    return names.mid(0, count);
}

void fillActionCombo(QComboBox *c)
{
    auto add = [c](const QString &text, ButtonAction::Kind k, quint16 v) {
        ButtonAction a;
        a.kind = k;
        a.value = v;
        c->addItem(text, a.toBytes());
    };
    add(QObject::tr("Lewy klik"), ButtonAction::Mouse, 0x0001);
    add(QObject::tr("Prawy klik"), ButtonAction::Mouse, 0x0002);
    add(QObject::tr("Środkowy klik"), ButtonAction::Mouse, 0x0004);
    add(QObject::tr("Wstecz (przycisk 4)"), ButtonAction::Mouse, 0x0008);
    add(QObject::tr("Dalej (przycisk 5)"), ButtonAction::Mouse, 0x0010);
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
    add(QObject::tr("G-Shift"), ButtonAction::Function, hidpp::FnGShift);
    add(QObject::tr("Pokaż stan baterii"), ButtonAction::Function, hidpp::FnBattery);
    c->insertSeparator(c->count());
    add(QObject::tr("Multimedia: odtwórz/pauza"), ButtonAction::Consumer, 0x00cd);
    add(QObject::tr("Multimedia: następny"), ButtonAction::Consumer, 0x00b5);
    add(QObject::tr("Multimedia: poprzedni"), ButtonAction::Consumer, 0x00b6);
    add(QObject::tr("Głośniej"), ButtonAction::Consumer, 0x00e9);
    add(QObject::tr("Ciszej"), ButtonAction::Consumer, 0x00ea);
    add(QObject::tr("Wycisz"), ButtonAction::Consumer, 0x00e2);
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

QLabel *note(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet("color: palette(placeholder-text);");
    return l;
}

} // namespace

// ===================================================================
EffectEditor::EffectEditor(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_combo = new QComboBox;
    m_color = new QPushButton;
    m_color->setFixedWidth(48);
    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setRange(1, 20); // okres w sekundach (odwrotnie: mniej = szybciej)
    m_speed->setInvertedAppearance(true);
    m_speed->setToolTip(tr("Szybkość efektu"));
    lay->addWidget(m_combo);
    lay->addWidget(m_color);
    lay->addWidget(new QLabel(tr("Szybkość:")));
    lay->addWidget(m_speed, 1);

    connect(m_combo, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        emit changed();
    });
    connect(m_color, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor::fromRgb(m_rgb), this, tr("Kolor podświetlenia"));
        if (!c.isValid())
            return;
        m_rgb = c.rgb() & 0xffffff;
        m_color->setStyleSheet(QString("background:%1").arg(c.name()));
        emit changed();
    });
    connect(m_speed, &QSlider::sliderReleased, this, &EffectEditor::changed);
    connect(m_speed, &QSlider::valueChanged, this, [this] {
        if (!m_speed->isSliderDown())
            emit changed();
    });
}

void EffectEditor::setAvailable(const QList<quint8> &ids, bool keepUnknown)
{
    QSignalBlocker b(m_combo);
    m_combo->clear();
    for (quint8 id : ids)
        if (Effect::known(id))
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
        m_rgb = e.rgb;
    m_color->setStyleSheet(QString("background:%1").arg(QColor::fromRgb(m_rgb).name()));
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
    m_color->setVisible(id == Effect::Static || id == Effect::Breathe);
    m_speed->setEnabled(id == Effect::Breathe || id == Effect::Cycle || id == Effect::CycleSat || id == Effect::Wave);
}

// ===================================================================
ProfileEditor::ProfileEditor(HidppDevice *dev, const DeviceInfo &info, const onboard::Info &oi, QWidget *parent)
    : QGroupBox(tr("Profil w pamięci myszy"), parent), m_dev(dev), m_info(info), m_oi(oi)
{
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(note(tr("Te ustawienia są zapisywane w myszy – działają w trybie „Pamięć wbudowana”, "
                           "także bez tej aplikacji i na innych komputerach.")));

    auto *top = new QHBoxLayout;
    m_profileCombo = new QComboBox;
    auto *activateBtn = new QPushButton(tr("Ustaw jako aktywny"));
    top->addWidget(new QLabel(tr("Profil:")));
    top->addWidget(m_profileCombo, 1);
    top->addWidget(activateBtn);
    lay->addLayout(top);

    // --- DPI ---
    int dpiMin = 100, dpiMax = 25600, dpiStep = 50;
    if (auto r = m_dev->call(feat::AdjustableDpi, 0x10, bytes({0}))) {
        const QList<int> l = hidpp::parseDpiList(r->mid(1));
        if (l.size() >= 2) {
            dpiMin = l.first();
            dpiMax = l.last();
            dpiStep = l[1] - l[0];
        }
    }
    auto *dpiBox = new QGroupBox(tr("Czułość (DPI)"));
    auto *dpiGrid = new QGridLayout(dpiBox);
    auto *group = new QButtonGroup(this);
    dpiGrid->addWidget(new QLabel(tr("Etap")), 0, 0);
    dpiGrid->addWidget(new QLabel(tr("DPI")), 0, 1);
    dpiGrid->addWidget(new QLabel(tr("Domyślny")), 0, 2);
    for (int i = 0; i < 5; ++i) {
        auto *spin = new QSpinBox;
        spin->setRange(0, dpiMax);
        spin->setSingleStep(dpiStep);
        spin->setSpecialValueText(tr("wyłączony"));
        spin->setToolTip(tr("%1–%2 DPI, co %3; 0 = etap wyłączony").arg(dpiMin).arg(dpiMax).arg(dpiStep));
        auto *rb = new QRadioButton;
        group->addButton(rb, i);
        dpiGrid->addWidget(new QLabel(QString::number(i + 1)), i + 1, 0);
        dpiGrid->addWidget(spin, i + 1, 1);
        dpiGrid->addWidget(rb, i + 1, 2);
        m_dpi << spin;
        m_dpiDefault << rb;
    }
    auto *misc = new QFormLayout;
    m_shift = new QComboBox;
    for (int i = 0; i < 5; ++i)
        m_shift->addItem(tr("Etap %1").arg(i + 1), i);
    misc->addRow(tr("DPI Shift (snajperski):"), m_shift);
    m_rate = new QComboBox;
    int rateFlags = 0x8b;
    if (auto r = m_dev->call(feat::ReportRate, 0x00))
        rateFlags = quint8(r->at(0));
    for (int ms : {1, 2, 4, 8})
        if (rateFlags & (1 << (ms - 1)))
            m_rate->addItem(tr("%1 Hz").arg(1000 / ms), ms);
    misc->addRow(tr("Częstotliwość raportowania:"), m_rate);
    dpiGrid->addLayout(misc, 6, 0, 1, 3);
    lay->addWidget(dpiBox);

    // --- Przyciski ---
    auto *btnBox = new QGroupBox(tr("Przypisania przycisków"));
    auto *btnGrid = new QGridLayout(btnBox);
    const QStringList names = buttonNames(m_info.name, m_oi.buttons);
    for (int i = 0; i < m_oi.buttons; ++i) {
        ButtonRow row;
        row.action = new QComboBox;
        fillActionCombo(row.action);
        row.key = new QKeySequenceEdit;
        row.key->setMaximumSequenceLength(1);
        row.key->setClearButtonEnabled(true);
        row.key->setVisible(false);
        btnGrid->addWidget(new QLabel(names.value(i)), i, 0);
        btnGrid->addWidget(row.action, i, 1);
        btnGrid->addWidget(row.key, i, 2);
        QKeySequenceEdit *key = row.key;
        QComboBox *combo = row.action;
        connect(combo, &QComboBox::currentIndexChanged, key, [combo, key] {
            key->setVisible(combo->currentData().toByteArray() == "key");
        });
        m_buttons << row;
    }
    btnGrid->setColumnStretch(1, 1);
    btnGrid->setColumnStretch(2, 1);
    lay->addWidget(btnBox);

    // --- Podświetlenie w profilu ---
    auto *lightBox = new QGroupBox(tr("Podświetlenie (zapisywane w profilu)"));
    auto *lightLay = new QVBoxLayout(lightBox);
    m_light = new EffectEditor;
    m_light->setAvailable({Effect::Off, Effect::Static, Effect::Breathe, Effect::Cycle}, true);
    connect(m_light, &EffectEditor::changed, this, [this] { m_lightDirty = true; });
    lightLay->addWidget(m_light);
    lightLay->addWidget(note(tr("Jeśli działa OpenRGB albo tryb „Sterowanie z aplikacji”, "
                                "to one nadpisują podświetlenie z profilu.")));
    lay->addWidget(lightBox);

    // --- Zapis ---
    auto *bottom = new QHBoxLayout;
    m_status = new QLabel;
    m_status->setWordWrap(true);
    auto *restore = new QPushButton(tr("Przywróć kopię…"));
    m_save = new QPushButton(tr("Zapisz do myszy"));
    m_save->setDefault(true);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(restore);
    bottom->addWidget(m_save);
    lay->addLayout(bottom);

    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, [this] {
        loadSector(m_profileCombo->currentData().toInt());
    });
    connect(activateBtn, &QPushButton::clicked, this, [this] {
        const bool ok = onboard::activate(*m_dev, m_sector);
        m_status->setText(ok ? tr("Profil aktywny.") : tr("Nie udało się aktywować profilu."));
    });
    connect(m_save, &QPushButton::clicked, this, &ProfileEditor::save);
    connect(restore, &QPushButton::clicked, this, &ProfileEditor::restoreBackup);

    reload();
}

void ProfileEditor::reload()
{
    m_headers = onboard::headers(*m_dev);
    const int active = onboard::activeSector(*m_dev);
    QSignalBlocker b(m_profileCombo);
    m_profileCombo->clear();
    for (const auto &h : m_headers) {
        if (!h.enabled)
            continue;
        QString label = tr("Profil %1").arg(h.sector & 0xff);
        if (h.sector >= 0x100)
            label += tr(" (fabryczny, tylko odczyt)");
        if (h.sector == active)
            label += tr(" – aktywny");
        m_profileCombo->addItem(label, h.sector);
    }
    const int idx = m_profileCombo->findData(active);
    m_profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    if (m_profileCombo->count())
        loadSector(m_profileCombo->currentData().toInt());
    else
        m_status->setText(tr("Nie znaleziono profili w pamięci."));
}

void ProfileEditor::loadSector(int sector)
{
    auto raw = onboard::readSector(*m_dev, sector, m_oi.size);
    if (!raw) {
        m_status->setText(tr("Nie udało się odczytać profilu – czy mysz jest wybudzona?"));
        m_save->setEnabled(false);
        return;
    }
    m_sector = sector;
    m_profile.raw = *raw;
    const bool rom = sector >= 0x100 || sector >= m_oi.sectors;
    const bool crcOk = m_profile.crcValid();
    m_save->setEnabled(!rom && crcOk);
    m_status->setText(!crcOk ? tr("Uwaga: suma kontrolna profilu się nie zgadza – zapis zablokowany.")
                      : rom  ? tr("To profil fabryczny (ROM) – nie można go zmienić.")
                             : QString());

    for (int i = 0; i < 5; ++i) {
        m_dpi[i]->setValue(m_profile.dpi(i) == 0xffff ? 0 : m_profile.dpi(i));
        m_dpiDefault[i]->setChecked(i == m_profile.defaultDpiIndex());
    }
    m_shift->setCurrentIndex(qBound(0, m_profile.shiftDpiIndex(), 4));
    const int ri = m_rate->findData(m_profile.reportRateMs());
    if (ri >= 0)
        m_rate->setCurrentIndex(ri);

    for (int i = 0; i < m_buttons.size(); ++i) {
        ButtonRow &row = m_buttons[i];
        row.original = m_profile.button(i);
        // usuń poprzednią pozycję „bez zmian”
        const int rawIdx = row.action->findText(tr("Makro / inne (bez zmian)"));
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
        }
        if (idx < 0) {
            row.action->addItem(tr("Makro / inne (bez zmian)"), row.original.raw);
            idx = row.action->count() - 1;
        }
        row.action->setCurrentIndex(idx);
        row.key->setVisible(row.action->currentData().toByteArray() == "key");
    }
    m_light->setEffect(m_profile.lighting(0));
    m_lightDirty = false;
}

QString ProfileEditor::backupDir() const
{
    return QDir::homePath() + "/.local/share/logihub/backups";
}

void ProfileEditor::save()
{
    hidpp::Profile p = m_profile;
    const QByteArray before = p.raw;

    int enabled = 0;
    for (int i = 0; i < 5; ++i) {
        int v = m_dpi[i]->value();
        if (v > 0 && v < 100)
            v = 100;
        if (v > 0)
            ++enabled;
        p.setDpi(i, v);
    }
    int def = 0;
    for (int i = 0; i < 5; ++i)
        if (m_dpiDefault[i]->isChecked())
            def = i;
    if (enabled == 0 || m_dpi[def]->value() == 0) {
        QMessageBox::warning(this, tr("DPI"), tr("Domyślny etap DPI musi mieć ustawioną wartość."));
        return;
    }
    p.setDefaultDpiIndex(def);
    if (m_dpi[m_shift->currentIndex()]->value() == 0) {
        QMessageBox::warning(this, tr("DPI"), tr("Etap DPI Shift musi mieć ustawioną wartość."));
        return;
    }
    p.setShiftDpiIndex(m_shift->currentIndex());
    p.setReportRateMs(m_rate->currentData().toInt());

    for (int i = 0; i < m_buttons.size(); ++i) {
        const ButtonRow &row = m_buttons[i];
        const QByteArray data = row.action->currentData().toByteArray();
        if (data == "key") {
            auto a = sequenceToAction(row.key->keySequence());
            if (!a) {
                QMessageBox::warning(this, tr("Przyciski"),
                                     tr("Przycisk %1: wybierz obsługiwany klawisz (litera, cyfra, F1–F24, strzałki…).")
                                         .arg(i + 1));
                return;
            }
            if (!(row.original.kind == ButtonAction::Key && *a == row.original))
                p.setButton(i, *a);
        } else if (data != row.original.raw) {
            ButtonAction cur = ButtonAction::fromBytes(row.original.raw);
            ButtonAction sel = ButtonAction::fromBytes(data);
            if (!(cur == sel)) // nie przepisuj, jeśli to ta sama akcja w innym zapisie
                p.setButton(i, sel);
        }
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

    // Kopia zapasowa przed zapisem
    QDir().mkpath(backupDir());
    QString safeKey = m_info.key;
    safeKey.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    const QString file = QString("%1/%2-profil%3-%4.bin")
                             .arg(backupDir(), safeKey)
                             .arg(m_sector)
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(before) != before.size()) {
        QMessageBox::critical(this, tr("Kopia zapasowa"), tr("Nie udało się zapisać kopii zapasowej – przerywam."));
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
    m_status->setText(tr("Zapisano do myszy. Kopia poprzedniej wersji: %1").arg(QFileInfo(file).fileName()));
}

void ProfileEditor::restoreBackup()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Przywróć profil z kopii"), backupDir(),
                                                      tr("Kopie profili (*.bin)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    hidpp::Profile p;
    p.raw = f.readAll();
    if (p.raw.size() != m_oi.size || !p.crcValid()) {
        QMessageBox::warning(this, tr("Kopia"), tr("Ten plik nie jest poprawną kopią profilu tej myszy."));
        return;
    }
    if (QMessageBox::question(this, tr("Przywróć"),
                              tr("Nadpisać wybrany profil zawartością kopii?\n%1").arg(QFileInfo(file).fileName()))
        != QMessageBox::Yes)
        return;
    if (onboard::writeSector(*m_dev, m_sector, p.raw)) {
        loadSector(m_sector);
        if (onboard::activeSector(*m_dev) == m_sector)
            onboard::activate(*m_dev, m_sector);
        m_status->setText(tr("Przywrócono profil z kopii."));
    } else {
        m_status->setText(tr("Błąd zapisu kopii do myszy."));
    }
}

// ===================================================================
DevicePage::DevicePage(const DeviceInfo &info, QWidget *parent) : QScrollArea(parent), m_info(info)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    m_body = new QWidget;
    auto *lay = new QVBoxLayout(m_body);
    m_header = new QLabel(QString("<h2>%1</h2>").arg(info.name.toHtmlEscaped()));
    lay->addWidget(m_header);
    auto *bat = new QHBoxLayout;
    m_batteryLabel = new QLabel;
    m_battery = new QProgressBar;
    m_battery->setRange(0, 100);
    m_battery->setMaximumWidth(260);
    m_battery->setVisible(false);
    bat->addWidget(m_batteryLabel);
    bat->addWidget(m_battery);
    bat->addStretch();
    lay->addLayout(bat);
    lay->addStretch();
    setWidget(m_body);

    if (!info.hidraw.isEmpty())
        m_dev = std::make_unique<HidppDevice>(info.hidraw, info.index);

    m_timer = new QTimer(this);
    m_timer->setInterval(20000);
    connect(m_timer, &QTimer::timeout, this, &DevicePage::refresh);
    m_timer->start();
    QTimer::singleShot(0, this, &DevicePage::refresh);
}

DevicePage::~DevicePage() = default;

void DevicePage::build()
{
    m_built = true;
    auto *lay = qobject_cast<QVBoxLayout *>(m_body->layout());
    const int pos = lay->count() - 1; // przed stretch
    QList<QWidget *> sections;

    if (!m_dev) {
        sections << new QGroupBox(tr("Sterowanie sprzętowe"));
        auto *l = new QVBoxLayout(sections.last());
        l->addWidget(note(tr("To urządzenie używa protokołu Logitecha, który nie jest jeszcze opisany dla Linuksa "
                             "(G735: raporty 0xF0–0xF3). Dźwięk działa normalnie – poniżej głośność i wyciszenie. "
                             "Podświetlenie, sidetone i EQ wymagają rozpracowania protokołu.")));
    } else {
        if (m_dev->has(feat::OnboardProfiles))
            sections << buildMode();
        if (m_dev->has(feat::AdjustableDpi) || m_dev->has(feat::ReportRate))
            sections << (m_hostBox = buildHost());
        if (m_dev->has(feat::OnboardProfiles)) {
            if (auto oi = onboard::info(*m_dev))
                sections << (m_profile = new ProfileEditor(m_dev.get(), m_info, *oi));
        }
        if (m_dev->has(feat::RgbEffects))
            sections << (m_lightBox = buildLighting());
        if (m_dev->has(feat::Brightness))
            sections << buildBrightness();
    }
    if (auto *a = buildAudio())
        sections << a;
    for (int i = 0; i < sections.size(); ++i)
        if (sections[i])
            lay->insertWidget(pos + i, sections[i]);
    updateModeVisibility();
}

QGroupBox *DevicePage::buildMode()
{
    auto *box = new QGroupBox(tr("Tryb pracy"));
    auto *l = new QVBoxLayout(box);
    m_mode = new QComboBox;
    m_mode->addItem(tr("Pamięć wbudowana – działa bez aplikacji, przyciski DPI aktywne"), onboard::Onboard);
    m_mode->addItem(tr("Sterowanie z aplikacji – DPI i RGB ustawiane na żywo"), onboard::Host);
    const int idx = m_mode->findData(onboard::mode(*m_dev));
    m_mode->setCurrentIndex(qMax(0, idx));
    l->addWidget(m_mode);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        onboard::setMode(*m_dev, m_mode->currentData().toInt());
        QSettings s;
        s.setValue(m_info.key + "/mode", m_mode->currentData().toInt());
        updateModeVisibility();
        if (m_mode->currentData().toInt() == onboard::Host)
            applySaved();
    });
    return box;
}

void DevicePage::updateModeVisibility()
{
    const bool host = !m_mode || m_mode->currentData().toInt() == onboard::Host;
    if (m_hostBox)
        m_hostBox->setVisible(host);
    // Mysz z profilami: podświetlenie na żywo tylko w trybie z aplikacji.
    if (m_lightBox && m_mode)
        m_lightBox->setVisible(host);
}

QGroupBox *DevicePage::buildHost()
{
    auto *box = new QGroupBox(tr("Ustawienia na żywo"));
    auto *form = new QFormLayout(box);
    QSettings s;
    if (m_dev->has(feat::AdjustableDpi)) {
        auto *spin = new QSpinBox;
        QList<int> list;
        if (auto r = m_dev->call(feat::AdjustableDpi, 0x10, bytes({0})))
            list = hidpp::parseDpiList(r->mid(1));
        spin->setRange(list.isEmpty() ? 100 : list.first(), list.isEmpty() ? 25600 : list.last());
        spin->setSingleStep(list.size() >= 2 ? list[1] - list[0] : 50);
        if (auto r = m_dev->call(feat::AdjustableDpi, 0x20, bytes({0})))
            spin->setValue((quint8(r->at(1)) << 8) | quint8(r->at(2)));
        spin->setKeyboardTracking(false);
        form->addRow(tr("DPI:"), spin);
        connect(spin, &QSpinBox::valueChanged, this, [this, spin](int v) {
            m_dev->call(feat::AdjustableDpi, 0x30, bytes({0, v >> 8, v & 0xff}));
            QSettings().setValue(m_info.key + "/host/dpi", v);
            Q_UNUSED(spin);
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
    if (m_mode)
        form->addRow(note(tr("W tym trybie fizyczne przyciski DPI na myszy nie zmieniają czułości. "
                             "Ustawienia są przywracane przy starcie aplikacji.")));
    return box;
}

QGroupBox *DevicePage::buildLighting()
{
    auto *box = new QGroupBox(tr("Podświetlenie RGB"));
    auto *form = new QFormLayout(box);
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
        auto *ed = new EffectEditor;
        ed->setAvailable(ids, false);
        QSettings s;
        const QString k = QString("%1/zone%2/").arg(m_info.key).arg(z);
        Effect saved;
        saved.id = quint8(s.value(k + "id", Effect::Static).toInt());
        saved.rgb = s.value(k + "rgb", 0x00a0ff).toUInt();
        saved.periodMs = s.value(k + "period", 5000).toInt();
        ed->setEffect(saved);
        form->addRow(zoneName(location, z) + ":", ed);
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
        form->addRow(note(tr("Nie udało się odczytać stref LED.")));
    return box;
}

QGroupBox *DevicePage::buildBrightness()
{
    auto *box = new QGroupBox(tr("Jasność podświetlenia"));
    auto *l = new QHBoxLayout(box);
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
    auto *val = new QLabel(QString::number(slider->value() * 100 / max) + "%");
    l->addWidget(slider, 1);
    l->addWidget(val);
    connect(slider, &QSlider::valueChanged, this, [this, slider, val, max](int v) {
        val->setText(QString::number(v * 100 / max) + "%");
        if (slider->isSliderDown())
            return;
        m_dev->call(feat::Brightness, 0x20, bytes({v >> 8, v & 0xff}));
    });
    connect(slider, &QSlider::sliderReleased, this, [this, slider] {
        const int v = slider->value();
        m_dev->call(feat::Brightness, 0x20, bytes({v >> 8, v & 0xff}));
    });
    return box;
}

QGroupBox *DevicePage::buildAudio()
{
    const QList<AudioNode> nodes = audio::findNodes(audioKeyword(m_info.name));
    if (nodes.isEmpty())
        return nullptr;
    auto *box = new QGroupBox(tr("Dźwięk"));
    auto *form = new QFormLayout(box);
    for (const AudioNode &n : nodes) {
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        auto *val = new QLabel;
        val->setMinimumWidth(40);
        auto *mute = new QCheckBox(tr("Wycisz"));
        bool muted = false;
        const int v = audio::volume(n.id, &muted);
        slider->setValue(qMax(0, v));
        val->setText(QString("%1%").arg(slider->value()));
        mute->setChecked(muted);
        h->addWidget(slider, 1);
        h->addWidget(val);
        h->addWidget(mute);
        const int id = n.id;
        connect(slider, &QSlider::valueChanged, this, [id, val](int x) {
            val->setText(QString("%1%").arg(x));
            audio::setVolume(id, x);
        });
        connect(mute, &QCheckBox::toggled, this, [id](bool m) { audio::setMuted(id, m); });
        form->addRow((n.isSink ? tr("Wyjście") : tr("Mikrofon")) + ":", row);
    }
    return box;
}

void DevicePage::applySaved()
{
    if (!m_dev || !m_online)
        return;
    QSettings s;
    const bool host = !m_mode || m_mode->currentData().toInt() == onboard::Host;
    if (host && m_mode) { // tylko mysz z profilami - DPI/polling na żywo
        if (s.contains(m_info.key + "/host/dpi")) {
            const int v = s.value(m_info.key + "/host/dpi").toInt();
            m_dev->call(feat::AdjustableDpi, 0x30, bytes({0, v >> 8, v & 0xff}));
        }
        if (s.contains(m_info.key + "/host/rate"))
            m_dev->call(feat::ReportRate, 0x20, bytes({s.value(m_info.key + "/host/rate").toInt()}));
    }
    if (m_lightBox && m_lightBox->isVisibleTo(this)) {
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
        m_batteryLabel->setText(tr("Połączenie: USB / odbiornik (bez sterowania HID++)"));
        if (!m_built)
            build();
    } else {
        const bool was = m_online;
        m_online = m_dev->ping();
        if (!m_online) {
            m_batteryLabel->setText(m_info.wireless ? tr("Uśpiona lub wyłączona – porusz myszą")
                                                    : tr("Brak odpowiedzi"));
            m_battery->setVisible(false);
            status = tr("uśpiona");
        } else {
            if (!m_built)
                build();
            if (!was)
                applySaved();
            if (auto r = m_dev->call(feat::UnifiedBattery, 0x10)) {
                const int pct = quint8(r->at(0));
                const int st = quint8(r->at(2));
                const bool charging = st == 1 || st == 2 || st == 4;
                m_battery->setVisible(true);
                m_battery->setValue(pct);
                m_batteryLabel->setText(charging ? tr("Bateria (ładowanie):") : st == 3 ? tr("Bateria (pełna):")
                                                                                        : tr("Bateria:"));
                status = QString("%1%%2").arg(pct).arg(charging ? " ⚡" : "");
            } else {
                m_batteryLabel->setText(m_info.wireless ? tr("Połączenie: Lightspeed") : tr("Połączenie: USB"));
            }
        }
    }
    if (status != m_statusText) {
        m_statusText = status;
        emit statusChanged();
    }
}
