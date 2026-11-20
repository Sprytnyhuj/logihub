#include "rgbpanel.h"
#include "theme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTcpSocket>
#include <QVBoxLayout>

namespace {
const quint32 kSwatches[] = {0x00b4ff, 0x2f6bff, 0x8a3dff, 0xff2fd0, 0xff2f3d, 0xff8a00, 0xffe600, 0x3ddc84, 0xffffff};

QString typeName(int t)
{
    switch (t) {
    case 0: return QObject::tr("Płyta główna");
    case 1: return QObject::tr("Pamięć RAM");
    case 2: return QObject::tr("Karta graficzna");
    case 3: return QObject::tr("Chłodzenie");
    case 4: return QObject::tr("Taśma LED");
    case 5: return QObject::tr("Klawiatura");
    case 6: return QObject::tr("Mysz");
    case 7: return QObject::tr("Podkładka");
    case 8: return QObject::tr("Słuchawki");
    case 11: return QObject::tr("Obudowa");
    case 16: return QObject::tr("Mikrofon");
    default: return QObject::tr("Urządzenie");
    }
}
}

// ===================================================================
SwatchRow::SwatchRow(quint32 rgb, QWidget *parent) : QWidget(parent), m_rgb(rgb)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    for (quint32 c : kSwatches) {
        auto *b = new QPushButton;
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("rgb", c);
        connect(b, &QPushButton::clicked, this, [this, c] {
            setColor(c);
            emit picked(c);
        });
        m_swatches << b;
        h->addWidget(b);
    }
    m_custom = new QPushButton(tr("Własny…"));
    connect(m_custom, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor::fromRgb(m_rgb), this, tr("Kolor"));
        if (!c.isValid())
            return;
        setColor(c.rgb() & 0xffffff);
        emit picked(m_rgb);
    });
    h->addWidget(m_custom);
    h->addStretch();
    setColor(rgb);
}

void SwatchRow::setColor(quint32 rgb)
{
    m_rgb = rgb;
    for (QPushButton *b : m_swatches) {
        const quint32 c = b->property("rgb").toUInt();
        b->setStyleSheet(QString("QPushButton { background: %1; border-radius: 14px; border: %2; padding: 0; }")
                             .arg(QColor::fromRgb(c).name(), c == rgb ? "3px solid #ffffff" : "1px solid #2c3343"));
    }
}

// ===================================================================
bool RgbPanel::serverAvailable()
{
    QTcpSocket s;
    s.connectToHost("127.0.0.1", 6742);
    return s.waitForConnected(300);
}

RgbPanel::RgbPanel(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(16);

    // Synchronizacja - jeden kolor na wszystkim
    QVBoxLayout *b;
    auto *syncBtn = new QPushButton(tr("Zastosuj na wszystkich"));
    syncBtn->setObjectName("primary");
    v->addWidget(theme::card(tr("Synchronizacja kolorów"),
                             tr("Jeden kolor na wszystkich urządzeniach z OpenRGB naraz – jak LIGHTSYNC w G Hub."),
                             &b, syncBtn));
    QSettings s;
    auto *sync = new SwatchRow(s.value("openrgb/syncColor", 0x00b4ff).toUInt());
    b->addWidget(sync);
    connect(sync, &SwatchRow::picked, this, [](quint32 c) { QSettings().setValue("openrgb/syncColor", c); });
    connect(syncBtn, &QPushButton::clicked, this, [this, sync] {
        int ok = 0;
        for (int i = 0; i < m_ctrls.size(); ++i)
            ok += apply(i, syncMode(m_ctrls[i]), sync->color()) ? 1 : 0;
        m_status->setText(tr("Kolor ustawiony na %1 z %2 urządzeń.").arg(ok).arg(m_ctrls.size()));
        reload(); // odśwież wybrane tryby w kartach
    });

    m_list = new QVBoxLayout;
    m_list->setSpacing(16);
    v->addLayout(m_list);

    // Zapis jako profil startowy OpenRGB
    const QString profile = orgb::serverProfileName();
    auto *save = new QPushButton(profile.isEmpty() ? tr("Zapisz profil „LogiHub”")
                                                   : tr("Zapisz w profilu „%1”").arg(profile));
    auto *refresh = new QPushButton(tr("Odśwież listę"));
    auto *right = new QWidget;
    auto *rh = new QHBoxLayout(right);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->addWidget(refresh);
    rh->addWidget(save);
    v->addWidget(theme::card(tr("Zachowaj ustawienia"),
                             profile.isEmpty()
                                 ? tr("Zmiany działają od razu. Zapis w profilu OpenRGB pozwala wczytać je przy starcie.")
                                 : tr("Serwer OpenRGB startuje z profilem „%1” (także po wybudzeniu). Zapisz w nim "
                                      "kolory, żeby zostały na stałe.").arg(profile),
                             &b, right));
    m_status = theme::hint({});
    b->addWidget(m_status);
    connect(refresh, &QPushButton::clicked, this, &RgbPanel::reload);
    connect(save, &QPushButton::clicked, this, [this, profile] {
        const QString name = profile.isEmpty() ? QStringLiteral("LogiHub") : profile;
        if (QMessageBox::question(this, tr("Profil OpenRGB"),
                                  tr("Nadpisać profil OpenRGB „%1” obecnymi kolorami wszystkich urządzeń?").arg(name))
            != QMessageBox::Yes)
            return;
        const bool ok = ensureConnected() && m_client->saveProfile(name);
        m_status->setText(ok ? tr("✔ Zapisano profil „%1”.").arg(name) : tr("Nie udało się zapisać profilu."));
    });

    reload();
}

