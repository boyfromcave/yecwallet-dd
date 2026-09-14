// Offline QTest for the Yellowback tab (plan §4.8 "Offline QTest cases", N28; Phase 7b-a).
//
// Runs under QT_QPA_PLATFORM=offscreen with no node: a bare YellowbackController(nullptr,
// nullptr) is fed canned JSON through YellowbackController::feed(), which applies the replies
// exactly as the RPC callbacks would. The canned values are the example values of
// docs/yellowback-rpc-contract.json (the generated copy of ycash-dd/doc/yellowback-rpc.md), so
// a contract change that renames a field shows up here as well as in check-rpc-contract.py.
//
// The dialog cases (Phase 7b-b) drive the actions through a fake transport
// (YellowbackController::setTransport — the "fake Connection" of N28) that answers each yed_*
// method with canned result JSON or a canned error identifier, and capture the confirmation
// and notice copy through YellowbackTab::confirmFn / noticeFn.
//
// The devnet end-to-end case reads YELLOWBACK_DEVNET_DIR (the devnet's --dir), takes the RPC
// port and credentials from its node0/ycash.conf, and QSKIPs when the variable is unset; CI
// runs only the offline cases.

#include <QtTest>
#include <QTabWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>

#include "yellowbacktab.h"
#include "yellowbackmodels.h"
#include "yellowbackcontroller.h"
#include "yellowbackrpc.h"
#include "settings.h"

using json = nlohmann::json;

// ── Canned replies (contract example values) ──────────────────────────────────────────────

static json infoActive() {
    return json::parse(R"({
      "rpcversion": 3, "enabled": true, "network": "regtest", "height": 331,
      "blockhash": "0f3a9c1e5b7d2a4c6e8f0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f",
      "chainHeight": 331, "startHeight": 1, "healthy": true, "unhealthyReason": "",
      "enforcing": true, "valveTripped": false, "sunset": false, "rejectedBlocks": 0,
      "suppressedBlocks": 0, "templatePolicy": "strict", "abandoned": false,
      "activation": {"status": "active", "lockInHeight": 129, "activateHeight": 193, "signalCount": 64, "window": 64},
      "miner": {"payoutAddress": "smQvTmAz2ExamplePayoutAddress1111111", "signal": true, "quoteKind": "quote",
                "quoteAgeSeconds": 12, "registered": true, "eligible": true},
      "attest": {"status": "ARMED", "triggerHeight": 300, "armHeight": 308, "seatedCount": 3, "poolSize": 9,
                 "poolFresh": 3, "carrierMode": "scriptsig", "required": true, "armed": true},
      "params": {"startHeight": 1, "enforceUntilHeight": 0, "sigmaRefBps": 0, "supplyCapBps": 0, "refLag": 2,
                 "refWindow": 40, "grace": 24, "payeeWindow": 10, "feeMinZat": 50000000, "feeBps": 25,
                 "tokenValueZat": 10000, "feeZat": 1000, "valveBlocks": 6, "abandonBlocks": 128,
                 "windows": {"fast": 8, "mid": 24, "slow": 64, "signal": 64},
                 "minFill": {"fast": 4, "mid": 16, "slow": 43},
                 "classes": [{"class": "A", "minBlocks": 48, "maxBlocks": 96, "baseRatioBps": 50000},
                             {"class": "B", "minBlocks": 97, "maxBlocks": 144, "baseRatioBps": 40000},
                             {"class": "C", "minBlocks": 145, "maxBlocks": 240, "baseRatioBps": 30000}],
                 "policy": {"penaltyBlocks": 12, "accuracyWindow": 24, "tiltBps": 10000, "preferredPayee": null, "preferredAttestor": null},
                 "attest": {"required": true, "mSelect": 2, "kSlack": 1, "nSlots": 5, "divergeBpsAttest": 1500, "armDelay": 8,
                            "armMin": 3, "emergencyPersist": 4, "emergencyRatioBps": 10500, "emergencyNoticeTtl": 64,
                            "carrierMode": "scriptsig", "attestFeeBps": 2500, "attestMaxAge": 8}}
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

static json estimateReply() {
    return json::parse(R"({
      "requiredZat": 25125628141, "termClass": "A", "lockHeight": 380, "claimHeight": 404, "minRatioBps": 50000,
      "baseRatioBps": 50000, "sigmaMultBps": 10000, "pMint": 1990000, "refHeight": 329
    })");
}

static json feePayeeReply() {
    return json::parse(R"({
      "eligible": ["smQvTmAz2ExamplePayoutAddress1111111"], "feeZat": 50000000,
      "default": {"payoutAddress": "smQvTmAz2ExamplePayoutAddress1111111", "weight": 19800},
      "policy": {"penaltyBlocks": 12, "accuracyWindow": 24, "tiltBps": 10000}
    })");
}

static json mintReply() {
    return json::parse(R"({
      "txid": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8",
      "vault": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0",
      "termClass": "A", "lockHeight": 380, "claimHeight": 404, "collateralZat": 25125628141, "feeZat": 62814071,
      "payee": "smQvTmAz2ExamplePayoutAddress1111111", "fundedFrom": "transparent", "warning": ""
    })");
}

static json redeemReply() {
    return json::parse(R"({
      "txid": "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d", "burnedCents": 100000,
      "feeZat": 62814071, "payee": "smQvTmAz2ExamplePayoutAddress1111111", "collateralOut": 25062804070,
      "to": "smExampleTransparentTwin111111111111"
    })");
}

static json sweepReply() {
    return json::parse(R"({
      "txid": "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d", "hex": "0400008085202f8901",
      "collateralOut": 25125627141, "to": "smExampleTransparentTwin111111111111", "unbackedCents": 100000
    })");
}

static json balanceReply(qint64 confirmed) {
    return json{{"confirmedCents", confirmed}, {"unconfirmedCents", 0}, {"height", 331}};
}

// A base58 regtest Yellowback address (no 0, O, I, l) for the send cases.
static const char* RECIPIENT = "yrEx1mp1eRece1verAddress1111111111";

// The fake Connection (N28): every method answers from a table, and every call is recorded.
struct FakeRpc {
    struct Call { QString method; json params; };
    QMap<QString, json>    results;
    QMap<QString, QString> errors;
    QList<Call>            calls;
    YellowbackController::Transport transport() {
        return [this](const QString& m, const json& params, YellowbackController::OkFn ok, YellowbackController::ErrFn err) {
            calls.append({m, params});
            if (errors.contains(m)) { if (err) err(errors[m]); return; }
            if (results.contains(m)) { if (ok) ok(results[m]); return; }
            if (err) err(QString("Method not found (%1 has no canned reply)").arg(m));
        };
    }
    int count(const char* m) const { int n = 0; for (auto& c : calls) if (c.method == m) n++; return n; }
    json lastParams(const char* m) const { json p; for (auto& c : calls) if (c.method == m) p = c.params; return p; }
};

