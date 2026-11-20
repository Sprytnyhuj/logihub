#include "hidpp.h"

#include <QFile>
#include <QTest>

using namespace hidpp;

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