bool RgbPanel::ensureConnected()
{
    if (m_client && m_client->connected())
        return true;
    m_client = std::make_unique<orgb::Client>();
    return m_client->connectToServer();
}

void RgbPanel::reload()
{
    // wyczyść stare karty
    while (QLayoutItem *it = m_list->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    if (!ensureConnected()) {
        m_list->addWidget(theme::hint(tr("Serwer OpenRGB nie działa. Uruchom OpenRGB z opcją --server.")));
        return;
    }
    m_ctrls = m_client->controllers();
    for (int pos = 0; pos < m_ctrls.size(); ++pos) {
        const orgb::Controller &c = m_ctrls[pos];
        auto *mode = new QComboBox;
        for (const auto &m : c.modes)
            mode->addItem(m.name);
        mode->setCurrentIndex(qBound(0, c.activeMode, int(c.modes.size()) - 1));
        QVBoxLayout *b;
        QString sub = typeName(c.type);
        if (!c.vendor.isEmpty())
            sub += " · " + c.vendor;
        sub += tr(" · %1 LED").arg(c.ledCount);
        auto *card = theme::card(c.name, sub, &b, mode);
        quint32 current = 0x00b4ff;
        if (c.activeMode >= 0 && c.activeMode < c.modes.size() && !c.modes[c.activeMode].colors.isEmpty())
            current = orgb::toRgb(c.modes[c.activeMode].colors.first());
        else if (!c.colors.isEmpty())
            current = orgb::toRgb(c.colors.first());
        auto *sw = new SwatchRow(current);
        b->addWidget(sw);
        auto updateVis = [this, pos, mode, sw] {
            const auto &m = m_ctrls[pos].modes.value(mode->currentIndex());
            sw->setVisible(m.colorMode == orgb::ColorsPerLed || m.colorMode == orgb::ColorsModeSpecific);
        };
        updateVis();
        connect(mode, &QComboBox::currentIndexChanged, this, [this, pos, mode, sw, updateVis] {
            updateVis();
            apply(pos, mode->currentIndex(), sw->color());
        });
        connect(sw, &SwatchRow::picked, this, [this, pos, mode](quint32 rgb) { apply(pos, mode->currentIndex(), rgb); });
        m_list->addWidget(card);
    }
    if (m_ctrls.isEmpty())
        m_list->addWidget(theme::hint(tr("OpenRGB nie znalazł żadnych urządzeń.")));
}

int RgbPanel::syncMode(const orgb::Controller &c) const
{
    // najlepiej „Static” z jednym kolorem, potem „Direct” (kolor na każdą diodę)
    for (int i = 0; i < c.modes.size(); ++i)
        if (c.modes[i].name.compare("Static", Qt::CaseInsensitive) == 0 && c.modes[i].colorMode != orgb::ColorsNone)
            return i;
    for (int i = 0; i < c.modes.size(); ++i)
        if (c.modes[i].colorMode == orgb::ColorsPerLed)
            return i;
    for (int i = 0; i < c.modes.size(); ++i)
        if (c.modes[i].colorMode == orgb::ColorsModeSpecific)
            return i;
    return c.activeMode;
}

bool RgbPanel::apply(int pos, int mode, quint32 rgb)
{
    if (pos < 0 || pos >= m_ctrls.size())
        return false;
    if (!ensureConnected())
        return false;
    bool ok = m_client->applyColor(m_ctrls[pos], mode, rgb);
    if (!ok) { // serwer mógł się zrestartować (np. po wybudzeniu) - połącz ponownie
        m_client.reset();
        ok = ensureConnected() && m_client->applyColor(m_ctrls[pos], mode, rgb);
    }
    if (ok)
        m_ctrls[pos].activeMode = mode;
    m_status->setText(ok ? tr("Zmieniono: %1").arg(m_ctrls[pos].name) : tr("Błąd połączenia z OpenRGB."));
    return ok;
}
