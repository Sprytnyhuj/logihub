#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace theme {

void apply()
{
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor("#0d0f13"));
    p.setColor(QPalette::WindowText, QColor("#e8ebf2"));
    p.setColor(QPalette::Base, QColor("#1a1e27"));
    p.setColor(QPalette::AlternateBase, QColor("#151820"));
    p.setColor(QPalette::Text, QColor("#e8ebf2"));
    p.setColor(QPalette::Button, QColor("#1f2430"));
    p.setColor(QPalette::ButtonText, QColor("#e8ebf2"));
    p.setColor(QPalette::Highlight, QColor(Accent));
    p.setColor(QPalette::HighlightedText, QColor("#001018"));
    p.setColor(QPalette::ToolTipBase, QColor("#1f2430"));
    p.setColor(QPalette::ToolTipText, QColor("#e8ebf2"));
    p.setColor(QPalette::PlaceholderText, QColor("#7b8496"));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#5a6274"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#5a6274"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#5a6274"));
    qApp->setPalette(p);

    qApp->setStyleSheet(QString(R"(
QWidget { font-size: 10pt; }
QMainWindow, QScrollArea, QScrollArea > QWidget > QWidget, #mainStack { background: #0d0f13; }
QFrame#card QStackedWidget, QFrame#card QStackedWidget > QWidget { background: transparent; }
QRadioButton::indicator { width: 18px; height: 18px; border-radius: 11px; border: 2px solid #3a4357; background: #1b2029; }
QRadioButton::indicator:hover { border-color: #5a6680; }
QRadioButton::indicator:checked { border: 2px solid %1; background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 #ffffff, stop:0.42 #ffffff, stop:0.5 %1, stop:1 %1); }
#sidebar { background: #12151b; border-right: 1px solid #1e232d; }
#brand { font-size: 17pt; font-weight: 800; color: #ffffff; letter-spacing: 1px; }
#brandSub { color: #6f788b; font-size: 9pt; }
#sidebarTitle { color: #6f788b; font-size: 8.5pt; font-weight: 700; letter-spacing: 1px; }
QListWidget#devices { background: transparent; border: none; outline: 0; }
QListWidget#devices::item { border-radius: 12px; margin: 3px 10px; padding: 0px; }
QListWidget#devices::item:hover { background: #1a1f29; }
QListWidget#devices::item:selected { background: #1b2433; border: 1px solid %1; }
#devName { font-size: 10.5pt; font-weight: 700; color: #ffffff; background: transparent; }
#devStatus { color: #8a93a6; font-size: 9pt; background: transparent; }
#pageTitle { font-size: 22pt; font-weight: 800; color: #ffffff; }
#chip { background: #1b2030; border: 1px solid #2a3142; border-radius: 11px; padding: 3px 10px; color: #b9c1d3; font-size: 9pt; }
#chipGood { background: #10261c; border: 1px solid #1f5c3d; border-radius: 11px; padding: 3px 10px; color: #5fe39c; font-size: 9pt; }
QFrame#card { background: #151820; border: 1px solid #222837; border-radius: 16px; }
QFrame#card QLabel { background: transparent; }
#cardTitle { font-size: 11.5pt; font-weight: 700; color: #ffffff; }
#cardSub, #hint { color: #7b8496; }
QTabWidget::pane { border: none; top: -1px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: #7f889b; padding: 10px 20px; margin-right: 4px;
               font-weight: 700; font-size: 10.5pt; border: none; border-bottom: 3px solid transparent; }
QTabBar::tab:hover { color: #d6dbe6; }
QTabBar::tab:selected { color: #ffffff; border-bottom: 3px solid %1; }
QPushButton { background: #1f2430; border: 1px solid #2c3343; border-radius: 9px; padding: 7px 16px; color: #e8ebf2; }
QPushButton:hover { background: #262c3b; border-color: #3a4357; }
QPushButton:pressed { background: #1a1f29; }
QPushButton:disabled { color: #5a6274; }
QPushButton#primary { background: %1; border: none; color: #00121c; font-weight: 800; padding: 9px 22px; }
QPushButton#primary:hover { background: #33c4ff; }
QPushButton#primary:disabled { background: #1c3a4a; color: #4b6b7c; }
QPushButton#seg { border-radius: 0; padding: 7px 8px; }
QPushButton#seg:checked { background: %1; color: #00121c; font-weight: 800; border-color: %1; }
QComboBox, QSpinBox, QLineEdit, QKeySequenceEdit QLineEdit {
    background: #1b2029; border: 1px solid #2c3343; border-radius: 9px; padding: 6px 10px; color: #e8ebf2;
    selection-background-color: %1; selection-color: #00121c; }
QComboBox:hover, QSpinBox:hover, QLineEdit:hover { border-color: #3a4357; }
QComboBox:focus, QSpinBox:focus, QLineEdit:focus { border-color: %1; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: #1b2029; border: 1px solid #2c3343; selection-background-color: #24314a; outline: 0; }
QSpinBox::up-button, QSpinBox::down-button { width: 18px; border: none; background: transparent; }
QSlider::groove:horizontal { height: 6px; background: #262c3a; border-radius: 3px; }
QSlider::sub-page:horizontal { background: %1; border-radius: 3px; }
QSlider::handle:horizontal { background: #ffffff; width: 16px; height: 16px; margin: -5px 0; border-radius: 8px; }
QSlider::groove:vertical { width: 6px; background: #262c3a; border-radius: 3px; }
QSlider::add-page:vertical { background: %1; border-radius: 3px; }
QSlider::handle:vertical { background: #ffffff; width: 16px; height: 16px; margin: 0 -5px; border-radius: 8px; }
QSlider:disabled::sub-page:horizontal, QSlider:disabled::add-page:vertical { background: #2f3646; }
QProgressBar { background: #232836; border: none; border-radius: 4px; max-height: 8px; }
QProgressBar::chunk { background: #3ddc84; border-radius: 4px; }
QCheckBox { spacing: 8px; }
QRadioButton { spacing: 6px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #2a3040; border-radius: 5px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QMenuBar { background: #12151b; color: #b9c1d3; }
QMenuBar::item:selected { background: #1f2430; }
QMenu { background: #1b2029; border: 1px solid #2c3343; }
QMenu::item:selected { background: #24314a; }
#saveBar { background: #12151b; border-top: 1px solid #1e232d; }
)").arg(Accent));
}

QFrame *card(const QString &title, const QString &subtitle, QVBoxLayout **body, QWidget *headerRight)
{
    auto *f = new QFrame;
    f->setObjectName("card");
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(20, 16, 20, 18);
    v->setSpacing(10);
    auto *head = new QHBoxLayout;
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *t = new QLabel(title);
    t->setObjectName("cardTitle");
    texts->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto *s = new QLabel(subtitle);
        s->setObjectName("cardSub");
        s->setWordWrap(true);
        texts->addWidget(s);
    }
    head->addLayout(texts, 1);
    if (headerRight)
        head->addWidget(headerRight, 0, Qt::AlignTop);
    v->addLayout(head);
    if (body)
        *body = v;
    return f;
}

QLabel *hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("hint");
    l->setWordWrap(true);
    return l;
}

QLabel *chip(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("chip");
    return l;
}

namespace {
// Własne ikony urządzeń (nie zależą od motywu ikon systemu).
void drawDevice(QPainter &p, const QString &kind, const QRectF &r)
{
    const qreal w = r.width(), x0 = r.left(), y0 = r.top();
    auto X = [&](qreal f) { return x0 + f * w; };
    auto Y = [&](qreal f) { return y0 + f * w; };
    QLinearGradient body(r.topLeft(), r.bottomRight());
    body.setColorAt(0, QColor("#f3f6fb"));
    body.setColorAt(1, QColor("#9fb0c8"));
    QPen line(QColor("#0d1520"), qMax<qreal>(1.2, w * 0.035), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen glow(QColor(theme::Accent), qMax<qreal>(1.5, w * 0.06), Qt::SolidLine, Qt::RoundCap);
    if (kind.contains("mouse")) {
        QPainterPath m;
        m.addRoundedRect(QRectF(X(0.27), Y(0.08), w * 0.46, w * 0.84), w * 0.23, w * 0.23);
        p.setPen(Qt::NoPen);
        p.setBrush(body);
        p.drawPath(m);
        p.setPen(line);
        p.drawLine(QPointF(X(0.5), Y(0.08)), QPointF(X(0.5), Y(0.38)));
        p.drawLine(QPointF(X(0.28), Y(0.38)), QPointF(X(0.72), Y(0.38)));
        p.setPen(glow);
        p.drawLine(QPointF(X(0.5), Y(0.17)), QPointF(X(0.5), Y(0.28)));
        p.drawLine(QPointF(X(0.36), Y(0.74)), QPointF(X(0.64), Y(0.74)));
    } else if (kind.contains("headset")) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(body, w * 0.08, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(X(0.17), Y(0.1), w * 0.66, w * 0.7), 0 * 16, 180 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(body);
        p.drawRoundedRect(QRectF(X(0.1), Y(0.42), w * 0.2, w * 0.34), w * 0.08, w * 0.08);
        p.drawRoundedRect(QRectF(X(0.7), Y(0.42), w * 0.2, w * 0.34), w * 0.08, w * 0.08);
        p.setPen(QPen(body, w * 0.05, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath boom;
        boom.moveTo(X(0.2), Y(0.74));
        boom.quadTo(X(0.22), Y(0.92), X(0.44), Y(0.9));
        p.drawPath(boom);
        p.setPen(glow);
        p.drawLine(QPointF(X(0.2), Y(0.5)), QPointF(X(0.2), Y(0.68)));
        p.drawLine(QPointF(X(0.8), Y(0.5)), QPointF(X(0.8), Y(0.68)));
    } else { // mikrofon
        p.setPen(Qt::NoPen);
        p.setBrush(body);
        p.drawRoundedRect(QRectF(X(0.33), Y(0.06), w * 0.34, w * 0.52), w * 0.17, w * 0.17);
        p.setPen(QPen(body, w * 0.05, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(X(0.24), Y(0.22), w * 0.52, w * 0.5), 180 * 16, 180 * 16);
        p.drawLine(QPointF(X(0.5), Y(0.72)), QPointF(X(0.5), Y(0.88)));
        p.drawLine(QPointF(X(0.34), Y(0.9)), QPointF(X(0.66), Y(0.9)));
        p.setPen(line);
        for (qreal f : {0.2, 0.28, 0.36})
            p.drawLine(QPointF(X(0.38), Y(f)), QPointF(X(0.62), Y(f)));
        p.setPen(glow);
        p.drawLine(QPointF(X(0.44), Y(0.47)), QPointF(X(0.56), Y(0.47)));
    }
}
}

QPixmap deviceTile(const QString &iconName, int size)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, QColor("#1d2a3d"));
    g.setColorAt(1, QColor("#10161f"));
    QPainterPath path;
    path.addRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), size * 0.24, size * 0.24);
    p.fillPath(path, g);
    p.setPen(QPen(QColor(0, 180, 255, 90), 1));
    p.drawPath(path);
    // poświata w tle
    QRadialGradient halo(QPointF(size / 2.0, size * 0.6), size * 0.5);
    halo.setColorAt(0, QColor(0, 180, 255, 60));
    halo.setColorAt(1, QColor(0, 180, 255, 0));
    p.fillPath(path, halo);
    const qreal m = size * 0.18;
    drawDevice(p, iconName, QRectF(m, m, size - 2 * m, size - 2 * m));
    return pm;
}

} // namespace theme

Toggle::Toggle(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(44, 24);
}

void Toggle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect().adjusted(1, 1, -1, -1);
    const bool on = isChecked();
    QColor track = on ? QColor(theme::Accent) : QColor("#2a3040");
    if (!isEnabled())
        track = QColor("#20242e");
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const qreal d = r.height() - 6;
    const qreal x = on ? r.right() - d - 3 : r.left() + 3;
    p.setBrush(isEnabled() ? QColor("#ffffff") : QColor("#5a6274"));
    p.drawEllipse(QRectF(x, r.top() + 3, d, d));
}
