#pragma once
// Rysunek myszy (widok z góry, kształt w stylu G502) z podpisami przycisków - jak w G Hub.
// Własna ilustracja wektorowa, bez grafik producenta.
#include <QPainterPath>
#include <QWidget>

class MouseDiagram : public QWidget {
    Q_OBJECT
public:
    explicit MouseDiagram(QWidget *parent = nullptr);
    // names[i] - nazwa przycisku i, actions[i] - aktualne przypisanie (tekst)
    void setButtons(const QStringList &names, const QStringList &actions);
    void setSelected(int index);
    int selected() const { return m_selected; }
    QSize sizeHint() const override { return {820, 540}; }
    QSize minimumSizeHint() const override { return {640, 540}; }
signals:
    void buttonClicked(int index);
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    struct Spot {
        int index;
        QPainterPath shape;  // w pikselach
        QPointF anchor;
        bool left;           // podpis po lewej stronie
        QRectF label;
    };
    void layoutSpots();
    int hit(const QPointF &p) const;
    QPainterPath body() const;
    QPointF map(qreal x, qreal y) const;
    QRectF mapRect(qreal cx, qreal cy, qreal w, qreal h) const;

    QStringList m_names, m_actions;
    QList<Spot> m_spots;
    QRectF m_box; // prostokąt rysunku myszy
    int m_hover = -1;
    int m_selected = 0;
};
