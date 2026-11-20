#pragma once
// Wygląd w stylu G Hub: ciemne tło, karty, niebieski akcent, przełączniki.
#include <QAbstractButton>
#include <QFrame>

class QLabel;
class QVBoxLayout;

namespace theme {
constexpr const char *Accent = "#00b4ff";
void apply();
// Karta z tytułem i opcjonalnym opisem; zwraca kartę, layout treści w *body.
QFrame *card(const QString &title, const QString &subtitle = {}, QVBoxLayout **body = nullptr,
             QWidget *headerRight = nullptr);
QLabel *hint(const QString &text);
QLabel *chip(const QString &text);
// Ikona urządzenia w kafelku z gradientem.
QPixmap deviceTile(const QString &iconName, int size);
}

// Przełącznik wł./wył. jak w G Hub.
class Toggle : public QAbstractButton {
    Q_OBJECT
public:
    explicit Toggle(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {44, 24}; }
protected:
    void paintEvent(QPaintEvent *) override;
};