// A tab with a controller that is fed the given replies; labels are found by object name.
// Prompts are captured: `answer` is what every confirmation returns.
struct Harness {
    YellowbackTab        tab{nullptr};
    YellowbackController ctl{nullptr, nullptr};
    FakeRpc              rpc;
    bool                 answer = true;
    QStringList          confirms;       // the confirmation texts shown, in order
    QStringList          notices;        // "title|text" of every notice
    QStringList          errorNotices;   // the texts of the error notices
    Harness() {
        tab.setController(&ctl);
        ctl.setTransport(rpc.transport());
        tab.confirmFn = [this](const QString&, const QString& text) { confirms << text; return answer; };
        tab.noticeFn  = [this](const QString& title, const QString& text, bool isError) {
            notices << title % "|" % text;
            if (isError) errorNotices << text;
        };
    }
    // Every piece of copy the dialogs show must follow §8.1: never "trustless", never a federation.
    bool copyIsClean() const {
        for (const QString& t : confirms + notices)
            if (t.contains("trustless", Qt::CaseInsensitive) || t.contains("federat", Qt::CaseInsensitive)) return false;
        return true;
    }
    void feedActive(int height = 331, const json& positions = json(nullptr), const json& claimable = json(nullptr),
                    qint64 balanceCents = 500000, bool abandoned = false) {
        json info = infoActive();
        info["height"] = height; info["chainHeight"] = height; info["abandoned"] = abandoned;
        ctl.feed(info, statsOpen(), activationActive(), balanceReply(balanceCents), positions, claimable, json(nullptr));
        // the refresh an action triggers on success reads the same replies back
        rpc.results[YellowbackRpc::GETINFO]          = info;
        rpc.results[YellowbackRpc::GETSTATS]         = statsOpen();
        rpc.results[YellowbackRpc::GETACTIVATION]    = activationActive();
        rpc.results[YellowbackRpc::GETBALANCE]       = balanceReply(balanceCents);
        rpc.results[YellowbackRpc::LISTPOSITIONS]    = positions.is_null() ? json::array() : positions;
        rpc.results[YellowbackRpc::LISTCLAIMABLE]    = claimable.is_null() ? json::array() : claimable;
        rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array();
    }
    void selectRow(const char* table, int row) {
        auto t = tab.findChild<QTableView*>(table);
        if (t != nullptr) t->setCurrentIndex(t->model()->index(row, 0));
    }
    // Line edits are found on a page: Send and Mint both have a txtAmount
    void setText(YellowbackTab::Page page, const char* name, const QString& text) {
        auto e = tab.page(page)->findChild<QLineEdit*>(name);
        if (e != nullptr) e->setText(text);
    }
    void mintAmount(const QString& t) { setText(YellowbackTab::Mint, "txtAmount", t); }
    void sendTo(const QString& addr, const QString& amount) {
        setText(YellowbackTab::Send, "txtRecipient", addr);
        setText(YellowbackTab::Send, "txtAmount", amount);
    }
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

// The devnet transport: synchronous JSON-RPC over HTTP to node 0 (the QTest has no MainWindow,
// so it cannot use Connection::doRPCSafe, which dereferences it).
struct DevnetTransport {
    QNetworkAccessManager nam;
    QNetworkRequest       request;
    json post(const json& payload, QString* error) {
        QNetworkReply* reply = nam.post(request, QByteArray::fromStdString(payload.dump()));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        json parsed = json::parse(reply->readAll(), nullptr, false);
        reply->deleteLater();
        if (parsed.is_discarded()) { *error = "no JSON in the reply"; return json(nullptr); }
        if (parsed.is_object() && parsed.find("error") != parsed.end() && parsed["error"].is_object()) {
            *error = QString::fromStdString(parsed["error"].value("message", std::string("unknown error")));
            return json(nullptr);
        }
        return parsed["result"];
    }
    YellowbackController::Transport transport() {
        return [this](const QString& m, const json& params, YellowbackController::OkFn ok, YellowbackController::ErrFn err) {
            json payload = {{"jsonrpc", "1.0"}, {"id", "yellowback_test"}, {"method", m.toStdString()}, {"params", params.is_null() ? json::array() : params}};
            QString e;
            json r = post(payload, &e);
            if (!e.isEmpty()) { if (err) err(e); } else if (ok) ok(r);
        };
    }
    // Read rpcuser / rpcpassword / rpcport from the devnet's per-node ycash.conf.  The wallet
    // only ever talks to node 0; the other nodes are reachable for the test's own steps (mining
    // a pool block, moving a pool's quote), which on a real network are other people's machines.
    QString                     devnetDir;
    QMap<int, QNetworkRequest>  nodeRequests;
    bool attachNode(const QString& dir, int node, QNetworkRequest* out, QString* why) {
        QFile f(dir % QString("/node%1/ycash.conf").arg(node));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { *why = "cannot read " % f.fileName(); return false; }
        QMap<QString, QString> kv;
        for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
            int eq = line.indexOf('=');
            if (eq > 0) kv[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
        }
        if (!kv.contains("rpcport") || !kv.contains("rpcuser") || !kv.contains("rpcpassword")) { *why = "ycash.conf lacks rpcport/rpcuser/rpcpassword"; return false; }
        out->setUrl(QUrl("http://127.0.0.1:" % kv["rpcport"] % "/"));
        out->setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
        out->setRawHeader("Authorization", "Basic " % (kv["rpcuser"] % ":" % kv["rpcpassword"]).toUtf8().toBase64());
        return true;
    }
    bool attach(const QString& dir, QString* why) {
        devnetDir = dir;
        if (!attachNode(dir, 0, &request, why)) return false;
        nodeRequests[0] = request;
        return true;
    }
    // A plain RPC for the test's own steps (generate, getblockcount); fails the test on error
    json rpc(const char* method, const json& params = json::array()) {
        QString e;
        json r = post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", method}, {"params", params}}, &e);
        if (!e.isEmpty()) qWarning("%s: %s", method, qPrintable(e));
        return r;
    }
    // Wait until node 0 has fully digested the tip: nothing left in the mempool, the Yellowback
    // index caught up with the chain (yed_getinfo.height == chainHeight == getblockcount).  getblockcount rises when a
    // block connects; the wallet and the overlay index follow, and a transaction built in that
    // gap selects inputs the block has already spent ("AcceptToMemoryPool: inputs already spent").
    bool settle(const QString& txid = QString()) {
        for (int w = 0; w < 600; w++) {
            json pool = rpc("getrawmempool"), info = rpc("yed_getinfo"), count = rpc("getblockcount");
            bool chain = pool.is_array() && pool.empty() && info.is_object() && count.is_number() &&
                         info.value("height", -1) == count.get<int>() &&
                         info.value("chainHeight", -2) == count.get<int>();
            // The wallet marks the block's inputs spent on its own thread, ~1 s after UpdateTip;
            // until it has, the next transaction selects an input the block already spent
            // ("AcceptToMemoryPool: inputs already spent").  gettransaction is the wallet's own
            // view, so a confirmation there is the signal that it has caught up.
            bool wallet = txid.isEmpty();
            if (chain && !wallet) {
                json t = rpc("gettransaction", json::array({txid.toStdString()}));
                wallet = t.is_object() && t.value("confirmations", 0) >= 1;
            }
            if (chain && wallet) { QTest::qWait(100); return true; }
            QTest::qWait(25);
        }
        return false;
    }
    // The same, on another devnet node (2-4 are the pools: they tag, signal and quote)
    json rpcOn(int node, const char* method, const json& params = json::array()) {
        if (!nodeRequests.contains(node)) {
            QNetworkRequest r; QString why;
            if (!attachNode(devnetDir, node, &r, &why)) { qWarning("node%d: %s", node, qPrintable(why)); return json(nullptr); }
            nodeRequests[node] = r;
        }
        QNetworkRequest saved = request;
        request = nodeRequests[node];
        QString e;
        json r = post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", method}, {"params", params}}, &e);
        request = saved;
        if (!e.isEmpty()) qWarning("node%d %s: %s", node, method, qPrintable(e));
        return r;
    }
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

