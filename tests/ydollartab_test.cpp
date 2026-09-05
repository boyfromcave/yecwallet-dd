// Placeholder QTest for the YDollar tab (plan §4.7 "Testing the wallet", H1/H5).
//
// Runs under QT_QPA_PLATFORM=offscreen. Today it instantiates the tab without a node and
// checks the pure helpers; the end-to-end case against the §6.0 playground
// (--conf <tmpdir>/node0/ycash.conf --no-embedded) is added once the node RPCs are frozen.

#include <QtTest>
#include <QTabWidget>

#include "ydollartab.h"
#include "ydollarmodels.h"
#include "settings.h"

class YDollarTabTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ycash-foundation");
        QCoreApplication::setApplicationName("yecwallet-ydollar-test");
        Settings::init();
        Settings::getInstance()->setHeadless(true);
    }

    void instantiatesOffscreen() {
        YDollarTab tab(nullptr);
        auto sub = tab.findChild<QTabWidget*>("subTabs");
        QVERIFY(sub != nullptr);
        QCOMPARE(sub->count(), (int)YDollarTab::PageCount);
        QCOMPARE(sub->tabText(YDollarTab::Overview), QString("Overview"));
        QCOMPARE(sub->tabText(YDollarTab::Settings), QString("Settings"));
        tab.show();
        QVERIFY(tab.isVisible());
        QVERIFY(tab.controller() == nullptr);
        // With no controller every action is disabled and the banner is up
        auto banner = tab.findChild<QLabel*>("lblBanner");
        QVERIFY(banner != nullptr && !banner->isHidden());
    }

    void parsesDollars() {
        qint64 c = 0;
        QVERIFY(YDollarTab::parseDollars("12.34", &c));  QCOMPARE(c, (qint64)1234);
        QVERIFY(YDollarTab::parseDollars("12", &c));     QCOMPARE(c, (qint64)1200);
        QVERIFY(YDollarTab::parseDollars("12.5", &c));   QCOMPARE(c, (qint64)1250);
        QVERIFY(YDollarTab::parseDollars("$1,234.56", &c)); QCOMPARE(c, (qint64)123456);
        QVERIFY(!YDollarTab::parseDollars("abc", &c));
        QVERIFY(!YDollarTab::parseDollars("1.234", &c));
        QVERIFY(!YDollarTab::parseDollars("-5", &c));
    }

    void formatsAmounts() {
        Settings::getInstance()->setYDollarUnitCents(false);
        QCOMPARE(YDollarFormat::cents(1234), QString("$12.34"));
        QCOMPARE(YDollarFormat::cents(-5), QString("-$0.05"));
        QCOMPARE(YDollarFormat::cents(1234, true), QString::fromUtf8("1234 ¢"));
        QCOMPARE(YDollarFormat::tierName(0), QString("1 hour"));
        QCOMPARE(YDollarFormat::tierRatio(4), QString("300 %"));
        QCOMPARE(YDollarFormat::heightWithEstimate(0, 100), QString("-"));
    }

    void parsesRecords() {
        auto j = nlohmann::json::parse(R"({"vaultTxid":"ab","status":"ACTIVE","mintedCents":10000,
            "collateralZat":123,"lockHeight":5,"tier":1,"rosterIndex":0,"canRedeem":true,
            "requiredBurnCents":10000,"unlockHeight":41,"ownerKeyId":"k"})");
        auto p = YDollarPosition::fromJson(j);
        QCOMPARE(p.vaultTxid, QString("ab"));
        QCOMPARE(p.mintedCents, (qint64)10000);
        QVERIFY(p.canRedeem);
        QCOMPARE(p.unlockHeight, 41);
        // Missing fields degrade to defaults, never throw
        auto t = YDollarTx::fromJson(nlohmann::json::object());
        QVERIFY(t.txid.isEmpty());
        QCOMPARE(t.confirmations, 0);
    }
};

QTEST_MAIN(YDollarTabTest)
#include "ydollartab_test.moc"
