// Offline QTest for the Yellowback tab (plan §4.8 "Offline QTest cases", N28; Phase 7b-a).
//
// Runs under QT_QPA_PLATFORM=offscreen with no node: a bare YellowbackController(nullptr,
// nullptr) is fed canned JSON through YellowbackController::feed(), which applies the replies
// exactly as the RPC callbacks would. The canned values are the example values of
// docs/yellowback-rpc-contract.json (the generated copy of ycash-dd/doc/yellowback-rpc.md), so
// a contract change that renames a field shows up here as well as in check-rpc-contract.py.
//
// Anything that needs a node (the devnet end-to-end case, Phase 7b-b) reads
// YELLOWBACK_DEVNET_DIR and QSKIPs when it is unset; CI runs only the offline cases.

#include <QtTest>
#include <QTabWidget>

#include "yellowbacktab.h"
#include "yellowbackmodels.h"
#include "yellowbackcontroller.h"
#include "yellowbackrpc.h"
#include "settings.h"

using json = nlohmann::json;

// ── Canned replies (contract example values) ──────────────────────────────────────────────

static json infoActive() {
    return json::parse(R"({
      "rpcversion": 2, "enabled": true, "network": "regtest", "height": 331,
      "blockhash": "0f3a9c1e5b7d2a4c6e8f0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f",
      "chainHeight": 331, "startHeight": 1, "healthy": true, "unhealthyReason": "",
      "enforcing": true, "valveTripped": false, "sunset": false, "rejectedBlocks": 0,
      "suppressedBlocks": 0, "templatePolicy": "strict", "abandoned": false,
      "activation": {"status": "active", "lockInHeight": 129, "activateHeight": 193, "signalCount": 64, "window": 64},
      "miner": {"payoutAddress": "smQvTmAz2ExamplePayoutAddress1111111", "signal": true, "quoteKind": "quote",
                "quoteAgeSeconds": 12, "registered": true, "eligible": true},
      "params": {"startHeight": 1, "enforceUntilHeight": 0, "sigmaRefBps": 0, "supplyCapBps": 0, "refLag": 2,
                 "refWindow": 40, "grace": 24, "payeeWindow": 10, "feeMinZat": 50000000, "feeBps": 25,
                 "tokenValueZat": 10000, "feeZat": 1000, "valveBlocks": 6, "abandonBlocks": 128,
                 "windows": {"fast": 8, "mid": 24, "slow": 64, "signal": 64},
                 "minFill": {"fast": 4, "mid": 16, "slow": 43},
                 "classes": [{"class": "A", "minBlocks": 48, "maxBlocks": 96, "baseRatioBps": 50000},
                             {"class": "B", "minBlocks": 97, "maxBlocks": 144, "baseRatioBps": 40000},
                             {"class": "C", "minBlocks": 145, "maxBlocks": 240, "baseRatioBps": 30000}],
                 "policy": {"penaltyBlocks": 12, "accuracyWindow": 24, "tiltBps": 10000, "preferredPayee": null}}
    })");
}

static json statsOpen() {
    return json::parse(R"({
      "height": 331, "supplyCents": 250000, "collateralZat": 62500000000, "activeVaults": 3, "voidVaults": 1,
      "closedVaults": 2, "claimedVaults": 0, "unbackedCents": 0, "issuedZat": 1656250000000,
      "pFast": 2000000, "pMid": 2000000, "pSlow": 1990000, "pMint": 1990000, "pClaim": 2000000,
      "sigmaMultBps": 10000, "globalRatioBps": 49750, "supplyCapCents": null, "haltMask": [], "mintingAllowed": true
    })");
}

static json activationActive() {
    return json::parse(R"({
      "status": "active", "lockInHeight": 129, "activateHeight": 193, "signalCount": 64, "window": 64,
      "threshold": 48, "participationFloor": 39, "enforcementFloor": 32, "enforcementResume": 39,
      "mintHalted": false, "enforcementSuspended": false, "enforcing": true, "valveTripped": false,
      "sunset": false, "enforceUntilHeight": 0, "history": [{"height": 331, "signalCount": 64}]
    })");
}