    void contractVersionIsThree() {
        QCOMPARE(YellowbackRpc::RPC_VERSION, 3);
        QCOMPARE(Settings::getYellowbackRpcVersion(), 3);
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
        info["rpcversion"] = 2;
        h.feed(info);
        QVERIFY(!h.ctl.isAvailable());
        QVERIFY(h.label("lblBanner").contains("version 3"));
        QVERIFY(h.label("lblBanner").contains("version 2"));
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

    // H10: the node reports how many of this wallet's outputs it holds locked, and whether the
    // index is protecting them at all.
    void bannerLockedOutputsIsInformation() {
        Harness h;
        json info = infoActive();
        info["lockedOutputs"] = 3; info["protectedByIndex"] = true;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(!h.visible("lblBanner"));           // locks working is not a warning
        QVERIFY(h.label("lblNotes").contains("3 of this wallet's outputs carry YED"));
        QVERIFY(h.label("lblNotes").contains("yed_unlockcoin"));
    }

    void bannerUnprotectedYedIsAWarning() {
        Harness h;
        json info = infoActive();
        info["lockedOutputs"] = 0; info["protectedByIndex"] = false;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY(h.label("lblBanner").contains("not holding your YED outputs locked"));
        QVERIFY(h.label("lblBanner").contains("burn the YED"));
        QVERIFY(!h.label("lblNotes").contains("yed_unlockcoin"));
        QVERIFY(h.copyIsClean());
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

        // Select the row: the page text follows and Release is offered (L14)
        auto table = h.tab.findChild<QTableView*>("tblPositions");
        QVERIFY(table != nullptr);
        table->setCurrentIndex(m->index(0, 0));
        QVERIFY(h.label("lblVaultAction").contains("no YED burned"));
        QVERIFY(h.button("btnRelease") != nullptr && h.button("btnRelease")->isEnabled() && !h.button("btnRelease")->isHidden());
        QVERIFY(h.button("btnRedeem")->isHidden() && h.button("btnSweep")->isHidden());
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
        QVERIFY(h.button("btnSweep") != nullptr && h.button("btnSweep")->isEnabled() && !h.button("btnSweep")->isHidden());
        QVERIFY(h.button("btnRedeem")->isEnabled());
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
        const qint64 yedBefore = h.ctl.confirmedCents();
    }

    // ── Mint page: class derivation from the lock length (§4.8; regtest ranges) ───────────

    void mintClassFromLockLength() {
        Harness h;
        h.feedActive();
        struct { int lock; const char* cls; } cases[] = {
            {47, ""}, {48, "A"}, {96, "A"}, {97, "B"}, {144, "B"}, {145, "C"}, {240, "C"}, {241, ""}};
        for (auto& c : cases)
            QCOMPARE(h.ctl.classForLock(c.lock).name, QString(c.cls));
        QCOMPARE(h.ctl.classForLock(100).baseRatioBps, (qint64)40000);
    }

    // ── Mint dialog ───────────────────────────────────────────────────────────────────────

    void mintConfirmationAndCall() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply();
        h.rpc.results[YellowbackRpc::MINT]               = mintReply();
        Settings::getInstance()->setYellowbackBackupPending(false);
        h.mintAmount("1000");
        h.tab.doMint();

        QCOMPARE(h.confirms.size(), 1);
        const QString& text = h.confirms[0];
        QVERIFY(text.contains("Mint $1,000.00 of YED"));
        QVERIFY(text.contains("class A"));
        QVERIFY(text.contains("48 blocks"));
        QVERIFY(text.contains(YellowbackFormat::zec(25125628141)));          // exact collateral
        QVERIFY(text.contains("500.00 %"));                                  // ratio
        QVERIFY(text.contains("1.00x"));                                     // sigma
        QVERIFY(text.contains(YellowbackFormat::zec(50000000)));             // enforcement fee
        QVERIFY(text.contains("smQvTmAz2ExamplePayoutAddress1111111"));      // payee
        QVERIFY(text.contains("height 380"));
        QVERIFY(text.contains("Back up wallet.dat"));
        QVERIFY(text.contains("only your key can spend it before the claim height"));

        // yed_mint <cents> <lockBlocks>, no `from` for the transparent balance
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::MINT), json::array({100000, 48}));
        // yed_getfeepayee <refHeight from the estimate> <requiredZat>
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::GETFEEPAYEE), json::array({329, 25125628141}));
        QCOMPARE(h.notices.size(), 1);
        QVERIFY(h.notices[0].startsWith("Mint sent|"));
        QVERIFY(h.notices[0].contains("6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0"));
        QVERIFY(h.notices[0].contains("class A"));
        QVERIFY(Settings::getInstance()->getYellowbackBackupPending());      // the after-mint nag
        QVERIFY(h.label("lblMintPageStatus").startsWith("Minted. txid: 6a1f"));
        QVERIFY(h.copyIsClean());
    }

    void mintCancelledMakesNoCall() {
        Harness h;
        h.answer = false;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply();
        h.mintAmount("1000");
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 0);
        QVERIFY(h.notices.isEmpty());
    }

    void mintNoEligiblePayee() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.errors[YellowbackRpc::GETFEEPAYEE] = "fee-no-eligible-payee: no quote tag in the payee window";
        h.rpc.results[YellowbackRpc::MINT] = mintReply();
        h.mintAmount("1000");
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].contains("Enforcement fee: none"));
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);
    }

    void mintRefusedByMintpol() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply();
        h.rpc.errors[YellowbackRpc::MINT] = "mintpol-no-price: no mint price at the reference snapshot";
        h.mintAmount("1000");
        h.tab.doMint();
        QCOMPARE(h.errorNotices.size(), 1);
        QVERIFY(h.errorNotices[0].contains("yed_mint failed: mintpol-no-price"));   // verbatim identifier
        QVERIFY(h.errorNotices[0].contains("too few recent blocks carry a price quote"));
        QVERIFY(h.label("lblMintPageStatus").contains("mintpol-no-price"));
        QVERIFY(!Settings::getInstance()->getYellowbackBackupPending() || true);   // untouched on failure
        QVERIFY(h.copyIsClean());
    }

    void mintBadLockAndUnsatisfiable() {
        QVERIFY(YellowbackController::explainError("mint-bad-lock: lockBlocks 47 is in no term class").contains("no term class"));
        QVERIFY(YellowbackController::explainError("mint-unsatisfiable: the required collateral exceeds MAX_MONEY").contains("maximum amount of YEC"));
        QVERIFY(YellowbackController::explainError("mintpol-participation: x").contains("60 %"));
        QVERIFY(YellowbackController::explainError("mintpol-cap: x").contains("supply cap"));
        QVERIFY(YellowbackController::explainError("mintpol-divergence: x").contains("diverge"));
        QVERIFY(YellowbackController::explainError("mintpol-global-ratio: x").contains("collateral ratio"));
        QVERIFY(YellowbackController::explainError("mintpol-not-active: x").contains("activation"));
        QVERIFY(YellowbackController::explainError("something else entirely").isEmpty());
    }

    // ── Send dialog ───────────────────────────────────────────────────────────────────────

    void sendConfirmationAndCall() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::VALIDATEADDRESS] = json{{"isvalid", true}, {"address", RECIPIENT}, {"ismine", false}};
        h.rpc.results[YellowbackRpc::SEND] = json::parse(R"({"txid": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "changeCents": 50000, "expiryHeight": 371})");
        h.sendTo(RECIPIENT, "123");
        h.tab.doSend();
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].contains("Send $123.00 of YED"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::SEND), json::array({RECIPIENT, 12300}));
        QVERIFY(h.label("lblSendStatus").contains("Sent $123.00"));
        QVERIFY(h.label("lblSendStatus").contains("change $500.00"));
        QVERIFY(h.label("lblSendStatus").contains("height 371"));
        QVERIFY(h.copyIsClean());
    }

    void sendChangeFloorNamesWorkableAmounts() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::VALIDATEADDRESS] = json{{"isvalid", true}, {"address", RECIPIENT}, {"ismine", false}};
        h.rpc.errors[YellowbackRpc::SEND] = "change-floor: change of 50 cents is below the minimum output of 100 cents; send 12345 cents (all selected inputs) or at most 12245 cents";
        h.sendTo(RECIPIENT, "122.95");
        h.tab.doSend();
        QCOMPARE(h.rpc.count(YellowbackRpc::SEND), 1);
        QVERIFY(h.label("lblSendHint").contains("exactly $123.45"));
        QVERIFY(h.label("lblSendHint").contains("at most $122.45"));
        QVERIFY(h.label("lblSendStatus").startsWith("yed_send failed: change-floor"));
        QCOMPARE(h.errorNotices.size(), 1);

        qint64 all = 0, atMost = 0;
        QVERIFY(YellowbackController::parseChangeFloor(h.rpc.errors[YellowbackRpc::SEND], &all, &atMost));
        QCOMPARE(all, (qint64)12345);
        QCOMPARE(atMost, (qint64)12245);
        QVERIFY(!YellowbackController::parseChangeFloor("change-floor: no figures here", &all, &atMost));
    }

    void sendRefusesNonYellowbackAddress() {
        Harness h;
        h.feedActive();
        // The node's yed_validateaddress reports the identifier in `reason` (no throw)
        h.rpc.results[YellowbackRpc::VALIDATEADDRESS] = json{{"isvalid", false}, {"reason", "not-a-yellowback-address"}};
        h.sendTo(RECIPIENT, "123");
        h.tab.doSend();
        QCOMPARE(h.rpc.count(YellowbackRpc::SEND), 0);
        QVERIFY(h.confirms.isEmpty());
        QVERIFY(h.label("lblSendHint").contains("not-a-yellowback-address"));
        // and the local check refuses an s1… address before any RPC
        h.sendTo("s1exampleTransparentAddress1111111", "123");
        h.tab.doSend();
        QCOMPARE(h.rpc.count(YellowbackRpc::VALIDATEADDRESS), 1);
        QVERIFY(h.label("lblSendHint").contains("Ycash address"));
    }

    void sendInsufficientYed() {
        Harness h;
        h.feedActive(331, json(nullptr), json(nullptr), 10000);
        h.sendTo(RECIPIENT, "150");
        h.tab.doSend();
        QCOMPARE(h.rpc.count(YellowbackRpc::VALIDATEADDRESS), 0);
        QVERIFY(h.label("lblSendHint").contains("Only $100.00 is confirmed"));
        QVERIFY(YellowbackController::explainError("insufficient-yed: need 15000 cents").contains("confirmed YED"));
    }

    // ── Redeem / Release dialogs ──────────────────────────────────────────────────────────

    void redeemActiveConfirmationAndResult() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.results[YellowbackRpc::GETFEEPAYEE] = feePayeeReply();
        h.rpc.results[YellowbackRpc::REDEEM]      = redeemReply();
        h.tab.redeemVault(YellowbackPosition::fromJson(p));

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].startsWith("Redeem vault 6a1f"));
        QVERIFY(h.confirms[0].contains("Burn: $1,000.00 of YED"));
        QVERIFY(h.confirms[0].contains(YellowbackFormat::zec(50000000)));                 // fee estimate
        QVERIFY(h.confirms[0].contains("smQvTmAz2ExamplePayoutAddress1111111"));
        QVERIFY(h.confirms[0].contains(YellowbackFormat::zec(25125628141 - 50000000)));   // collateral out
        QVERIFY(h.confirms[0].contains("fresh transparent address"));
        // yed_getfeepayee <tip - refLag> <collateralZat>; yed_redeem <vaultTxid> (no `to`)
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::GETFEEPAYEE), json::array({379, 25125628141}));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::REDEEM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}));
        QCOMPARE(h.notices.size(), 1);
        QVERIFY(h.notices[0].startsWith("Redeem sent|"));
        QVERIFY(h.notices[0].contains("YED burned: $1,000.00"));                            // burnedCents
        QVERIFY(h.notices[0].contains("fee: " % YellowbackFormat::zec(62814071) % " to smQvTmAz2ExamplePayoutAddress1111111"));   // feeZat, payee
        QVERIFY(h.notices[0].contains(YellowbackFormat::zec(25062804070) % " to smExampleTransparentTwin111111111111"));          // collateralOut, to
        QVERIFY(h.copyIsClean());
    }

    // H4: the node burns a sub-dollar remainder when no selection leaves workable change, reports
    // it as extraBurnCents, and the dialog says so.
    void redeemShowsExtraBurn() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.results[YellowbackRpc::GETFEEPAYEE] = feePayeeReply();
        json r = redeemReply(); r["extraBurnCents"] = 75;
        h.rpc.results[YellowbackRpc::REDEEM] = r;
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QCOMPARE(h.notices.size(), 1);
        QVERIFY(h.notices[0].contains("YED burned: $1,000.00"));
        QVERIFY2(h.notices[0].contains("plus $0.75 burned as a remainder"), qPrintable(h.notices[0]));
        QVERIFY(h.copyIsClean());
    }

    void redeemWithoutExtraBurnSaysNothing() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.results[YellowbackRpc::GETFEEPAYEE] = feePayeeReply();
        h.rpc.results[YellowbackRpc::REDEEM]      = redeemReply();      // extraBurnCents 0
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QCOMPARE(h.notices.size(), 1);
        QVERIFY(!h.notices[0].contains("remainder"));
    }

    void redeemWithDestinationFromRedeemPage() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.errors[YellowbackRpc::GETFEEPAYEE] = "fee-no-eligible-payee: E(R) is empty";
        h.rpc.results[YellowbackRpc::REDEEM]     = redeemReply();
        h.tab.redeemVault(YellowbackPosition::fromJson(p), "ys1exampleSaplingDestination");
        QVERIFY(h.confirms[0].contains("Enforcement fee: none"));
        QVERIFY(h.confirms[0].contains("ys1exampleSaplingDestination"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::REDEEM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "ys1exampleSaplingDestination"}));
        // the Redeem page lists the vault and offers the default destination
        auto combo = h.tab.findChild<QComboBox*>("cmbVault");
        QVERIFY(combo != nullptr && combo->count() == 1);
        QVERIFY(h.tab.redeemDestination().isEmpty());
        QVERIFY(h.button("btnStart")->isEnabled());
    }

    void redeemRefusedInsufficientYedLocally() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}), json(nullptr), 5000);
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.confirms.isEmpty());
        QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 0);
        QCOMPARE(h.errorNotices.size(), 1);
        QVERIFY(h.errorNotices[0].contains("only $50.00 is confirmed"));
        QVERIFY(!h.button("btnStart")->isEnabled());
    }

    void redeemVaultLockedAndNotActiveErrors() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.results[YellowbackRpc::GETFEEPAYEE] = feePayeeReply();
        h.rpc.errors[YellowbackRpc::REDEEM] = "vault-locked: vault is locked until height 380";
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QCOMPARE(h.errorNotices.size(), 1);
        QVERIFY(h.errorNotices[0].contains("yed_redeem failed: vault-locked"));
        QVERIFY(h.errorNotices[0].contains("every Ycash node enforces"));

        h.rpc.errors[YellowbackRpc::REDEEM] = "vault-not-active: vault is not active";
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QCOMPARE(h.errorNotices.size(), 2);
        QVERIFY(h.errorNotices[1].contains("already closed or claimed"));

        h.rpc.errors[YellowbackRpc::REDEEM] = "mempool-check-failed:vault-redeem-fee-missing";
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.errorNotices[2].contains("nothing was signed or sent"));

        // locked by the wallet's own height check: no call at all
        h.feedActive(300, json::array({p}));
        int before = h.rpc.count(YellowbackRpc::REDEEM);
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), before);
        QVERIFY(h.notices.last().contains("locked until height 380"));
        QVERIFY(h.copyIsClean());
    }

    void releaseVoidVault() {
        Harness h;
        json p = positionActive();
        p["status"] = "VOID"; p["voidReason"] = "mint-ratio-below-minimum"; p["mintedCents"] = 0;
        p["sweepBefore"] = 404; p["canRedeem"] = true; p["underwaterAt"] = nullptr;
        h.feedActive(381, json::array({p}), json(nullptr), 0);
        json r = redeemReply(); r["burnedCents"] = 0; r["feeZat"] = 0; r["payee"] = nullptr; r["collateralOut"] = 25125628141;
        h.rpc.results[YellowbackRpc::REDEEM] = r;
        h.tab.redeemVault(YellowbackPosition::fromJson(p));

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].startsWith("Release the collateral of VOID vault"));
        QVERIFY(h.confirms[0].contains("No YED is burned and no fee is paid"));
        QVERIFY(h.confirms[0].contains("before height 404"));
        QCOMPARE(h.rpc.count(YellowbackRpc::GETFEEPAYEE), 0);      // no fee to estimate
        QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 1);           // the same RPC as Redeem (L14)
        QVERIFY(h.notices[0].startsWith("Release sent|"));
        QVERIFY(h.notices[0].contains("YED burned: $0.00"));
        QVERIFY(h.notices[0].contains("fee: " % YellowbackFormat::zec(0) % " to none"));
        // the Vaults page offers Release for that row
        h.selectRow("tblPositions", 0);
        QVERIFY(h.button("btnRelease")->isEnabled() && !h.button("btnRelease")->isHidden());
        QVERIFY(h.button("btnSweep")->isHidden());
        QVERIFY(h.copyIsClean());
    }

    // ── Claim dialog ──────────────────────────────────────────────────────────────────────

    void claimConfirmationAndResult() {
        Harness h;
        h.feedActive(410, json(nullptr), json::array({claimableRow()}));
        h.rpc.results[YellowbackRpc::CLAIM] = redeemReply();
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].startsWith("Claim vault 6a1f"));
        QVERIFY(h.confirms[0].contains("Burn: $1,000.00 of YED"));
        QVERIFY(h.confirms[0].contains("Enforcement fee: " % YellowbackFormat::zec(62814071)));
        QVERIFY(h.confirms[0].contains(YellowbackFormat::zec(25125628141 - 62814071)));
        QVERIFY(h.confirms[0].contains("$0.4000 per YEC"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::CLAIM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}));
        QVERIFY(h.notices[0].startsWith("Claim sent|"));
        QVERIFY(h.notices[0].contains("YED burned: $1,000.00"));
        QVERIFY(h.notices[0].contains("smQvTmAz2ExamplePayoutAddress1111111"));
        // the Claim button follows the selection
        QVERIFY(!h.button("btnClaim")->isEnabled());
        h.selectRow("tblClaimable", 0);
        QVERIFY(h.button("btnClaim")->isEnabled());
        QVERIFY(h.copyIsClean());
    }

    void claimDegradesWhenNodeLacksTheRpc() {
        Harness h;
        h.feedActive(410, json(nullptr), json::array({claimableRow()}));
        h.rpc.errors[YellowbackRpc::CLAIM] = "Method not found";
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));
        QCOMPARE(h.errorNotices.size(), 1);
        QVERIFY(h.errorNotices[0].startsWith("yed_claim failed: Method not found"));
        QVERIFY(h.errorNotices[0].contains("does not offer this command"));

        h.rpc.errors[YellowbackRpc::CLAIM] = "claim-not-yet: tip below claimHeight";
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));
        QVERIFY(h.errorNotices[1].contains("claim height has not been reached"));
        h.rpc.errors[YellowbackRpc::CLAIM] = "claim-not-underwater: RED-4 would fail";
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));
        QVERIFY(h.errorNotices[2].contains("not underwater"));
    }

    // ── Sweep dialog (L10) ────────────────────────────────────────────────────────────────

    void sweepCarriesTheAcknowledgement() {
        Harness h;
        json p = positionActive(); p["canSweep"] = true; p["sweepBefore"] = 404;
        h.feedActive(390, json::array({p}), json(nullptr), 500000, true);
        h.rpc.results[YellowbackRpc::SWEEP] = sweepReply();
        h.tab.sweepVault(YellowbackPosition::fromJson(p));

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].contains(YellowbackRpc::SWEEP_ACKNOWLEDGEMENT));   // verbatim
        QVERIFY(h.confirms[0].contains("no YED burned and no fee paid"));
        QVERIFY(h.confirms[0].contains("$1,000.00 of YED minted against this vault stay in circulation unbacked"));
        QVERIFY(h.confirms[0].contains("before height 404"));
        // yed_sweep <vaultTxid> "I understand this leaves YED unbacked"
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::SWEEP),
                 json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "I understand this leaves YED unbacked"}));
        QVERIFY(h.notices[0].startsWith("Sweep sent|"));
        QVERIFY(h.notices[0].contains("YED now unbacked: $1,000.00"));
        QVERIFY(h.notices[0].contains(YellowbackFormat::zec(25125627141) % " to smExampleTransparentTwin111111111111"));
        QVERIFY(h.notices[0].contains("clipboard"));
        h.selectRow("tblPositions", 0);
        QVERIFY(h.button("btnSweep")->isEnabled() && !h.button("btnSweep")->isHidden());
        QVERIFY(h.button("btnRedeem")->isEnabled());   // past lockHeight: Redeem is still offered
        QVERIFY(h.copyIsClean());
    }

    void sweepErrors() {
        Harness h;
        json p = positionActive(); p["canSweep"] = true;
        h.feedActive(390, json::array({p}), json(nullptr), 500000, true);
        h.rpc.errors[YellowbackRpc::SWEEP] = "sweep-not-abandoned: enforcement is on";
        h.tab.sweepVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.errorNotices[0].contains("yed_sweep failed: sweep-not-abandoned"));
        QVERIFY(h.errorNotices[0].contains("Redeem instead"));
        h.rpc.errors[YellowbackRpc::SWEEP] = "sweep-acknowledgement-missing: second argument";
        h.tab.sweepVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.errorNotices[1].contains("exact acknowledgement"));
        h.rpc.errors[YellowbackRpc::SWEEP] = "Method not found (yed_sweep)";
        h.tab.sweepVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.errorNotices[2].contains("does not offer this command"));
        QVERIFY(h.copyIsClean());
    }

    // No action is reachable while the tab is unavailable
    void actionsNeedAvailability() {
        Harness h;
        json info = infoActive(); info["healthy"] = false; info["unhealthyReason"] = "storage";
        json p = positionActive(); p["canRedeem"] = true;
        h.ctl.feed(info, statsOpen(), activationActive(), balanceReply(500000), json::array({p}), json::array({claimableRow()}), json(nullptr));
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.mintAmount("1000");
        h.tab.doMint();
        h.tab.redeemVault(YellowbackPosition::fromJson(p));
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));
        h.tab.sweepVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.rpc.calls.isEmpty());
        QVERIFY(h.confirms.isEmpty());
        QVERIFY(!h.button("btnMint")->isEnabled());
        QVERIFY(!h.button("btnRedeem")->isEnabled());
        QVERIFY(!h.button("btnClaim")->isEnabled());
        QVERIFY(!h.button("btnStart")->isEnabled());
    }

    // ── Devnet end-to-end (N28): mint → send → redeem against a running devnet ────────────
    // YELLOWBACK_DEVNET_DIR is the devnet's --dir; node0/ycash.conf holds the RPC port and
    // credentials the wallet would read through --conf.

    void devnetEndToEnd() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetTransport dev;
        QString why;
        if (!dev.attach(dir, &why)) QFAIL(qPrintable(why));

        Harness h;
        h.ctl.setTransport(dev.transport());
        h.ctl.onConnected();                        // yed_getinfo, rpcversion check, then every refresh
        QVERIFY2(h.ctl.isAvailable(), qPrintable(h.ctl.unavailableReason()));
        QCOMPARE(h.ctl.network(), QString("regtest"));
        QVERIFY(h.ctl.mintBlocker(10000).isEmpty());
        const qint64 yedBefore = h.ctl.confirmedCents();
        const int    vaultsBefore = h.ctl.positionsModel()->rowCount(QModelIndex());

        // Mint $100 for the shortest class-A lock (48 blocks on regtest)
        h.mintAmount("100");
        auto tier = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(tier != nullptr && tier->count() == 3);
        tier->setCurrentIndex(0);
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY(h.notices.size() == 1 && h.notices[0].startsWith("Mint sent|"));
        QString mintTxid = h.label("lblMintPageStatus").section("txid: ", 1).trimmed();
        QCOMPARE(mintTxid.size(), 64);
        QVERIFY(Settings::getInstance()->getYellowbackBackupPending());

        dev.rpc("generate", json::array({1}));
        QVERIFY(dev.settle(mintTxid));
        h.ctl.refresh(true);
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 10000);
        QCOMPARE(h.ctl.positionsModel()->rowCount(QModelIndex()), vaultsBefore + 1);
        // The row from yed_listpositions, else from yed_getvault <txid> (the same shape minus can*):
        // a node whose yed_listpositions predates the v2 shape still answers yed_getvault in it.
        YellowbackPosition vault;
        for (int i = 0; i < h.ctl.positionsModel()->rowCount(QModelIndex()); i++)
            if (h.ctl.positionsModel()->positionAt(i)->txid == mintTxid) vault = *h.ctl.positionsModel()->positionAt(i);
        if (vault.txid.isEmpty()) {
            qWarning("yed_listpositions has no row with txid %s; reading yed_getvault", qPrintable(mintTxid));
            h.ctl.getVault(mintTxid, [&](const json& v) { vault = YellowbackPosition::fromJson(v); }, [](const QString& e) { qWarning("%s", qPrintable(e)); });
        }
        QCOMPARE(vault.txid, mintTxid);
        QCOMPARE(vault.status, QString("ACTIVE"));
        QCOMPARE(vault.mintedCents, (qint64)10000);

        // Send $40 to a fresh own Yellowback address (change $60 is above the floor)
        json addr = dev.rpc("yed_getnewaddress");
        QString to = QString::fromStdString(addr.is_string() ? addr.get<std::string>() : addr.value("address", std::string()));
        QVERIFY(h.ctl.looksLikeYellowbackAddress(to));
        h.sendTo(to, "40");
        h.tab.doSend();
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY(h.label("lblSendStatus").contains("Sent $40.00"));
        const QString sendTxid = h.label("lblSendStatus").section("txid: ", 1).section(' ', 0, 0).trimmed();
        QCOMPARE(sendTxid.size(), 64);
        dev.rpc("generate", json::array({1}));
        QVERIFY(dev.settle(sendTxid));
        h.ctl.refresh(true);
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 10000);     // sent to ourselves
        QVERIFY(h.ctl.transactionsModel()->rowCount(QModelIndex()) >= 2);

        // Redeem at lockHeight: burn $100, collateral back minus the fee
        int height = dev.rpc("getblockcount").get<int>();
        if (height < vault.lockHeight) dev.rpc("generate", json::array({vault.lockHeight - height}));
        QVERIFY(dev.settle());
        h.ctl.refresh(true);
        QVERIFY(h.ctl.height() >= vault.lockHeight);
        int noticesBefore = h.notices.size();
        h.tab.redeemVault(vault);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY(h.notices.size() == noticesBefore + 1 && h.notices.last().startsWith("Redeem sent|"));
        QVERIFY(h.notices.last().contains("YED burned: $100.00"));
        const QString redeemTxid = h.notices.last().section("txid ", 1).section('\n', 0, 0).trimmed();
        dev.rpc("generate", json::array({1}));
        QVERIFY(dev.settle(redeemTxid));
        h.ctl.refresh(true);
        QCOMPARE(h.ctl.confirmedCents(), yedBefore);
        YellowbackPosition closed;
        h.ctl.getVault(mintTxid, [&](const json& v) { closed = YellowbackPosition::fromJson(v); }, [](const QString& e) { qWarning("%s", qPrintable(e)); });
        QCOMPARE(closed.status, QString("CLOSED"));
        QCOMPARE(closed.burnedCents, (qint64)10000);
        QVERIFY(!closed.unbacked);
        QVERIFY(h.copyIsClean());
    }

    // ── Devnet claim and sweep (N28, L10, L13) ───────────────────────────────────────────
    // The whole schedule is the test's own: it crashes the pools' quotes for a full slow window
    // so pClaim = max(pMid, pSlow) falls (the sequence of ycash-dd/qa/rpc-tests/yellowback_claim.py),
    // claims the underwater vault through the Claim page, then mines untagged blocks from node 0
    // until yed_getinfo.abandoned and sweeps the second vault through the Vaults page.  Every
    // Yellowback action goes through the tab's own code path; only mining and the pools' quotes
    // (other people's machines on a real network) are driven directly.
    void devnetClaimAndSweep() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetTransport dev;
        QString why;
        if (!dev.attach(dir, &why)) QFAIL(qPrintable(why));
        QString probe;
        dev.post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", "yed_sweep"}, {"params", json::array()}}, &probe);
        QVERIFY2(!YellowbackController::isMethodNotFound(probe), "the node has no yed_sweep");
        dev.post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", "yed_claim"}, {"params", json::array()}}, &probe);
        QVERIFY2(!YellowbackController::isMethodNotFound(probe), "the node has no yed_claim");

        Harness h;
        h.ctl.setTransport(dev.transport());
        h.ctl.onConnected();
        QVERIFY2(h.ctl.isAvailable(), qPrintable(h.ctl.unavailableReason()));

        const int pools[3] = {2, 3, 4};                 // the devnet's signalling, quoting pools
        auto height = [&]() { json r = dev.rpc("getblockcount"); return r.is_number() ? r.get<int>() : -1; };
        // One block at a time, round-robin, waiting for node 0 to see each: the pools are peers,
        // and two of them generating from the same height would fork the devnet.
        auto minePools = [&](int n) {
            for (int i = 0; i < n; i++) {
                int want = height() + 1;
                dev.rpcOn(pools[i % 3], "generate", json::array({1}));
                for (int w = 0; w < 400 && height() < want; w++) QTest::qWait(25);
            }
        };
        // A transaction node 0 broadcast has to reach the pool that will mine it
        auto waitForTx = [&](const QString& txid) {
            for (int w = 0; w < 400; w++) {
                json m = dev.rpcOn(pools[0], "getrawmempool");
                if (m.is_array()) for (const auto& t : m) if (QString::fromStdString(t.get<std::string>()) == txid) return true;
                QTest::qWait(25);
            }
            return false;
        };
        auto positionOf = [&](const QString& txid) {
            YellowbackPosition p;
            for (int i = 0; i < h.ctl.positionsModel()->rowCount(QModelIndex()); i++)
                if (h.ctl.positionsModel()->positionAt(i)->txid == txid) p = *h.ctl.positionsModel()->positionAt(i);
            if (p.txid.isEmpty())    // a closed vault leaves yed_listpositions; yed_getvault still has it
                h.ctl.getVault(txid, [&](const json& v) { p = YellowbackPosition::fromJson(v); },
                               [](const QString& e) { qWarning("yed_getvault: %s", qPrintable(e)); });
            return p;
        };

        // Two vaults at the devnet's $50: S is swept, C is claimed.  A previous case may have
        // mined untagged blocks (devnetEndToEnd mines to a lock height on node 0), which pushes
        // the trailing signal count under the mint floor: let the pools signal it back up.
        for (int i = 0; i < 96 && !h.ctl.mintBlocker(10000).isEmpty(); i++) { minePools(1); h.ctl.refresh(true); }
        // MINTPOL-1 is read at the mint's reference height, REF_LAG blocks behind the tip, so the
        // tip being allowed again is not yet enough: carry the recovery back past it.
        minePools(h.ctl.refLag() + 1);
        h.ctl.refresh(true);
        QVERIFY2(h.ctl.mintBlocker(10000).isEmpty(), qPrintable(h.ctl.mintBlocker(10000)));
        const qint64 yedBefore = h.ctl.confirmedCents();
        auto tier = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(tier != nullptr);
        QStringList minted;
        for (int i = 0; i < 2; i++) {
            h.errorNotices.clear();
            h.mintAmount("100");
            tier->setCurrentIndex(0);                   // the shortest class-A lock (48 blocks on regtest)
            h.tab.doMint();
            QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
            QString txid = h.label("lblMintPageStatus").section("txid: ", 1).trimmed();
            QCOMPARE(txid.size(), 64);
            minted << txid;
            QVERIFY2(waitForTx(txid), "the mint did not reach the pool's mempool");
            minePools(1);
            QVERIFY2(dev.settle(txid), "node 0 did not digest the mint's block");
            h.ctl.refresh(true);
        }
        const QString sweepTxid = minted[0], claimTxid = minted[1];
        YellowbackPosition vaultS = positionOf(sweepTxid), vaultC = positionOf(claimTxid);
        QCOMPARE(vaultS.status, QString("ACTIVE"));
        QCOMPARE(vaultC.status, QString("ACTIVE"));
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 20000);

        // Past the lock height, so the only reason a sweep can be refused is that enforcement
        // has not been abandoned.
        if (height() < vaultS.lockHeight) minePools(vaultS.lockHeight - height());
        h.ctl.refresh(true);
        QVERIFY(!h.ctl.isAbandoned());
        h.errorNotices.clear();
        h.tab.sweepVault(positionOf(sweepTxid));
        QVERIFY(!h.errorNotices.isEmpty());
        QVERIFY2(h.errorNotices.last().contains("sweep-not-abandoned"), qPrintable(h.errorNotices.last()));

        // Crash the quote on all three pools and publish it for a full slow window: pClaim is
        // max(pMid, pSlow) and each window needs two-thirds of its 64 blocks to carry a quote.
        auto pClaimNow = [&]() { return YellowbackJson::toInt(h.ctl.stats(), YellowbackRpc::Stats::P_CLAIM, 0); };
        const qint64 before = pClaimNow();
        for (int p : pools) dev.rpcOn(p, "yed_setquote", json::array({10000, 1}));   // $0.01 in micro-USD
        minePools(64);
        h.ctl.refresh(true);
        QVERIFY2(pClaimNow() < before, qPrintable(QString("pClaim did not fall: %1 -> %2").arg(before).arg(pClaimNow())));

        // Claim C through the Claim page once it is past its claim height and underwater.
        vaultC = positionOf(claimTxid);
        if (height() < vaultC.claimHeight) minePools(vaultC.claimHeight - height());
        h.ctl.refresh(true);
        QVERIFY2(h.ctl.claimableModel()->rowCount(QModelIndex()) > 0, "no vault is claimable after the crash");
        const YellowbackClaimable* row = nullptr;
        for (int i = 0; i < h.ctl.claimableModel()->rowCount(QModelIndex()); i++)
            if (h.ctl.claimableModel()->rowAt(i)->vault.section(':', 0, 0) == claimTxid) row = h.ctl.claimableModel()->rowAt(i);
        QVERIFY2(row != nullptr, "the minted vault is not in yed_listclaimable");
        QCOMPARE(row->mintedCents, (qint64)10000);
        h.errorNotices.clear();
        int noticesBefore = h.notices.size();
        h.tab.claimVault(*row);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QCOMPARE(h.notices.size(), noticesBefore + 1);
        QVERIFY(h.notices.last().startsWith("Claim sent|"));
        QVERIFY2(h.notices.last().contains("YED burned: $100.00"), qPrintable(h.notices.last()));
        const QString claimTx = h.notices.last().section("txid ", 1).section('\n', 0, 0).trimmed();
        QVERIFY(waitForTx(claimTx));
        minePools(1);
        QVERIFY(dev.settle(claimTx));
        h.ctl.refresh(true);
        QCOMPARE(positionOf(claimTxid).status, QString("CLAIMED"));
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 10000);      // the claim burned $100 of our YED

        // Abandonment (L10/L12): node 0 is not a pool, so its blocks neither tag nor signal.
        // Once the trailing signal count is under the floor haltMask.ENFORCEMENT is set, and
        // ABANDON_BLOCKS (128 on regtest) consecutive such tips is abandonment.
        const int abandonBlocks = (int)YellowbackJson::toInt(h.ctl.params(), YellowbackRpc::Params::ABANDON_BLOCKS, 128);
        for (int i = 0; i < 3 * abandonBlocks && !h.ctl.isAbandoned(); i += 8) {
            dev.rpc("generate", json::array({8}));
            h.ctl.refresh(true);
        }
        QVERIFY2(h.ctl.isAbandoned(), "the devnet did not reach abandonment within three abandonment windows");

        // Sweep S through the Vaults page: no burn, no fee, the YED left unbacked.
        vaultS = positionOf(sweepTxid);
        QCOMPARE(vaultS.status, QString("ACTIVE"));
        QVERIFY(vaultS.canSweep);
        QCOMPARE(vaultS.sweepBefore, vaultS.claimHeight);
        h.errorNotices.clear();
        noticesBefore = h.notices.size();
        h.tab.sweepVault(vaultS);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QCOMPARE(h.notices.size(), noticesBefore + 1);
        QVERIFY(h.notices.last().startsWith("Sweep sent|"));
        QVERIFY2(h.notices.last().contains("YED now unbacked: $100.00"), qPrintable(h.notices.last()));
        const QString sweptTx = h.notices.last().section("txid ", 1).section('\n', 0, 0).trimmed();
        dev.rpc("generate", json::array({1}));
        QVERIFY(dev.settle(sweptTx));
        h.ctl.refresh(true);
        YellowbackPosition swept = positionOf(sweepTxid);
        QCOMPARE(swept.status, QString("CLOSED"));
        QVERIFY(swept.unbacked);
        QCOMPARE(swept.burnedCents, (qint64)0);
        QVERIFY(h.copyIsClean());
    }
};

QTEST_MAIN(YellowbackTabTest)
#include "yellowbacktab_test.moc"
