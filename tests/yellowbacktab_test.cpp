// Placeholder QTest for the Yellowback tab (plan §4.7 "Testing the wallet", H1/H5).
//
// Runs under QT_QPA_PLATFORM=offscreen. Today it instantiates the tab without a node and
// checks the pure helpers; the end-to-end case against the §6.0 playground
// (--conf <tmpdir>/node0/ycash.conf --no-embedded) is added once the node RPCs are frozen.

#include <QtTest>
#include <QTabWidget>

#include "yellowbacktab.h"
#include "yellowbackmodels.h"
#include "settings.h"

class YellowbackTabTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ycash-foundation");
        QCoreApplication::setApplicationName("yecwallet-yellowback-test");
        Settings::init();
        Settings::getInstance()->setHeadless(true);
    }

    void instantiatesOffscreen() {
        YellowbackTab tab(nullptr);
        auto sub = tab.findChild<QTabWidget*>("subTabs");
        QVERIFY(sub != nullptr);
        QCOMPARE(sub->count(), (int)YellowbackTab::PageCount);
        QCOMPARE(sub->tabText(YellowbackTab::Overview), QString("Overview"));
        QCOMPARE(sub->tabText(YellowbackTab::Settings), QString("Settings"));
        tab.show();
        QVERIFY(tab.isVisible());
        QVERIFY(tab.controller() == nullptr);
        // With no controller every action is disabled and the banner is up
        auto banner = tab.findChild<QLabel*>("lblBanner");
        QVERIFY(banner != nullptr && !banner->isHidden());
    }

    void parsesDollars() {
        qint64 c = 0;
        QVERIFY(YellowbackTab::parseDollars("12.34", &c));  QCOMPARE(c, (qint64)1234);
        QVERIFY(YellowbackTab::parseDollars("12", &c));     QCOMPARE(c, (qint64)1200);
        QVERIFY(YellowbackTab::parseDollars("12.5", &c));   QCOMPARE(c, (qint64)1250);
        QVERIFY(YellowbackTab::parseDollars("$1,234.56", &c)); QCOMPARE(c, (qint64)123456);
        QVERIFY(!YellowbackTab::parseDollars("abc", &c));
        QVERIFY(!YellowbackTab::parseDollars("1.234", &c));
        QVERIFY(!YellowbackTab::parseDollars("-5", &c));
    }

    void formatsAmounts() {
        Settings::getInstance()->setYellowbackUnitCents(false);
        QCOMPARE(YellowbackFormat::cents(1234), QString("$12.34"));
        QCOMPARE(YellowbackFormat::cents(-5), QString("-$0.05"));
        QCOMPARE(YellowbackFormat::cents(1234, true), QString::fromUtf8("1234 ¢"));
        QCOMPARE(YellowbackFormat::tierName(0), QString("1 hour"));
        QCOMPARE(YellowbackFormat::tierRatio(4), QString("300 %"));
        QCOMPARE(YellowbackFormat::heightWithEstimate(0, 100), QString("-"));
    }

    void parsesRecords() {
        auto j = nlohmann::json::parse(R"({"vaultTxid":"ab","status":"ACTIVE","mintedCents":10000,
            "collateralZat":123,"lockHeight":5,"tier":1,"rosterIndex":0,"canRedeem":true,
            "requiredBurnCents":10000,"unlockHeight":41,"ownerKeyId":"k"})");
        auto p = YellowbackPosition::fromJson(j);
        QCOMPARE(p.vaultTxid, QString("ab"));
        QCOMPARE(p.mintedCents, (qint64)10000);
        QVERIFY(p.canRedeem);
        QCOMPARE(p.unlockHeight, 41);
        // Missing fields degrade to defaults, never throw
        auto t = YellowbackTx::fromJson(nlohmann::json::object());
        QVERIFY(t.txid.isEmpty());
        QCOMPARE(t.confirmations, 0);
    }
};

QTEST_MAIN(YellowbackTabTest)
#include "yellowbacktab_test.moc"