static json positionActive() {
    return json::parse(R"({
      "txid": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "vout": 0, "status": "ACTIVE",
      "ownerPubKey": "02a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f9",
      "ownerKeyId": "1f2e3d4c5b6a79880706050403020100f1e2d3c4", "ownerAddress": "yrExampleOwnerAddress111111111111111",
      "termClass": "A", "lockHeight": 380, "claimHeight": 404, "collateralZat": 25125628141, "collateral": 251.25628141,
      "mintedCents": 100000, "mintHeight": 332, "refHeight": 329, "feePaidZat": 62814071, "closeHeight": null,
      "closingTxid": "", "burnedCents": 0, "unbacked": false, "claimable": false, "underwaterAt": 437800,
      "voidReason": "", "canRedeem": false, "canClaim": false, "canSweep": false
    })");
}

static json claimableRow() {
    return json::parse(R"({
      "vault": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0",
      "ownerAddress": "yrExampleOwnerAddress111111111111111", "collateralZat": 25125628141, "mintedCents": 100000,
      "feeZat": 62814071, "claimHeight": 404, "underwaterAt": 437800, "pClaim": 400000
    })");
}

static json txMint() {
    return json::parse(R"({
      "txid": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "height": 332, "confirmations": 1,
      "type": "mint", "verdict": "ok", "path": "", "yedIn": 0, "yedOut": 100000, "burned": 0, "amountCents": 100000,
      "feeZat": 62814071, "payee": "smQvTmAz2ExamplePayoutAddress1111111", "unbacked": false, "expired": false
    })");
}

// A tab with a controller that is fed the given replies; labels are found by object name.
struct Harness {
    YellowbackTab        tab{nullptr};
    YellowbackController ctl{nullptr, nullptr};
    Harness() { tab.setController(&ctl); }
    void feed(const json& info, const json& stats = json(nullptr), const json& activation = json(nullptr),
              const json& positions = json(nullptr), const json& claimable = json(nullptr)) {
        ctl.feed(info, stats, activation, json(nullptr), positions, claimable, json(nullptr));
    }
    QString label(const char* name) {
        auto l = tab.findChild<QLabel*>(name);
        return l == nullptr ? QString("<no label %1>").arg(name) : l->text();
    }
    bool visible(const char* name) {
        auto l = tab.findChild<QLabel*>(name);
        return l != nullptr && !l->isHidden();
    }
    QPushButton* button(const char* name) { return tab.findChild<QPushButton*>(name); }
};

class YellowbackTabTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ycash-foundation");
        QCoreApplication::setApplicationName("yecwallet-yellowback-test");
        Settings::init();
        Settings::getInstance()->setHeadless(true);
        Settings::getInstance()->setYellowbackUnitCents(false);
    }

    // ── Structure ─────────────────────────────────────────────────────────────────────────

    void instantiatesOffscreen() {
        YellowbackTab tab(nullptr);
        auto sub = tab.findChild<QTabWidget*>("subTabs");
        QVERIFY(sub != nullptr);
        QCOMPARE(sub->count(), (int)YellowbackTab::PageCount);
        QCOMPARE(sub->tabText(YellowbackTab::Overview), QString("Overview"));
        QCOMPARE(sub->tabText(YellowbackTab::Claim), QString("Claim"));
        QCOMPARE(sub->tabText(YellowbackTab::Settings), QString("Settings"));
        tab.show();
        QVERIFY(tab.isVisible());
        QVERIFY(tab.controller() == nullptr);
        // With no controller every action is disabled and the banner is up
        auto banner = tab.findChild<QLabel*>("lblBanner");
        QVERIFY(banner != nullptr && !banner->isHidden());
    }

    void contractVersionIsTwo() {
        QCOMPARE(YellowbackRpc::RPC_VERSION, 2);
        QCOMPARE(Settings::getYellowbackRpcVersion(), 2);
    }

    // ── Helpers and records ───────────────────────────────────────────────────────────────

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
        QCOMPARE(YellowbackFormat::cents(1234), QString("$12.34"));
        QCOMPARE(YellowbackFormat::cents(-5), QString("-$0.05"));
        QCOMPARE(YellowbackFormat::cents(1234, true), QString::fromUtf8("1234 ¢"));
        QCOMPARE(YellowbackFormat::price(1990000), QString("$1.9900"));
        QCOMPARE(YellowbackFormat::bpsAsMultiplier(10000), QString("1.00x"));
        QCOMPARE(YellowbackFormat::bpsAsPercent(49750), QString("497.50 %"));
        QCOMPARE(YellowbackFormat::heightWithEstimate(0, 100), QString("-"));
        // null prices render as "undefined" (§4.8 Overview row)
        QCOMPARE(YellowbackFormat::priceOrUndefined(json::parse(R"({"pClaim": null})"), "pClaim"), QString("undefined"));
        QCOMPARE(YellowbackFormat::priceOrUndefined(json::parse(R"({"pClaim": 2000000})"), "pClaim"), QString("$2.0000"));
    }

    void parsesRecords() {
        auto p = YellowbackPosition::fromJson(positionActive());
        QCOMPARE(p.txid, QString("6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"));
        QCOMPARE(p.vaultName(), p.txid % ":0");
        QCOMPARE(p.mintedCents, (qint64)100000);
        QCOMPARE(p.termClass, QString("A"));
        QCOMPARE(p.lockHeight, 380);
        QCOMPARE(p.claimHeight, 404);
        QCOMPARE(p.closeHeight, -1);          // null
        QCOMPARE(p.sweepBefore, -1);          // optional, absent
        QCOMPARE(p.underwaterAt, (qint64)437800);
        QVERIFY(!p.canRedeem && !p.canClaim && !p.canSweep);

        auto c = YellowbackClaimable::fromJson(claimableRow());
        QCOMPARE(c.vault.right(2), QString(":0"));
        QCOMPARE(c.feeZat, (qint64)62814071);
        QCOMPARE(c.pClaim, (qint64)400000);

        auto t = YellowbackTx::fromJson(txMint());
        QCOMPARE(t.type, QString("mint"));
        QCOMPARE(t.payee, QString("smQvTmAz2ExamplePayoutAddress1111111"));
        QVERIFY(!t.unbacked && !t.expired);

        // Missing fields degrade to defaults, never throw
        auto e = YellowbackTx::fromJson(json::object());
        QVERIFY(e.txid.isEmpty());
        QCOMPARE(e.confirmations, 0);
        auto q = YellowbackPosition::fromJson(json::object());
        QVERIFY(q.status.isEmpty());
    }

    void recognisesErrorIdentifiers() {
        QVERIFY(YellowbackController::isMethodNotFound("Method not found (Yellowback requires -experimentalfeatures -yellowback)"));
        QVERIFY(YellowbackController::isIndexUnhealthy("yellowback-unhealthy: storage: commit failed; restart with -reindex-yellowback"));
        QVERIFY(!YellowbackController::isIndexUnhealthy("vault-locked: tip 300 below lockHeight 380"));
    }

    // ── Status banner (§4.8 banner row), one case per state ───────────────────────────────

    void bannerUnavailableWhenUnhealthy() {
        Harness h;
        json info = infoActive();
        info["healthy"] = false; info["unhealthyReason"] = "storage: commit failed";
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(!h.ctl.isAvailable());
        QVERIFY(h.visible("lblBanner"));
        QVERIFY(!h.visible("lblStatus"));
        QVERIFY(h.label("lblBanner").startsWith("Yellowback unavailable:"));
        QVERIFY(h.label("lblBanner").contains("storage: commit failed"));
        QVERIFY(h.label("lblBanner").contains("-reindex-yellowback"));
    }

    void bannerUnavailableOnVersionMismatch() {
        Harness h;
        json info = infoActive();
        info["rpcversion"] = 1;
        h.feed(info);
        QVERIFY(!h.ctl.isAvailable());
        QVERIFY(h.label("lblBanner").contains("version 2"));
        QVERIFY(h.label("lblBanner").contains("version 1"));
    }

    void bannerUnavailableWhileNotAtTip() {
        // v2 has no `synced`: the index is at the tip iff height == chainHeight
        Harness h;
        json info = infoActive();
        info["height"] = 300;
        h.feed(info);
        QVERIFY(!h.ctl.isSynced());
        QVERIFY(!h.ctl.isAvailable());
        QVERIFY(h.label("lblBanner").contains("300"));
        QVERIFY(h.label("lblBanner").contains("331"));
    }

    void bannerActive() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive());
        QVERIFY(h.ctl.isAvailable());
        QVERIFY(!h.visible("lblBanner"));           // no warnings
        QVERIFY(h.visible("lblStatus"));
        QVERIFY(h.label("lblStatus").contains("active since height 193"));
        QVERIFY(h.label("lblStatus").contains("64/64"));
        // The connected node is a pool with a fresh quote: an information line, not a warning
        QVERIFY(h.visible("lblNotes"));
        QVERIFY(h.label("lblNotes").contains("smQvTmAz2ExamplePayoutAddress1111111"));
        QVERIFY(h.label("lblNotes").contains("price quote"));
        QVERIFY(h.label("lblNotes").contains("eligible for enforcement fees"));
        // Copy rule (§4.8): never "trustless", and enforcement is described, not guaranteed
        QVERIFY(!h.label("lblStatus").contains("trustless", Qt::CaseInsensitive));
    }

    void bannerSignaling() {
        Harness h;
        json info = infoActive();
        info["activation"] = json::parse(R"({"status": "signaling", "lockInHeight": 0, "activateHeight": 0, "signalCount": 40, "window": 64})");
        info["enforcing"] = false;
        json act = activationActive();
        act["status"] = "signaling"; act["signalCount"] = 40; act["mintHalted"] = true;
        h.feed(info, statsOpen(), act);
        QVERIFY(h.ctl.isAvailable());
        QVERIFY(h.label("lblStatus").contains("signalling"));
        QVERIFY(h.label("lblStatus").contains("40/64"));
        QVERIFY(h.label("lblStatus").contains("needs 48"));
        QVERIFY(!h.visible("lblBanner"));           // not enforcing before activation is not a warning
    }

    void bannerLockedIn() {
        Harness h;
        json info = infoActive();
        info["activation"] = json::parse(R"({"status": "locked_in", "lockInHeight": 129, "activateHeight": 193, "signalCount": 60, "window": 64})");
        h.feed(info, statsOpen(), json(nullptr));
        QVERIFY(h.label("lblStatus").contains("locked in"));
        QVERIFY(h.label("lblStatus").contains("193"));
    }

    void bannerEnforcementSuspended() {
        Harness h;
        json act = activationActive();
        act["enforcementSuspended"] = true; act["mintHalted"] = true; act["signalCount"] = 20;
        json stats = statsOpen();
        stats["haltMask"] = json::array({"PARTICIPATION", "ENFORCEMENT"}); stats["mintingAllowed"] = false;
        h.feed(infoActive(), stats, act);
        QVERIFY(h.visible("lblBanner"));
        QVERIFY(h.label("lblBanner").contains("Enforcement suspended"));
        QVERIFY(h.label("lblBanner").contains("not policed"));
        // The overview names the halt reasons
        QVERIFY(h.label("lblMintStatus").contains("PARTICIPATION"));
        QVERIFY(h.label("lblMintStatus").contains("ENFORCEMENT"));
        QVERIFY(h.label("lblEnforcement").contains("suspended"));
    }

    void bannerParticipationHalt() {
        Harness h;
        json stats = statsOpen();
        stats["haltMask"] = json::array({"PARTICIPATION"}); stats["mintingAllowed"] = false;
        h.feed(infoActive(), stats, activationActive());
        QVERIFY(h.label("lblBanner").contains("Participation halt"));
        QVERIFY(!h.label("lblBanner").contains("Enforcement suspended"));
        QVERIFY(h.label("lblMintStatus").contains("Minting is paused"));
    }

    void bannerValveTripped() {
        Harness h;
        json info = infoActive();
        info["valveTripped"] = true; info["enforcing"] = false; info["rejectedBlocks"] = 1;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(h.label("lblBanner").contains("work valve tripped"));
        QVERIFY(h.label("lblBanner").contains("Restart the node"));
        QVERIFY(h.label("lblNotes").contains("rejected 1 block"));
        QVERIFY(h.label("lblEnforcement").contains("valve tripped"));
    }

    void bannerSunset() {
        Harness h;
        json info = infoActive();
        info["sunset"] = true; info["enforcing"] = false;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(h.label("lblBanner").contains("Enforcement sunset"));
        QVERIFY(h.label("lblBanner").contains("upgrade"));
        QVERIFY(h.label("lblEnforcement").contains("sunset"));
    }

    void bannerAbandoned() {
        Harness h;
        json info = infoActive();
        info["abandoned"] = true; info["enforcing"] = false;
        json stats = statsOpen();
        stats["haltMask"] = json::array({"PARTICIPATION", "ENFORCEMENT"}); stats["mintingAllowed"] = false;
        h.feed(info, stats, activationActive());
        QVERIFY(h.ctl.isAbandoned());
        QVERIFY(h.label("lblBanner").startsWith("Enforcement abandoned"));
        QVERIFY(h.label("lblBanner").contains("Sweep"));
        QVERIFY(!h.label("lblBanner").contains("Enforcement suspended"));   // abandonment supersedes
        QVERIFY(h.label("lblEnforcement").contains("abandoned"));
    }

    void bannerSuppressedBlocksIsInformation() {
        Harness h;
        json info = infoActive();
        info["suppressedBlocks"] = 2;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(!h.visible("lblBanner"));           // L11: not a warning
        QVERIFY(h.label("lblNotes").contains("2 rule-breaking block"));
        QVERIFY(h.label("lblNotes").contains("enforcement is still on"));
    }

    void bannerNotEnforcingNode() {
        Harness h;
        json info = infoActive();
        info["enforcing"] = false;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(h.label("lblBanner").contains("not enforcing"));
        QVERIFY(h.label("lblBanner").contains("pools that do enforce"));
    }

    void bannerNoMinerLine() {
        Harness h;
        json info = infoActive();
        info["miner"] = json::parse(R"({"payoutAddress": null, "signal": false, "quoteKind": "none", "quoteAgeSeconds": null, "registered": false, "eligible": false})");
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(!h.visible("lblNotes"));
    }

    // ── Overview (§4.8 Overview row) ──────────────────────────────────────────────────────

    void overviewRendersStats() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive());
        QCOMPARE(h.label("lblYecPrice"), QString("$2.0000 per YEC"));
        QCOMPARE(h.label("lblMintClaimPrice"), QString("$1.9900 / $2.0000"));
        QCOMPARE(h.label("lblSigma"), QString("1.00x"));
        QCOMPARE(h.label("lblGlobalRatio"), QString("497.50 %"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("no cap"));
        QCOMPARE(h.label("lblMintStatus"), QString("open"));
        QCOMPARE(h.label("lblSupply"), QString("$2,500.00 / ") % YellowbackFormat::zec(62500000000));
        QCOMPARE(h.label("lblVaults"), QString("3 / 1 / 2 / 0"));
        QCOMPARE(h.label("lblUnbacked"), QString("$0.00"));
        QVERIFY(h.label("lblActivation").contains("active since 193"));
        QVERIFY(h.label("lblEnforcement").contains("this node enforces"));
        QVERIFY(h.label("lblEnforcement").contains("strict"));
        QVERIFY(h.label("lblIndexHeight").startsWith("331"));
    }

    void overviewUndefinedPricesAndCap() {
        Harness h;
        json stats = statsOpen();
        stats["pMid"] = nullptr; stats["pMint"] = nullptr; stats["pClaim"] = nullptr; stats["globalRatioBps"] = nullptr;
        stats["supplyCapCents"] = 300000; stats["haltMask"] = json::array({"NO_PRICE"}); stats["mintingAllowed"] = false;
        h.feed(infoActive(), stats, activationActive());
        QVERIFY(h.label("lblYecPrice").startsWith("undefined"));
        QCOMPARE(h.label("lblMintClaimPrice"), QString("undefined / undefined"));
        QVERIFY(h.label("lblGlobalRatio").startsWith("undefined"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$500.00 of $3,000.00 cap"));
        QVERIFY(h.label("lblMintStatus").contains("NO_PRICE"));
    }

    void overviewCapReached() {
        Harness h;
        json stats = statsOpen();
        stats["supplyCapCents"] = 250000; stats["mintingAllowed"] = false;   // no halt bit, cap full
        h.feed(infoActive(), stats, activationActive());
        QVERIFY(h.label("lblMintStatus").contains("supply cap"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$0.00 of $2,500.00 cap"));
    }

    void overviewTrustCopy() {
        Harness h;
        QString trust = h.label("lblTrust");
        QVERIFY(trust.startsWith("Yellowback is a miner-enforced, over-collateralised stablecoin overlay on Ycash."));
        QVERIFY(trust.contains("a majority of hashpower that runs the module and follows it makes those rules hold"));
        QVERIFY(!trust.contains("trustless", Qt::CaseInsensitive));
        QVERIFY(!trust.contains("federat", Qt::CaseInsensitive));
    }

    // ── Vaults (§4.8 Vaults row) ──────────────────────────────────────────────────────────

    void vaultsRenderActiveRow() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({positionActive()}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->rowCount(QModelIndex()), 1);
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::DisplayRole).toString(), QString("ACTIVE"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Minted), Qt::DisplayRole).toString(), QString("$1,000.00"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::TermClass), Qt::DisplayRole).toString(), QString("A"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::ClaimHeight), Qt::DisplayRole).toString().startsWith("404"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Claimable), Qt::DisplayRole).toString(), QString("no"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Unbacked), Qt::DisplayRole).toString(), QString("no"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::SweepBefore), Qt::DisplayRole).toString(), QString("-"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Vault), Qt::DisplayRole).toString().right(2), QString(":0"));

        // Before the lock height: no action, the text says when
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(positionActive()), 331, false);
        QVERIFY(!a.release && !a.redeem && !a.sweep);
        QVERIFY(a.text.contains("lock height 380"));
        // At the lock height: Redeem, and the burn is exactly the debt
        json past = positionActive(); past["canRedeem"] = true;
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(past), 380, false);
        QVERIFY(a.redeem && !a.release && !a.sweep);
        QVERIFY(a.text.contains("burns $1,000.00"));
    }

    void vaultsRenderVoidRow() {
        Harness h;
        json v = positionActive();
        v["status"] = "VOID"; v["voidReason"] = "bad-mint-collateral"; v["underwaterAt"] = nullptr;
        v["sweepBefore"] = 404; v["canRedeem"] = true;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({v}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::DisplayRole).toString(), QString("VOID (bad-mint-collateral)"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::SweepBefore), Qt::DisplayRole).toString().startsWith("404"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::ToolTipRole).toString().contains("bad-mint-collateral"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::ToolTipRole).toString().contains("no burn, no fee"));

        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(v), 380, false);
        QVERIFY(a.release && !a.redeem && !a.sweep);
        QVERIFY(a.text.contains("no YED burned and no fee paid"));
        QVERIFY(a.text.contains("before height 404"));

        // Select the row: the page text follows and the Release button is shown but disabled (7b-b)
        auto table = h.tab.findChild<QTableView*>("tblPositions");
        QVERIFY(table != nullptr);
        table->setCurrentIndex(m->index(0, 0));
        QVERIFY(h.label("lblVaultAction").contains("no YED burned"));
        QVERIFY(h.label("lblVaultAction").contains("later release"));
        QVERIFY(h.button("btnRelease") != nullptr && !h.button("btnRelease")->isEnabled());
        QVERIFY(h.button("btnWhyVoid")->isEnabled());
    }

    void vaultsRenderAbandonedRow() {
        Harness h;
        json info = infoActive(); info["abandoned"] = true;
        json v = positionActive(); v["sweepBefore"] = 404; v["canSweep"] = true; v["canRedeem"] = true;
        h.feed(info, statsOpen(), activationActive(), json::array({v}));
        auto m = h.ctl.positionsModel();
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::SweepBefore), Qt::DisplayRole).toString().startsWith("404"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::SweepBefore), Qt::ToolTipRole).toString().contains("abandoned"));

        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(v), 400, true);
        QVERIFY(a.sweep && a.redeem);
        QVERIFY(a.text.contains("unbacked"));
        QVERIFY(a.text.contains("before height 404"));

        auto table = h.tab.findChild<QTableView*>("tblPositions");
        table->setCurrentIndex(m->index(0, 0));
        QVERIFY(h.button("btnSweep") != nullptr && !h.button("btnSweep")->isEnabled());
        QVERIFY(h.label("lblVaultAction").contains("Sweep"));
    }

    void vaultsRenderClosedRows() {
        json closed = positionActive();
        closed["status"] = "CLOSED"; closed["closeHeight"] = 390; closed["closingTxid"] = "9e8d"; closed["burnedCents"] = 100000;
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(closed), 400, false);
        QVERIFY(!a.release && !a.redeem && !a.sweep);
        QVERIFY(a.text.contains("burning $1,000.00"));

        json swept = closed; swept["burnedCents"] = 0; swept["unbacked"] = true;
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(swept), 400, false);
        QVERIFY(a.text.contains("unbacked"));

        json claimed = closed; claimed["status"] = "CLAIMED";
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(claimed), 400, false);
        QVERIFY(a.text.contains("Claimed by someone else"));

        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({swept}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Unbacked), Qt::DisplayRole).toString(), QString("yes"));
    }

    // ── Claim list (§4.8 Claim row) ───────────────────────────────────────────────────────

    void claimListRenders() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive(), json(nullptr), json::array({claimableRow()}));
        auto m = h.ctl.claimableModel();
        QCOMPARE(m->rowCount(QModelIndex()), 1);
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::Burn), Qt::DisplayRole).toString(), QString("$1,000.00"));
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::Fee), Qt::DisplayRole).toString(), YellowbackFormat::zec(62814071));
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::ClaimHeight), Qt::DisplayRole).toString(), QString("404"));
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::UnderwaterAt), Qt::DisplayRole).toString(), QString("$0.4378"));
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::PClaim), Qt::DisplayRole).toString(), QString("$0.4000"));
        QVERIFY(h.label("lblClaimHint").contains("1 claimable vault"));
        QVERIFY(h.button("btnClaim") != nullptr && !h.button("btnClaim")->isEnabled());
    }

    void claimListEmpty() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive(), json(nullptr), json::array());
        QCOMPARE(h.ctl.claimableModel()->rowCount(QModelIndex()), 0);
        QVERIFY(h.label("lblClaimHint").contains("No vault is claimable"));
    }

    // ── Transactions (v2 types parse; the full rendering cases are Phase 7b-b) ────────────

    void transactionTypesParse() {
        QCOMPARE(YellowbackFormat::typeLabel("claim"), QString("Claim"));
        QCOMPARE(YellowbackFormat::typeLabel("claimed"), QString("Vault claimed"));
        QCOMPARE(YellowbackFormat::typeLabel("sweep"), QString("Sweep"));
        json e = txMint(); e["height"] = -1; e["confirmations"] = 0; e["verdict"] = "expired"; e["expired"] = true;
        QVERIFY(YellowbackTx::fromJson(e).expired);
    }

    // ── Mint page: class list from params.classes; the button is Phase 7b-b ───────────────

    void mintClassesFromParams() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive());
        auto classes = h.ctl.termClasses();
        QCOMPARE(classes.size(), 3);
        QCOMPARE(classes[1].name, QString("B"));
        QCOMPARE(classes[1].minBlocks, 97);
        auto combo = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->itemData(0).toInt(), 48);
        QVERIFY(h.button("btnMint") != nullptr && !h.button("btnMint")->isEnabled());
        QVERIFY(h.ctl.mintBlocker(1000).contains("between $100.00 and $10,000.00"));
        QVERIFY(h.ctl.mintBlocker(10000).isEmpty());
    }

    // ── Devnet end-to-end (Phase 7b-b) ────────────────────────────────────────────────────

    void devnetEndToEnd() {
        if (qEnvironmentVariableIsEmpty("YELLOWBACK_DEVNET_DIR"))
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node (Phase 7b-b)");
        QSKIP("the devnet end-to-end case is written in Phase 7b-b");
    }
};

QTEST_MAIN(YellowbackTabTest)
#include "yellowbacktab_test.moc"
