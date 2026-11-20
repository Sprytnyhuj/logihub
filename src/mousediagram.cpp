#include "mousediagram.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace {
constexpr qreal kLabelW = 180;
constexpr qreal kLabelH = 44;
const QColor kAccent(theme::Accent);
}

MouseDiagram::MouseDiagram(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MouseDiagram::setButtons(const QStringList &names, const QStringList &actions)
{
    m_names = names;
    m_actions = actions;
    update();
}

void MouseDiagram::setSelected(int index)
{
    m_selected = index;
    update();
}

QPointF MouseDiagram::map(qreal x, qreal y) const
{
    return {m_box.left() + x * m_box.width(), m_box.top() + y * m_box.height()};
}

QRectF MouseDiagram::mapRect(qreal cx, qreal cy, qreal w, qreal h) const
{
    const QPointF c = map(cx, cy);
    return QRectF(c.x() - w * m_box.width() / 2, c.y() - h * m_box.height() / 2, w * m_box.width(),
                  h * m_box.height());
}

QPainterPath MouseDiagram::body() const
{
    // Sylwetka z góry: węższy przód, wybrzuszenie pod kciuk po lewej.
    QPainterPath p;
    p.moveTo(map(0.50, 0.00));
    p.cubicTo(map(0.70, 0.00), map(0.84, 0.05), map(0.86, 0.18));
    p.cubicTo(map(0.90, 0.40), map(0.92, 0.62), map(0.86, 0.82));
    p.cubicTo(map(0.80, 0.97), map(0.62, 1.00), map(0.50, 1.00));
    p.cubicTo(map(0.36, 1.00), map(0.22, 0.97), map(0.17, 0.86));
    p.cubicTo(map(0.13, 0.78), map(0.02, 0.72), map(0.03, 0.58));
    p.cubicTo(map(0.04, 0.46), map(0.12, 0.42), map(0.13, 0.34));
    p.cubicTo(map(0.14, 0.20), map(0.24, 0.00), map(0.50, 0.00));
    p.closeSubpath();
    return p;
}

void MouseDiagram::resizeEvent(QResizeEvent *)
{
    layoutSpots();
}

void MouseDiagram::layoutSpots()
{
    const qreal h = height() - 16;
    const qreal w = qMin(h * 0.6, width() - 2 * (kLabelW + 40));
    m_box = QRectF((width() - w) / 2, (height() - w / 0.6) / 2, w, w / 0.6);
    m_spots.clear();

    auto rounded = [&](qreal cx, qreal cy, qreal w, qreal h, qreal r) {
        QPainterPath p;
        const QRectF rc = mapRect(cx, cy, w, h);
        p.addRoundedRect(rc, rc.width() * r, rc.width() * r);
        return p;
    };
    const QPainterPath b = body();

    // Główne przyciski: górna część korpusu podzielona na pół.
    QPainterPath top;
    top.moveTo(map(0.0, 0.0));
    top.lineTo(map(1.0, 0.0));
    top.lineTo(map(1.0, 0.40));
    top.cubicTo(map(0.70, 0.43), map(0.30, 0.43), map(0.0, 0.40));
    top.closeSubpath();
    QPainterPath leftHalf, rightHalf;
    leftHalf.addRect(QRectF(map(0, 0), map(0.5, 1)));
    rightHalf.addRect(QRectF(map(0.5, 0), map(1, 1)));
    const QPainterPath lmb = b.intersected(top).intersected(leftHalf);
    const QPainterPath rmb = b.intersected(top).intersected(rightHalf);

    struct Def { int index; QPainterPath shape; bool left; };
    QList<Def> defs = {
        {9, rounded(0.25, 0.15, 0.075, 0.075, 0.35), true},   // G8 DPI w górę
        {10, rounded(0.27, 0.25, 0.075, 0.075, 0.35), true},  // G7 DPI w dół
        {6, rounded(0.43, 0.18, 0.035, 0.09, 0.5), true},     // kółko w lewo
        {7, rounded(0.57, 0.18, 0.035, 0.09, 0.5), false},    // kółko w prawo
        {2, rounded(0.50, 0.18, 0.085, 0.17, 0.5), false},    // kółko
        {8, rounded(0.50, 0.335, 0.06, 0.04, 0.5), false},    // G9 za kółkiem
        {4, rounded(0.15, 0.43, 0.06, 0.07, 0.5), true},      // G6 snajperski
        {5, rounded(0.085, 0.53, 0.05, 0.085, 0.5), true},    // G5 przód
        {3, rounded(0.075, 0.64, 0.05, 0.085, 0.5), true},    // G4 tył
        {0, lmb, true},
        {1, rmb, false},
    };
    for (const Def &d : defs) {
        Spot s;
        s.index = d.index;
        s.shape = d.shape;
        s.left = d.left;
        // kotwica: środek kształtu (dla dużych przycisków - bliżej krawędzi)
        // kotwice dobrane tak, by kolejność w pionie = kolejność podpisów (linie się nie krzyżują)
        s.anchor = d.shape.boundingRect().center();
        if (d.index == 0)
            s.anchor = map(0.30, 0.345);
        else if (d.index == 1)
            s.anchor = map(0.72, 0.28);
        else if (d.index == 2)
            s.anchor = map(0.50, 0.11);
        m_spots << s;
    }
    // Podpisy: każda strona osobno, posortowane po wysokości kotwicy, równo rozłożone.
    for (bool left : {true, false}) {
        QList<Spot *> side;
        for (Spot &s : m_spots)
            if (s.left == left)
                side << &s;
        std::sort(side.begin(), side.end(), [](Spot *a, Spot *b) { return a->anchor.y() < b->anchor.y(); });
        const int n = int(side.size());
        const qreal spacing = n > 1 ? qMin((height() - 16 - kLabelH) / (n - 1), kLabelH + 18) : 0;
        const qreal startY = (height() - ((n - 1) * spacing + kLabelH)) / 2;
        for (int i = 0; i < n; ++i) {
            const qreal x = left ? 8 : width() - kLabelW - 8;
            side[i]->label = QRectF(x, startY + i * spacing, kLabelW, kLabelH);
        }
    }
}

int MouseDiagram::hit(const QPointF &p) const
{
    for (const Spot &s : m_spots) // kolejność: najmniejsze elementy pierwsze
        if (s.shape.contains(p) || s.label.contains(p))
            return s.index;
    return -1;
}

void MouseDiagram::mouseMoveEvent(QMouseEvent *e)
{
    const int h = hit(e->position());
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void MouseDiagram::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void MouseDiagram::mousePressEvent(QMouseEvent *e)
{
    const int h = hit(e->position());
    if (h >= 0) {
        m_selected = h;
        update();
        emit buttonClicked(h);
    }
}

void MouseDiagram::paintEvent(QPaintEvent *)
{
    if (m_spots.isEmpty())
        layoutSpots();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Poświata pod myszą
    QRadialGradient halo(m_box.center(), m_box.height() * 0.6);
    halo.setColorAt(0, QColor(0, 180, 255, 40));
    halo.setColorAt(1, QColor(0, 180, 255, 0));
    p.fillRect(rect(), halo);

    // Korpus
    const QPainterPath b = body();
    QLinearGradient bodyGrad(m_box.topLeft(), m_box.bottomRight());
    bodyGrad.setColorAt(0, QColor("#2b3240"));
    bodyGrad.setColorAt(0.5, QColor("#1b2029"));
    bodyGrad.setColorAt(1, QColor("#11151c"));
    p.setPen(QPen(QColor("#3a4357"), 1.5));
    p.setBrush(bodyGrad);
    p.drawPath(b);
    // odbłysk na grzbiecie
    QRadialGradient shine(map(0.55, 0.58), m_box.width() * 0.32);
    shine.setColorAt(0, QColor(255, 255, 255, 14));
    shine.setColorAt(1, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shine);
    p.drawPath(b);
    // pasek podświetlenia
    QLinearGradient light(map(0.36, 0), map(0.64, 0));
    light.setColorAt(0, QColor(0, 180, 255, 0));
    light.setColorAt(0.5, QColor(0, 200, 255, 230));
    light.setColorAt(1, QColor(0, 180, 255, 0));
    p.setPen(QPen(QBrush(light), m_box.width() * 0.02, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(map(0.38, 0.70), map(0.62, 0.70));

    // Strefy przycisków
    for (const Spot &s : m_spots) {
        const bool sel = s.index == m_selected, hov = s.index == m_hover;
        const bool big = s.index == 0 || s.index == 1;
        QColor fill = big ? QColor(255, 255, 255, 6) : QColor("#3a4357");
        if (s.index == 4)
            fill = QColor("#7a2430"); // przycisk snajperski wyróżniony
        if (hov)
            fill = big ? QColor(0, 180, 255, 40) : QColor("#4b5a74");
        if (sel)
            fill = big ? QColor(0, 180, 255, 70) : kAccent;
        p.setPen(QPen(sel ? kAccent : QColor("#4a5468"), sel ? 2 : 1));
        p.setBrush(fill);
        p.drawPath(s.shape);
    }
    // rowki kółka
    if (const auto it = std::find_if(m_spots.begin(), m_spots.end(), [](const Spot &s) { return s.index == 2; });
        it != m_spots.end()) {
        const QRectF w = it->shape.boundingRect();
        p.setPen(QPen(QColor(0, 0, 0, 90), 1));
        for (int i = 1; i < 8; ++i) {
            const qreal y = w.top() + w.height() * i / 8.0;
            p.drawLine(QPointF(w.left() + 3, y), QPointF(w.right() - 3, y));
        }
    }

    // Podpisy z liniami
    QFont nameFont = font();
    nameFont.setPointSizeF(8.5);
    QFont actFont = font();
    actFont.setPointSizeF(9.5);
    actFont.setBold(true);
    for (const Spot &s : m_spots) {
        const bool sel = s.index == m_selected, hov = s.index == m_hover;
        const QColor lineColor = sel ? kAccent : hov ? QColor("#8a96ad") : QColor("#465064");
        const QPointF edge = s.left ? QPointF(s.label.right(), s.label.center().y())
                                    : QPointF(s.label.left(), s.label.center().y());
        const QPointF elbow(s.left ? edge.x() + 18 : edge.x() - 18, edge.y());
        p.setPen(QPen(lineColor, sel ? 2 : 1.2));
        p.setBrush(Qt::NoBrush);
        QPainterPath line;
        line.moveTo(edge);
        line.lineTo(elbow);
        line.lineTo(s.anchor);
        p.drawPath(line);
        p.setBrush(lineColor);
        p.drawEllipse(s.anchor, 3, 3);

        // karta podpisu
        p.setPen(QPen(sel ? kAccent : hov ? QColor("#3a4357") : QColor("#262c3a"), sel ? 1.5 : 1));
        p.setBrush(sel ? QColor("#14263a") : QColor("#151820"));
        p.drawRoundedRect(s.label, 9, 9);
        const QRectF inner = s.label.adjusted(12, 4, -10, -4);
        p.setFont(nameFont);
        p.setPen(QColor("#7b8496"));
        p.drawText(QRectF(inner.left(), inner.top(), inner.width(), inner.height() / 2),
                   Qt::AlignLeft | Qt::AlignBottom, m_names.value(s.index).toUpper());
        p.setFont(actFont);
        p.setPen(sel ? kAccent : QColor("#e8ebf2"));
        const QString act = QFontMetrics(actFont).elidedText(m_actions.value(s.index), Qt::ElideRight,
                                                             int(inner.width()));
        p.drawText(QRectF(inner.left(), inner.center().y(), inner.width(), inner.height() / 2),
                   Qt::AlignLeft | Qt::AlignTop, act);
    }
}
