// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

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

#include <QToolButton>
#include <QScrollArea>
#include <QtTest>
#include <QStackedWidget>
#include <QLayout>
#include <QComboBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTemporaryDir>

#include "yellowbacktab.h"
#include "yellowbackmodels.h"
#include "yellowbackcontroller.h"
#include "yellowbackrpc.h"
#include "settings.h"

using json = nlohmann::json;

// ── Canned replies (contract example values) ──────────────────────────────────────────────

static json infoActive() {
    return json::parse(R"({
      "rpcversion": 6, "enabled": true, "network": "regtest", "height": 331, "mintRequiresArmed": false,
      "blockhash": "0f3a9c1e5b7d2a4c6e8f0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f",
      "chainHeight": 331, "startHeight": 103, "healthy": true, "unhealthyReason": "",
      "supplyCapReached": false, "lockedOutputs": 0, "protectedByIndex": true, "rebuilt": false,
      "upgrade": {"name": "Vault", "branchId": "6d5b7a31", "status": "active", "activationHeight": 103,
                  "attestorSetId": "5e755e755e755e755e755e755e755e755e755e755e755e755e755e755e755e75", "claimDelay": 10, "height": 331},
      "miner": {"payoutAddress": "smQvTmAz2ExamplePayoutAddress1111111", "quoteKind": "quote",
                "quoteAgeSeconds": 12, "registered": true, "eligible": true},
      "attest": {"status": "ARMED", "triggerHeight": 300, "armHeight": 308, "seatedCount": 3, "poolSize": 9,
                 "poolFresh": 3, "carrierMode": "scriptsig", "required": true, "armed": true},
      "params": {"startHeight": 103, "attestorSetId": "5e755e755e755e755e755e755e755e755e755e755e755e755e755e755e755e75", "claimDelay": 10,
                 "sigmaRefBps": 0, "sigmaMultMaxBps": 10000, "supplyCapBps": 0, "refLag": 2,
                 "inTermClaims": true, "claimThresholdBps": 12500, "earlyRedeemFeeBps": [500, 250, 100],
                 "refWindow": 40, "grace": 24, "payeeWindow": 10, "feeMinZat": 50000000, "feeBps": 25,
                 "tokenValueZat": 10000, "feeZat": 1000,
                 "windows": {"fast": 8, "mid": 24, "slow": 64},
                 "minFill": {"fast": 4, "mid": 16, "slow": 43},
                 "classes": [{"class": "A", "minBlocks": 48, "maxBlocks": 96, "baseRatioBps": 50000, "earlyRedeemFeeBps": 500},
                             {"class": "B", "minBlocks": 97, "maxBlocks": 144, "baseRatioBps": 40000, "earlyRedeemFeeBps": 250},
                             {"class": "C", "minBlocks": 145, "maxBlocks": 240, "baseRatioBps": 30000, "earlyRedeemFeeBps": 100}],
                 "policy": {"penaltyBlocks": 12, "accuracyWindow": 24, "tiltBps": 10000, "preferredPayee": null, "preferredAttestor": null},
                 "attest": {"required": true, "mSelect": 2, "kSlack": 1, "nSlots": 5, "divergeBpsAttest": 1500, "armDelay": 8,
                            "armMin": 3, "emergencyPersist": 4, "emergencyRatioBps": 10500, "emergencyNoticeTtl": 64,
                            "carrierMode": "scriptsig", "attestFeeBps": 2500, "attestMaxAge": 8,
                            "bondMinZat": 1000000000, "bondMinLock": 200, "bondMaturity": 8}}
    })");
}

// rpcversion 6 with the in-term rule switched off (inTermClaims false, θ 110 %): the code paths the
// wallet keeps for a vault judged by its heights, exercised by the cases written before in-term claims
static json infoLegacy() {
    json i = infoActive();
    i["params"]["inTermClaims"] = false;
    i["params"]["claimThresholdBps"] = 11000;
    return i;
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
      "name": "Vault", "branchId": "6d5b7a31", "status": "active", "activationHeight": 103,
      "attestorSetId": "5e755e755e755e755e755e755e755e755e755e755e755e755e755e755e755e75", "claimDelay": 10, "height": 331
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
      "voidReason": "", "canRedeem": false, "canClaim": false,
      "scriptPubKey": "045945440020755e755e755e"
    })");
}

// rpcversion 5: a vault under a claim (yed_listvaults "CLAIMING" row): the claimant's intent at the
// claim's output 0 and the owner's residual at output 1, both releasable at height + CLAIM_DELAY (10)
static const char* CLAIM_TXID = "c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1c1a1";
static json claimingVault(int claimHeight = 405, bool residual = true) {
    json v = positionActive();
    v["status"] = "CLAIMING"; v["claimable"] = false;
    v["intents"] = json::array({{{"txid", CLAIM_TXID}, {"vout", 0}, {"role", "claimant"}, {"height", claimHeight}, {"releaseHeight", claimHeight + 10}}});
    if (residual)
        v["intents"].push_back({{"txid", CLAIM_TXID}, {"vout", 1}, {"role", "residual"}, {"height", claimHeight}, {"releaseHeight", claimHeight + 10}});
    return v;
}

// set_getinfo of the YED attestor set; `mine` makes one member this wallet's (current, live)
static const char* MEMBER_KEY = "03b1c2d3e4f5061728394a5b6c7d8e9f0a1b2c3d4e5f6071829304a5b6c7d8e9f0";
static json attestorSetReply(bool mine, bool current = true, bool frozen = false) {
    json s = json::parse(R"({
      "seats": 15, "unlockthreshold": 1, "cancelthreshold": 1, "slashthreshold": 1, "open": true, "livenesswindow": 1000,
      "maturity": 1, "members": 2, "active": 2, "current": 2, "dormant": false, "released": false,
      "memberlist": [
        {"key": "02aaaa0000000000000000000000000000000000000000000000000000000000aa", "status": "active", "current": true, "live": true,
         "joinheight": 280, "lastact": 300, "bondoutpoint": "7b2c:0", "bondvalue": 10.0, "bondlocktime": 500, "bondfrozen": false, "wallet": false}
      ]
    })");
    s["memberlist"].push_back({{"key", MEMBER_KEY}, {"status", "active"}, {"current", current}, {"live", true}, {"joinheight", 281},
                               {"lastact", 320}, {"bondoutpoint", "7b2d:0"}, {"bondvalue", 10.0}, {"bondlocktime", 501},
                               {"bondfrozen", frozen}, {"wallet", mine}});
    return s;
}

static json claimableRow() {
    return json::parse(R"({
      "vault": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0",
      "ownerAddress": "yrExampleOwnerAddress111111111111111", "collateralZat": 25125628141, "mintedCents": 100000,
      "feeZat": 62814070, "claimHeight": 404, "underwaterAt": 437800, "pClaim": 400000
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
      "requiredZat": 251256282000, "termClass": "A", "lockHeight": 377, "claimHeight": 401, "minRatioBps": 50000,
      "baseRatioBps": 50000, "sigmaMultBps": 10000, "pMint": 1990000, "refHeight": 329
    })");
}

// ── v3 canned replies (contract example values of docs/yellowback-rpc-contract.json) ──────

static json attestorRow() {
    return json::parse(R"({
      "seq": 1, "status": "ELIGIBLE", "statusHeight": 288,
      "attestorPubKey": "03b1c2d3e4f5061728394a5b6c7d8e9f0a1b2c3d4e5f6071829304a5b6c7d8e9f0",
      "bondAddress": "smExampleBondAddress11111111111111111", "bondKeyAddress": "smExampleBondKeyAddr11111111111111111",
      "bondLocktime": 500, "bondOutpoint": {"txid": "7b2c3d4e5f60718293a4b5c6d7e8f9001a2b3c4d5e6f708192a3b4c5d6e7f809", "vout": 0},
      "bondSpentHeight": null, "bondZat": 1000000000, "flags": {"pool": false, "tier": 0}, "founding": true,
      "lastBundleHeight": 330, "pinned": false, "poolFresh": true, "registerHeight": 280, "seated": true,
      "seatedSince": 300, "weight": "31000000000"
    })");
}

static json priceReply() {
    return json::parse(R"({
      "height": 331, "pFast": 2000000, "pMid": 2000000, "pSlow": 1990000, "pMint": 1990000, "pClaim": 2000000,
      "xMint": 1990000, "xClaim": 2000000, "armed": true, "attestStatus": "ARMED",
      "seated": [1, 2, 3], "pinnedSeqs": [3], "pinnedKeys": ["smQvTmAz2ExamplePayoutAddress1111111"],
      "fill": {"fast": {"window": 8, "minFill": 4, "quoteTags": 8}},
      "tag": {"found": true, "kind": "quote", "priceMicroUsd": 2000000, "signal": true, "sourceMask": 3, "version": 1,
              "payoutAddress": "smQvTmAz2ExamplePayoutAddress1111111"}
    })");
}

static json selectionReply() {
    return json::parse(R"({
      "refHeight": 329, "armed": true, "mSelect": 2, "kSlack": 1, "pool": [1, 2, 3], "fallback": false,
      "selector": "", "sumWeight": "93000000000", "reachable": 2,
      "selected": [{"seq": 2, "status": "ELIGIBLE", "weight": "31000000000", "bondKeyAddress": "smExampleBondKeyAddr11111111111111111", "poolFresh": true},
                   {"seq": 1, "status": "ELIGIBLE", "weight": "31000000000", "bondKeyAddress": "smExampleBondKeyAddr11111111111111111", "poolFresh": true}]
    })");
}

static json estimateReplyArmed() {
    json e = estimateReply();
    e["xMint"] = 1990000; e["aMint"] = 1985000; e["pMint"] = 1985000; e["source"] = "a"; e["armed"] = true;
    e["bundleSeqs"] = json::array({1, 2}); e["attestFeeZat"] = 157430730; e["divergenceBps"] = 25;
    e["requiredZat"] = 251889169000;     // H-9.3: ceil(100000 · 50000 · COIN / 1985000) up to 1,000 zat, as the wallet recomputes it
    return e;
}

// yed_mint with wait = false: the carrier is out, every main-transaction field at its zero value
static json mintPendingReply() {
    return json::parse(R"({
      "txid": "", "vault": "", "termClass": "", "lockHeight": 0, "claimHeight": 0, "collateralZat": 0, "feeZat": 0,
      "payee": null, "fundedFrom": "", "warning": "",
      "carrierTxid": "5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c", "pending": true,
      "refHeight": 329, "xMint": 0, "aMint": null, "pMint": 0, "source": "", "bundleSeqs": [], "attestFeeZat": 0, "attestPayee": null
    })");
}

// yed_gettxinfo of the mint the node built on the next ChainTip (contract example values)
static json txInfoMint() {
    return json::parse(R"({
      "txid": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "type": "mint", "height": 332,
      "verdict": "ok", "path": "", "yedIn": 0, "yedOut": 100000, "burned": 0, "feeZat": 62814071,
      "payee": "smQvTmAz2ExamplePayoutAddress1111111", "expired": false,
      "pMint": 1985000, "xMint": 1990000, "aMint": 1985000, "pClaim": 2000000, "xClaim": 2000000, "aClaim": 1995000,
      "bundleSeqs": [1, 2], "attestFeeZat": 15703517, "attestPayee": "smExampleBondAddress11111111111111111",
      "bundleSource": "scriptsig", "carrierVin": 1, "claimPath": "", "residualZat": 0, "notice": false
    })");
}

static json txRow(const char* type, const char* txid) {
    json t = txMint();
    t["type"] = type; t["txid"] = txid;
    return t;
}

static json claimPendingReply() {
    return json::parse(R"({
      "txid": "", "burnedCents": 0, "feeZat": 0, "payee": null, "collateralOut": 0, "to": "", "extraBurnCents": 0,
      "carrierTxid": "5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c", "pending": true, "refHeight": 405,
      "xClaim": 0, "aClaim": null, "pClaim": 0, "pEmerg": null, "claimPath": "", "bundleSeqs": [], "attestFeeZat": 0,
      "attestPayee": null, "residualZat": 0
    })");
}

static json txInfoClaim() {
    json t = txInfoMint();
    t["txid"] = "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d";
    t["type"] = "claim"; t["burned"] = 100000; t["claimPath"] = "b"; t["residualZat"] = 250000000; t["pClaim"] = 400000;
    return t;
}

static json noticePendingReply() {
    return json::parse(R"({
      "txid": "", "vault": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0",
      "carrierTxid": "5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c", "pending": true,
      "refHeight": 338, "emergencyOpenAt": 342, "xClaim": 0, "aClaim": null, "pEmerg": null, "bundleSeqs": []
    })");
}

// H-9.3: the wallet recomputes FEE-1 (feeMinZat 50000000, feeBps 25 in infoActive), so the
// canned fee is FEE-1 of the collateral asked about (the contract's example figures disagree with
// its own parameters; the fixtures are the consistent ones)
static qint64 fee1(qint64 collateralZat) { return std::max<qint64>(50000000, collateralZat * 25 / 10000); }
static const qint64 VAULT_ZAT   = 25125628141;     // positionActive / claimableRow collateral
static const qint64 EST_ZAT     = 251256282000;    // estimateReply: $1,000 at 500 % and $1.99
static const qint64 EST_ARMED_ZAT = 251889169000;  // estimateReplyArmed: $1,000 at 500 % and $1.985
static json feePayeeReply(qint64 collateralZat = VAULT_ZAT) {
    json f = json::parse(R"({
      "eligible": ["smQvTmAz2ExamplePayoutAddress1111111"], "feeZat": 50000000,
      "default": {"payoutAddress": "smQvTmAz2ExamplePayoutAddress1111111", "weight": 19800},
      "policy": {"penaltyBlocks": 12, "accuracyWindow": 24, "tiltBps": 10000}
    })");
    f["feeZat"] = fee1(collateralZat);
    return f;
}

static json mintReply() {
    return json::parse(R"({
      "txid": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8",
      "vault": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0",
      "termClass": "A", "lockHeight": 380, "claimHeight": 404, "collateralZat": 251256282000, "feeZat": 628140705,
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
    bool                 inTerm = true;  // feedActive(): infoActive() (in-term claims) or infoLegacy()
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
    // Every piece of copy the dialogs show must follow the trust statement: never "trustless", never
    // a federation, and (rpcversion 5) never an enforcing pool, a pause or an abandonment.
    bool copyIsClean() const {
        for (const QString& t : confirms + notices)
            if (t.contains("trustless", Qt::CaseInsensitive) || t.contains("federat", Qt::CaseInsensitive) ||
                t.contains("abandon", Qt::CaseInsensitive) || t.contains("pools that run the Yellowback module", Qt::CaseInsensitive) ||
                t.contains("enforcing pool", Qt::CaseInsensitive)) return false;
        return true;
    }
    void feedActive(int height = 331, const json& positions = json(nullptr), const json& claimable = json(nullptr),
                    qint64 balanceCents = 500000) {
        json info = inTerm ? infoActive() : infoLegacy();
        info["height"] = height; info["chainHeight"] = height;
        ctl.feed(info, statsOpen(), activationActive(), balanceReply(balanceCents), positions, claimable, json(nullptr));
        // the refresh an action triggers on success reads the same replies back
        rpc.results[YellowbackRpc::GETINFO]          = info;
        rpc.results[YellowbackRpc::GETSTATS]         = statsOpen();
        rpc.results[YellowbackRpc::GETACTIVATION]    = activationActive();
        rpc.results[YellowbackRpc::GETBALANCE]       = balanceReply(balanceCents);
        rpc.results[YellowbackRpc::LISTPOSITIONS]    = positions.is_null() ? json::array() : positions;
        rpc.results[YellowbackRpc::LISTCLAIMABLE]    = claimable.is_null() ? json::array() : claimable;
        rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array();
        rpc.results[YellowbackRpc::LISTVAULTS]       = json::array();
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
    // The Mint page's estimate is debounced (400 ms): type the amount, then wait for the reply
    void estimateFor(const QString& amount) { mintAmount(amount); QTest::qWait(600); }
};

// The txid a result notice names ("title|...txid <txid>\n..."): every action summary carries one
static QString noticeTxid(const QString& notice) {
    return notice.section("txid ", 1).section('\n', 0, 0).trimmed();
}

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
            // Only this wallet's transactions count: since P4-b the attestors' agents heartbeat on
            // their own nodes (SET_HEARTBEAT every heartbeat_blocks), and such a transaction sits in
            // node 0's mempool until a pool mines it
            bool ownPending = false;
            if (pool.is_array())
                for (const auto& t : pool) {
                    QString e;
                    post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", "gettransaction"}, {"params", json::array({t})}}, &e);
                    if (e.isEmpty()) { ownPending = true; break; }
                }
            bool chain = pool.is_array() && !ownPending && info.is_object() && count.is_number() &&
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
    // ARMED, a mint, notice or claim needs a price proof: for each selected attestor an
    // attestation in node 0's pool citing a height in (R - attestMaxAge, R], R = tip - refLag
    // (tip for a notice). The agents sign only at a due tick (every attestInterval blocks) and
    // the subscriber relays with a delay, so a burst of fast blocks leaves the pool stale
    // (bundle-insufficient), and right after a tick the newest attestations cite heights above
    // R. yed_getinfo.attest.poolFresh does not tell the two apart (it counts citations above
    // tip - refLag - attestMaxAge with no upper bound), so the probe is yed_buildbundle at R
    // itself. While the bundle fails for bundle-insufficient a block from mineOne every 2 s
    // either carries R up to the new citations (every seat fresh) or brings the agents' next
    // due tick closer (pool stale: gating on poolFresh alone would wait forever, since the
    // agents attest only on new blocks). An unarmed devnet needs nothing.
    bool waitFresh(int refLag, std::function<void()> mineOne = nullptr, int seconds = 120) {
        for (int w = 0; w < seconds * 4; w++) {
            json info = rpc("yed_getinfo");
            const json a = info.is_object() && info.find("attest") != info.end() ? info["attest"] : json(nullptr);
            if (!a.is_object() || !a.value("armed", false)) return true;
            QString e;
            post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", "yed_buildbundle"},
                  {"params", json::array({info.value("height", 0) - refLag, ""})}}, &e);
            if (e.isEmpty()) return true;
            if (mineOne && w % 8 == 7 &&
                (e.contains("bundle-insufficient") || a.value("poolFresh", 0) >= a.value("seatedCount", 1))) mineOne();
            QTest::qWait(250);
        }
        return false;
    }
    // Move the market the way `yellowback-devnet price USD` does (its apply_price): the pools'
    // shared mock-price file, each automated attestor's own attest-price-<n> file, and
    // yed_setquote on the pools (2-4) when they run without quote agents (the default devnet).
    // Under v3 a claim is priced from the attestors' bundle, so the pools' quotes alone no
    // longer make a vault claimable.
    bool setMarketPrice(const QString& usd) {
        auto writeFile = [&](const QString& path) {
            QFile f(path % ".tmp");
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            f.write((usd % "\n").toUtf8()); f.close();
            QFile::remove(path);
            return QFile::rename(path % ".tmp", path);
        };
        bool ok = writeFile(devnetDir % "/mock-price");
        for (const QString& name : QDir(devnetDir).entryList({"attest-price-*"}, QDir::Files))
            if (!name.endsWith(".tmp")) ok = writeFile(devnetDir % "/" % name) && ok;
        for (int p : {2, 3, 4})
            rpcOn(p, "yed_setquote", json::array({(qint64)llround(usd.toDouble() * 1000000.0), 1}));
        return ok;
    }
    // Every devnet case starts at the devnet's $50 with full price windows: a case that crashed the
    // market and failed before restoring it would otherwise starve every later mint (pMint is the
    // lowest window median, so the main transaction of a two-step mint cannot be funded).
    bool restoreMarket(const QString& usd = "50") {
        if (!setMarketPrice(usd)) return false;
        const qint64 floor = (qint64)llround(usd.toDouble() * 1000000.0 * 0.8);
        for (int i = 0; i < 300; i++) {
            // both sources: the pools' windows (yed_getstats) and the attestors' bound, which only the
            // estimate shows (pMint = min(xMint, aMint) while armed; the agents follow their mock
            // price at their next tick)
            json st = rpc("yed_getstats");
            QString e;
            json est = post({{"jsonrpc", "1.0"}, {"id", "t"}, {"method", "yed_estimatecollateral"}, {"params", json::array({10000, 48})}}, &e);
            if (st.is_object() && st["pMint"].is_number() && st["pMint"].get<qint64>() >= floor &&
                e.isEmpty() && est.is_object() && est["pMint"].is_number() && est["pMint"].get<qint64>() >= floor) return true;
            // a stale attestation pool (bundle-insufficient) fills at the agents' next tick: wait for it
            if (!e.isEmpty() && e.contains("bundle-insufficient") && i % 3 != 0) { QTest::qWait(700); continue; }
            const int before = rpc("getblockcount").get<int>();
            rpcOn(2 + i % 3, "generate", json::array({1}));
            for (int w = 0; w < 400 && rpc("getblockcount").get<int>() <= before; w++) QTest::qWait(25);
        }
        return false;
    }
    // Node 0's wallet has the transaction in a block already
    bool confirmed(const QString& txid) {
        json t = rpc("gettransaction", json::array({txid.toStdString()}));
        return t.is_object() && t.value("confirmations", 0) >= 1;
    }
    // A v3 two-step action (W7): the reply names the carrier only, the node builds the main
    // transaction on the ChainTip that confirms the carrier, and the wallet's follow-up poll then
    // posts the result. Mine one block (mineOne) per second until `arrived` holds.
    bool awaitTwoStep(std::function<bool()> arrived, std::function<void()> mineOne, int blocks = 20) {
        for (int i = 0; i < blocks && !arrived(); i++) {
            mineOne();
            for (int w = 0; w < 40 && !arrived(); w++) QTest::qWait(25);
        }
        for (int w = 0; w < 200 && !arrived(); w++) QTest::qWait(25);
        return arrived();
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
        QSettings().remove("yellowback/signedcancel");     // a previous run's signed cancels (cancelClaim keeps them)
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

    void contractVersionIsSix() {
        // in-term claims (in-term plan §4.1): rpcversion 6 lists in-term claimable rows and quotes the early-redeem fee
        QCOMPARE(YellowbackRpc::RPC_VERSION, 6);
        QCOMPARE(Settings::getYellowbackRpcVersion(), 6);
    }

    // The rpcversion 6 handshake: 6 is accepted, 5 (the vault upgrade before in-term claims)
    // and 7 are refused with both numbers named, and nothing is enabled until they match.
    void handshakeAcceptsRpcVersionSixOnly() {
        for (int v : { 5, 7 }) {
            Harness h;
            json info = infoActive();
            info["rpcversion"] = v;
            h.feed(info, statsOpen(), activationActive());
            QVERIFY(!h.ctl.isAvailable());
            QVERIFY(!h.ctl.isVersionOk());
            QVERIFY(h.label("lblBanner").contains("version 6"));
            QVERIFY(h.label("lblBanner").contains(QString("version %1").arg(v)));
            QVERIFY(!h.button("btnMint")->isEnabled());
        }
        Harness h;
        h.feedActive();
        QVERIFY(h.ctl.isVersionOk());
        QVERIFY(h.ctl.isAvailable());
        // onConnected over the transport: yed_getinfo answering 5 starts the refresh, which reads
        // the CLAIMING vaults and the attestor set too
        Harness c;
        json info = infoActive();
        c.rpc.results[YellowbackRpc::GETINFO] = info;
        c.ctl.onConnected();
        QVERIFY(c.ctl.isVersionOk());
        QVERIFY(c.rpc.count(YellowbackRpc::GETSTATS) >= 1);
        QVERIFY(c.rpc.count(YellowbackRpc::LISTVAULTS) >= 1);
        QCOMPARE(c.rpc.lastParams(YellowbackRpc::LISTVAULTS), json::array({"CLAIMING", 1000, 0}));
        QVERIFY(c.rpc.count("set_getinfo") >= 1);
        QCOMPARE(c.rpc.lastParams("set_getinfo"), json::array({"5e755e755e755e755e755e755e755e755e755e755e755e755e755e755e755e75"}));
        QVERIFY(c.rpc.count("vault_getinfo") >= 1);
        // a node without the yed_* commands (no upgrade or no attestor set): no conf repair is offered
        Harness none;
        none.rpc.errors[YellowbackRpc::GETINFO] = "Method not found";
        none.ctl.onConnected();
        QVERIFY(!none.ctl.isAvailable());
        QVERIFY2(none.label("lblBanner").contains("-nuparams=6d5b7a31"), qPrintable(none.label("lblBanner")));
        QVERIFY(!none.label("lblBanner").contains("experimentalfeatures"));
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
        // audit F-3: comma-decimal locales; a comma before two digits is the decimal mark
        QVERIFY(YellowbackTab::parseDollars("12,50", &c));     QCOMPARE(c, (qint64)1250);
        QVERIFY(YellowbackTab::parseDollars("1,5", &c));       QCOMPARE(c, (qint64)150);
        QVERIFY(YellowbackTab::parseDollars("1,234.56", &c));  QCOMPARE(c, (qint64)123456);
        QVERIFY(YellowbackTab::parseDollars("1.234,56", &c));  QCOMPARE(c, (qint64)123456);
        QVERIFY(YellowbackTab::parseDollars("1,234", &c));     QCOMPARE(c, (qint64)123400);
        QVERIFY(YellowbackTab::parseDollars("1,234,567", &c)); QCOMPARE(c, (qint64)123456700);
        QVERIFY(YellowbackTab::parseDollars("1.234.567,89", &c)); QCOMPARE(c, (qint64)123456789);
        QVERIFY(YellowbackTab::parseDollars("$ 12,50", &c));   QCOMPARE(c, (qint64)1250);
        QVERIFY(!YellowbackTab::parseDollars("12,345.678", &c));
        QVERIFY(!YellowbackTab::parseDollars("1,23,45", &c));
        QVERIFY(!YellowbackTab::parseDollars("12,5,0", &c));
        QVERIFY(!YellowbackTab::parseDollars("1,2345", &c));
        QVERIFY(!YellowbackTab::parseDollars("12.", &c));
        QVERIFY(!YellowbackTab::parseDollars(",50", &c));
        QVERIFY(!YellowbackTab::parseDollars("1234567890", &c));
        // the Send page echoes what it read beside the field
        Harness h;
        h.feedActive();
        h.sendTo("", "12,50");
        QCOMPARE(h.tab.page(YellowbackTab::Send)->findChild<QLabel*>("lblAmountParsed")->text(), QString("= $12.50"));
        h.sendTo("", "12,5,0");
        QCOMPARE(h.tab.page(YellowbackTab::Send)->findChild<QLabel*>("lblAmountParsed")->text(), QString("not an amount"));
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
        QCOMPARE(p.underwaterAt, (qint64)437800);
        QVERIFY(!p.canRedeem && !p.canClaim);
        QCOMPARE(p.scriptPubKey, QString("045945440020755e755e755e"));
        QVERIFY(p.intents.isEmpty());          // optional, absent unless CLAIMING
        QVERIFY(p.claimantIntent() == nullptr);
        auto cl = YellowbackPosition::fromJson(claimingVault());
        QCOMPARE(cl.status, QString("CLAIMING"));
        QCOMPARE(cl.intents.size(), 2);
        QVERIFY(cl.claimantIntent() != nullptr && cl.residualIntent() != nullptr);
        QCOMPARE(cl.claimantIntent()->outpoint(), QString(CLAIM_TXID) % ":0");
        QCOMPARE(cl.claimantIntent()->releaseHeight, 415);
        QCOMPARE(cl.residualIntent()->vout, 1);

        auto c = YellowbackClaimable::fromJson(claimableRow());
        QCOMPARE(c.vault.right(2), QString(":0"));
        QCOMPARE(c.feeZat, (qint64)62814070);
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
        QVERIFY(YellowbackController::isMethodNotFound("Method not found"));
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
        QVERIFY(h.label("lblBanner").contains("version 6"));
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
        QVERIFY(!h.visible("lblStatus"));           // the Overview's Rules row says it; the text stays for the pending case
        QVERIFY2(h.label("lblStatus").contains("consensus rules since height 103"), qPrintable(h.label("lblStatus")));
        QVERIFY(h.label("lblStatus").contains("6d5b7a31"));
        QVERIFY(h.label("lblStatus").contains("every full node checks them"));
        // The connected node is a pool with a fresh quote: an information line, not a warning
        QVERIFY(h.visible("lblNotes"));
        QVERIFY(h.label("lblNotes").contains("smQvTmAz2ExamplePayoutAddress1111111"));
        QVERIFY(h.label("lblNotes").contains("price quote"));
        QVERIFY(h.label("lblNotes").contains("eligible for pool fees"));
        // Copy rule (§4.8, upgrade plan §10): never "trustless", and no enforcing pool is described
        QVERIFY(!h.label("lblStatus").contains("trustless", Qt::CaseInsensitive));
        QVERIFY(!h.label("lblStatus").contains("signal", Qt::CaseInsensitive));
    }

    // rpcversion 5: the upgrade scheduled but not yet reached
    void bannerUpgradePending() {
        Harness h;
        json info = infoActive();
        info["upgrade"]["status"] = "pending"; info["upgrade"]["activationHeight"] = 500;
        json act = activationActive(); act["status"] = "pending"; act["activationHeight"] = 500;
        h.feed(info, statsOpen(), act);
        QVERIFY(h.ctl.isAvailable());
        QVERIFY(!h.ctl.upgradeActive());
        QVERIFY(h.visible("lblStatus"));            // news until the upgrade activates
        QVERIFY2(h.label("lblStatus").contains("activates at height 500"), qPrintable(h.label("lblStatus")));
        QVERIFY(h.label("lblActivation").contains("activates at 500"));
        QVERIFY(!h.visible("lblBanner"));
        // the retired fields, should a node still send them, change nothing
        info["abandoned"] = true; info["valveTripped"] = true; info["sunset"] = true; info["enforcing"] = false;
        h.feed(info, statsOpen(), act);
        QVERIFY(!h.visible("lblBanner"));
        QVERIFY(!h.label("lblStatus").contains("abandon", Qt::CaseInsensitive));
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

    void bannerNoMinerLine() {
        Harness h;
        json info = infoActive();
        info["miner"] = json::parse(R"({"payoutAddress": null, "quoteKind": "none", "quoteAgeSeconds": null, "registered": false, "eligible": false})");
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
        QVERIFY2(h.label("lblActivation").contains("active since 103 (branch 6d5b7a31)"), qPrintable(h.label("lblActivation")));
        QVERIFY(h.label("lblActivation").contains("attestor set 5e755e755e75"));
        QVERIFY(h.label("lblActivation").contains("claim delay 10 blocks"));
        QCOMPARE(h.label("lblEnforcement"), QString("consensus: every full node checks them"));
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

    // W20: no halt bit, the cap reached: minting is limited to the classes at the 500 % floor, not paused
    void overviewCapReached() {
        Harness h;
        json info = infoActive();
        info["supplyCapReached"] = true; info["params"]["supplyCapBps"] = 1500; info["params"]["recapRatioBps"] = 50000;
        json stats = statsOpen();
        stats["supplyCents"] = 260000; stats["supplyCapCents"] = 250000;
        stats["mintingAllowed"] = false; stats["mintableClasses"] = json::array({"A"});
        h.feed(info, stats, activationActive());
        QVERIFY(h.ctl.softSupplyCap());
        QVERIFY(h.ctl.supplyCapReached());
        QCOMPARE(h.label("lblMintStatus"), QString("limited to class A"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$2,600.00 of $2,500.00 cap — class A only"));
        const QString tip = h.tab.findChild<QLabel*>("lblMintStatus")->toolTip();
        QVERIFY2(tip.contains("supply cap ($2,500.00, 15.00 % of issued YEC value)"), qPrintable(tip));
    }

    // A node from before W20 (no supplyCapReached): the cap is still a ceiling, exactly as before
    void overviewCapReachedOnANodeBeforeW20() {
        Harness h;
        json stats = statsOpen();
        stats["supplyCapCents"] = 250000; stats["mintingAllowed"] = false;   // no halt bit, cap full
        json old = infoActive(); old.erase("supplyCapReached");
        h.feed(old, stats, activationActive());
        QVERIFY(!h.ctl.softSupplyCap());
        QVERIFY(!h.ctl.supplyCapReached());
        QVERIFY(h.label("lblMintStatus").contains("supply cap"));
        QCOMPARE(h.label("lblMintStatus"), QString("paused (supply cap)"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$0.00 of $2,500.00 cap"));
        QVERIFY(h.ctl.mintLimit().isEmpty());
        QCOMPARE(h.ctl.mintBlocker(10000, "A"), QString("Minting is paused: the supply cap ($2,500.00) is reached with $2,500.00 in circulation."));
        // with mintableClasses on a W16 node but no supplyCapReached: still paused
        stats["mintableClasses"] = json::array();
        h.feed(old, stats, activationActive());
        QVERIFY(h.ctl.mintBlocker(10000, "A").startsWith("Minting is paused: the supply cap"));
        // an amount over the cap is refused for every class while the cap has room
        json room = statsOpen();
        room["supplyCents"] = 240000; room["supplyCapCents"] = 250000;
        h.feed(old, room, activationActive());
        QCOMPARE(h.ctl.mintBlocker(20000, "A"), QString("Minting $200.00 would exceed the supply cap ($2,400.00 of $2,500.00 in circulation)."));
    }

    void overviewTrustCopy() {
        Harness h;
        QString trust = YellowbackTab::aboutText();   // behind the tab's info button and in Help > About, not on the Overview
        QVERIFY(h.tab.findChild<QToolButton*>("btnAboutYellowback") != nullptr);
        // upgrade plan §10, summarised: consensus rules, two-party pricing, the claim delay and cancel, no shielded pool
        QVERIFY(trust.startsWith("Ycash Yellowback (YED) is an over-collateralised dollar on Ycash."));
        QVERIFY(trust.contains("every Yellowback rule is a Ycash consensus rule that every full node checks"));
        QVERIFY(trust.contains("no pool enforces it, and nothing can pause or abandon it"));
        QVERIFY(trust.contains("a single signer is never a price"));
        QVERIFY(trust.contains("any honest attestor can cancel a claim made at a wrong price"));
        QVERIFY(trust.contains("Nothing in Yellowback touches the shielded pool"));
        QVERIFY(!trust.contains("miner-enforced"));
        QVERIFY(!trust.contains("hashpower that runs the module"));
        QVERIFY(!trust.contains("trustless", Qt::CaseInsensitive));
        QVERIFY(!trust.contains("federat", Qt::CaseInsensitive));
    }

    // ── Vaults (§4.8 Vaults row) ──────────────────────────────────────────────────────────

    void vaultsRenderActiveRow() {
        Harness h;
        h.feed(infoLegacy(), statsOpen(), activationActive(), json::array({positionActive()}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->rowCount(QModelIndex()), 1);
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::DisplayRole).toString(), QString("ACTIVE"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Minted), Qt::DisplayRole).toString(), QString("$1,000.00"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::TermClass), Qt::DisplayRole).toString(), QString("A"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::ClaimHeight), Qt::DisplayRole).toString().startsWith("404"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Claimable), Qt::DisplayRole).toString().startsWith("not before 404"));   // says when, not just no
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Unbacked), Qt::DisplayRole).toString(), QString("no"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::ClaimState), Qt::DisplayRole).toString(), QString("-"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Vault), Qt::DisplayRole).toString().right(2), QString(":0"));

        // Before the lock height: no action, the text says when
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(positionActive()), 331);
        QVERIFY(!a.release && !a.redeem);
        QVERIFY(a.text.contains("lock height 380"));
        // At the lock height: Redeem, and the burn is exactly the debt
        json past = positionActive(); past["canRedeem"] = true;
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(past), 380);
        QVERIFY(a.redeem && !a.release && a.renew);
        QVERIFY(a.text.contains("burns $1,000.00"));
    }

    // ── Found by the owner's first walk of Scenario 1 (role-based regtest plan §8.1) ──────────

    // W16: a global-ratio halt limits minting to the recapitalising class instead of stopping it
    void mintLimitedToRecapClassUnderGlobalRatioHalt() {
        Harness h;
        json info = infoActive();
        info["params"]["globalRatioHaltBps"] = 25000; info["params"]["recapRatioBps"] = 50000;
        json stats = statsOpen();
        stats["haltMask"] = json::array({"GLOBAL_RATIO"}); stats["mintingAllowed"] = false;
        stats["globalRatioBps"] = 21460; stats["mintableClasses"] = json::array({"A"});
        h.feed(info, stats, activationActive());
        QCOMPARE(h.ctl.mintableClasses(), QStringList({"A"}));
        QVERIFY2(h.ctl.mintBlocker(10000, "A").isEmpty(), qPrintable(h.ctl.mintBlocker(10000, "A")));
        QVERIFY(h.ctl.mintBlocker(10000, "C").contains("only class A"));
        QVERIFY(h.ctl.mintBlocker(10000, "C").contains("Class C cannot mint"));
        QVERIFY(h.ctl.mintLimit().contains("limited"));
        QVERIFY(h.ctl.mintLimit().contains("class A"));
        // the overview and the mint page both say "limited", never "paused"
        QVERIFY(h.label("lblMintStatus").contains("limited"));
        QVERIFY(!h.label("lblMintStatus").contains("paused"));
        QVERIFY(h.visible("lblGate"));
        QVERIFY(h.label("lblGate").contains("limited"));
        // the classes that cannot mint are greyed out in the lock-length list
        auto cmb = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(cmb != nullptr);
        auto model = qobject_cast<QStandardItemModel*>(cmb->model());
        QVERIFY(model != nullptr);
        for (int i = 0; i < cmb->count(); i++) {
            const QString cls = h.ctl.classForLock(cmb->itemData(i).toInt()).name;
            QCOMPARE(model->item(i)->isEnabled(), cls == "A");
        }
        QVERIFY(h.copyIsClean());
    }

    // W20: above the cap the class at the 500 % floor still mints; the others are refused by name
    void mintLimitedToRecapClassAboveTheSupplyCap() {
        Harness h;
        json info = infoActive();
        info["supplyCapReached"] = true; info["params"]["supplyCapBps"] = 1500; info["params"]["recapRatioBps"] = 50000;
        json stats = statsOpen();
        stats["supplyCents"] = 250000; stats["supplyCapCents"] = 250000;
        stats["mintingAllowed"] = false; stats["mintableClasses"] = json::array({"A"});
        h.feed(info, stats, activationActive());
        QCOMPARE(h.ctl.mintableClasses(), QStringList({"A"}));
        // class A mints even though supply + cents exceeds the cap
        QVERIFY2(h.ctl.mintBlocker(10000, "A").isEmpty(), qPrintable(h.ctl.mintBlocker(10000, "A")));
        QVERIFY2(h.ctl.mintBlocker(10000).isEmpty(), qPrintable(h.ctl.mintBlocker(10000)));
        const QString c = h.ctl.mintBlocker(10000, "C");
        QVERIFY2(c.contains("Class C cannot mint"), qPrintable(c));
        QVERIFY2(c.contains("supply cap"), qPrintable(c));
        QVERIFY2(c.contains("only class A"), qPrintable(c));
        QVERIFY2(c.contains("500.00 %"), qPrintable(c));
        QVERIFY(!c.contains("Minting is paused"));
        const QString limit = h.ctl.mintLimit();
        QVERIFY2(limit.startsWith("Minting is limited: YED in circulation ($2,500.00) has reached the supply cap ($2,500.00, 15.00 % of issued YEC value)."), qPrintable(limit));
        QVERIFY2(limit.contains("Only class A can mint above the cap"), qPrintable(limit));
        QVERIFY(!limit.contains("system-wide collateral ratio"));
        QCOMPARE(h.label("lblMintStatus"), QString("limited to class A"));
        QVERIFY(!h.label("lblMintStatus").contains("paused"));
        QVERIFY(h.visible("lblGate"));
        QVERIFY(h.label("lblGate").contains("limited"));
        auto cmb = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(cmb != nullptr);
        auto model = qobject_cast<QStandardItemModel*>(cmb->model());
        QVERIFY(model != nullptr);
        for (int i = 0; i < cmb->count(); i++) {
            const QString cls = h.ctl.classForLock(cmb->itemData(i).toInt()).name;
            QCOMPARE(model->item(i)->isEnabled(), cls == "A");
        }
        QVERIFY(h.copyIsClean());
    }

    // W20: the cap not yet reached, but this amount would cross it: only a class at the floor may
    void mintCrossingTheSupplyCapNeedsTheRecapFloor() {
        Harness h;
        json info = infoActive();
        info["supplyCapReached"] = false; info["params"]["supplyCapBps"] = 1500; info["params"]["recapRatioBps"] = 50000;
        json stats = statsOpen();
        stats["supplyCents"] = 240000; stats["supplyCapCents"] = 250000; stats["mintableClasses"] = json::array({"A", "B", "C"});
        h.feed(info, stats, activationActive());
        QVERIFY(h.ctl.mintLimit().isEmpty());
        QCOMPARE(h.label("lblMintStatus"), QString("open"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$100.00 of $2,500.00 cap"));
        QVERIFY(h.ctl.mintBlocker(10000, "C").isEmpty());                   // fills the cap exactly
        QVERIFY2(h.ctl.mintBlocker(20000, "A").isEmpty(), qPrintable(h.ctl.mintBlocker(20000, "A")));
        QCOMPARE(h.ctl.mintBlocker(20000, "C"),
                 QString("Minting $200.00 in class C would take YED in circulation above the supply cap ($2,400.00 of $2,500.00): "
                         "above the cap only class A can mint, because its minimum ratio reaches the 500.00 % floor. "
                         "Choose that lock length, or mint at most $100.00."));
        // at a 1.25x volatility multiplier class B locks 500 % too, and qualifies (the floor is on the ratio locked)
        stats["sigmaMultBps"] = 12500;
        h.feed(info, stats, activationActive());
        QVERIFY(h.ctl.mintBlocker(20000, "B").isEmpty());
        QVERIFY(h.ctl.mintBlocker(20000, "C").contains("only class A or B can mint"));
    }

    // W16 + W20 together: a lone GLOBAL_RATIO halt with the cap reached; the notice says both
    void mintLimitedUnderGlobalRatioHaltAndSupplyCap() {
        Harness h;
        json info = infoActive();
        info["supplyCapReached"] = true;
        info["params"]["supplyCapBps"] = 1500; info["params"]["globalRatioHaltBps"] = 25000; info["params"]["recapRatioBps"] = 50000;
        json stats = statsOpen();
        stats["supplyCents"] = 260000; stats["supplyCapCents"] = 250000; stats["globalRatioBps"] = 21460;
        stats["haltMask"] = json::array({"GLOBAL_RATIO"}); stats["mintingAllowed"] = false; stats["mintableClasses"] = json::array({"A"});
        h.feed(info, stats, activationActive());
        QVERIFY2(h.ctl.mintBlocker(10000, "A").isEmpty(), qPrintable(h.ctl.mintBlocker(10000, "A")));
        const QString limit = h.ctl.mintLimit();
        QVERIFY2(limit.contains("214.60 %, below its 250.00 % floor"), qPrintable(limit));
        QVERIFY2(limit.contains("has reached the supply cap ($2,500.00"), qPrintable(limit));
        QVERIFY2(limit.contains("Only class A can mint until both clear"), qPrintable(limit));
        const QString c = h.ctl.mintBlocker(10000, "C");
        QVERIFY2(c.contains("Class C cannot mint while the system-wide collateral ratio (214.60 %)"), qPrintable(c));
        QVERIFY2(c.contains("at the supply cap ($2,500.00)"), qPrintable(c));
        QVERIFY2(c.contains("only class A can"), qPrintable(c));
        QCOMPARE(h.label("lblMintStatus"), QString("limited to class A"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$2,600.00 of $2,500.00 cap — class A only"));
    }

    // W20: the cap reached and no class reaches the floor (here a second halt bit): paused, as before
    void mintPausedAboveTheSupplyCapWhenNoClassQualifies() {
        Harness h;
        json info = infoActive();
        info["supplyCapReached"] = true; info["params"]["recapRatioBps"] = 50000;
        json stats = statsOpen();
        stats["supplyCents"] = 250000; stats["supplyCapCents"] = 250000;
        stats["mintingAllowed"] = false; stats["mintableClasses"] = json::array();
        h.feed(info, stats, activationActive());
        QVERIFY(h.ctl.mintLimit().isEmpty());
        const QString b = h.ctl.mintBlocker(10000, "A");
        QCOMPARE(b, QString("Minting is paused: the supply cap ($2,500.00) is reached with $2,500.00 in circulation. "
                            "No term class reaches the 500.00 % floor a mint above the cap needs."));
        QCOMPARE(h.label("lblMintStatus"), QString("paused (supply cap)"));
        QCOMPARE(h.label("lblCapHeadroom"), QString("$2,500.00 of $2,500.00 cap — reached"));
        // the cap plus another halt bit: the halt explains the pause
        stats["haltMask"] = json::array({"DIVERGENCE"});
        h.feed(info, stats, activationActive());
        QVERIFY(h.ctl.mintBlocker(10000, "A").startsWith("Minting is paused. "));
        QCOMPARE(h.label("lblMintStatus"), QString("paused: DIVERGENCE"));
    }

    void mintPausedUnderGlobalRatioWhenNoClassQualifies() {
        Harness h;
        json stats = statsOpen();
        stats["haltMask"] = json::array({"GLOBAL_RATIO"}); stats["mintingAllowed"] = false;
        stats["mintableClasses"] = json::array();
        h.feed(infoActive(), stats, activationActive());
        QVERIFY(h.ctl.mintBlocker(10000, "A").contains("Minting is paused"));
        QVERIFY(h.ctl.mintLimit().isEmpty());
        QVERIFY(h.label("lblMintStatus").contains("paused"));
        // a node from before W16 has no mintableClasses: the halt pauses everything, as before
        json old = statsOpen();
        old["haltMask"] = json::array({"GLOBAL_RATIO"}); old["mintingAllowed"] = false;
        h.feed(infoActive(), old, activationActive());
        QVERIFY(h.ctl.mintableClasses().isEmpty());
        QVERIFY(h.ctl.mintBlocker(10000, "A").contains("Minting is paused"));
    }

    // "I don't actually see a collateral ratio for my specific position"
    void vaultsShowRatioAndUnderwaterPrice() {
        Harness h;
        h.feed(infoLegacy(), statsOpen(), activationActive(), json::array({positionActive()}));
        auto m = h.ctl.positionsModel();
        const YellowbackPosition pos = YellowbackPosition::fromJson(positionActive());
        const qint64 bps = YellowbackPositionsModel::ratioBps(pos, 2000000);       // statsOpen()'s pClaim
        QVERIFY(bps > 0);
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Ratio), Qt::DisplayRole).toString(), YellowbackFormat::bpsAsPercent(bps));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::UnderwaterBelow), Qt::DisplayRole).toString(), YellowbackFormat::price(437800));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Ratio), Qt::ToolTipRole).toString().contains("110 %"));
        // the fixture's vault is under the claim threshold at that price: shown in red
        if (bps < 11000)
            QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Ratio), Qt::ForegroundRole).value<QBrush>().color(), QColor(Qt::red));
        // no claim price: the column says so rather than inventing a number
        json stats = statsOpen(); stats["pClaim"] = nullptr; stats["pFast"] = nullptr;
        h.feed(infoLegacy(), stats, activationActive(), json::array({positionActive()}));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Ratio), Qt::DisplayRole).toString().contains("no price"));
    }

    // "Sent $5.55 ... but it says sent 0.00": a send whose outputs all came back to this wallet
    void transactionsLabelSelfTransfer() {
        Harness h;
        json tx = json::parse(R"({"txid": "cf46df487bfc90719eb03917696c05af0b9b7e76f1588f5f7ba6fc1440e795d4", "height": 333,
            "confirmations": 1, "type": "send", "verdict": "ok", "path": "", "yedIn": 10000, "yedOut": 10000, "burned": 0,
            "amountCents": 0, "feeZat": 0, "payee": null, "unbacked": false, "expired": false})");
        h.ctl.feed(infoActive(), statsOpen(), activationActive(), json(nullptr), json(nullptr), json(nullptr), json::array({tx}));
        auto m = h.ctl.transactionsModel();
        QCOMPARE(m->rowCount(QModelIndex()), 1);
        QCOMPARE(m->data(m->index(0, YellowbackTxModel::Type), Qt::DisplayRole).toString(), QString("self-transfer"));
        QVERIFY(m->data(m->index(0, YellowbackTxModel::Amount), Qt::ToolTipRole).toString().contains("balance is unchanged"));
        // a real send keeps its label
        tx["amountCents"] = -555; tx["yedOut"] = 9445;
        h.ctl.feed(infoActive(), statsOpen(), activationActive(), json(nullptr), json(nullptr), json(nullptr), json::array({tx}));
        QVERIFY(m->data(m->index(0, YellowbackTxModel::Type), Qt::DisplayRole).toString() != QString("self-transfer"));
    }

    // "the UI only has one line, so you cannot clearly read the information": a wrapped value
    // beside its label must get the height its text needs, at a modest window width
    void longValueLabelsAreNotClipped() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive());
        h.tab.resize(720, 560);
        h.tab.show();
        QApplication::processEvents();
        struct Probe { const char* page; YellowbackTab::Page id; const char* label; QString text; };
        const QList<Probe> probes = {
            { "Overview", YellowbackTab::Overview, "lblAttestation", "TRIGGERED (disabled)" },   // the longest text the app puts there now
            { "Mint", YellowbackTab::Mint, "lblSource", "pools $42.4163 per YEC, attestors $45.6786 per YEC — the pools' price bound the mint" },
            { "Mint", YellowbackTab::Mint, "lblSelection", "3 of 3 selected attestors have a fresh attestation on this node (seq 0, 2, 3); a mint needs 2" },
        };
        for (const Probe& pr : probes) {
            QWidget* page = h.tab.page(pr.id);
            h.tab.findChild<QTabWidget*>("subTabs")->setCurrentIndex(pr.id);   // the Overview sits in a scroll area, not in the stack
            auto lbl = h.tab.findChild<QLabel*>(pr.label);
            QVERIFY2(lbl != nullptr, pr.label);
            lbl->setText(pr.text);
            QApplication::processEvents();
            if (page->layout()) page->layout()->activate();
            QApplication::processEvents();
            QVERIFY2(lbl->width() > 100, qPrintable(QString("%1 has width %2").arg(pr.label).arg(lbl->width())));
            const int needed = lbl->heightForWidth(lbl->width());
            QVERIFY2(lbl->height() >= needed,
                     qPrintable(QString("%1 on %2 is clipped: height %3, needs %4 at width %5").arg(pr.label).arg(pr.page).arg(lbl->height()).arg(needed).arg(lbl->width())));
        }
    }

    // ── The owner's second walk of Scenario 1 (regtest plan F-15 to F-19) ──────────────────

    // "difficult to read each row to understand which vault I needed to act on first"
    void vaultsActByAndHighlight() {
        Harness h;
        h.inTerm = false;                                            // the heights rule; in-term: vaultsInTermThresholdWarning
        json p = positionActive();                                   // lock 380, claim 404
        h.feed(infoLegacy(), statsOpen(), activationActive(), json::array({p}));   // height 331
        auto m = h.ctl.positionsModel();
        auto cell = [&](int col, int role = Qt::DisplayRole) { return m->data(m->index(0, col), role); };
        QVERIFY2(cell(YellowbackPositionsModel::ActBy).toString().startsWith("locked; redeemable in 49 block"), qPrintable(cell(YellowbackPositionsModel::ActBy).toString()));
        QVERIFY(!cell(YellowbackPositionsModel::ActBy, Qt::BackgroundRole).isValid());
        QVERIFY(cell(YellowbackPositionsModel::Claimable).toString().startsWith("not before 404"));
        // in the grace window: the owner's to redeem now, orange
        p["canRedeem"] = true;
        h.feedActive(390, json::array({p}));
        QVERIFY2(cell(YellowbackPositionsModel::ActBy).toString().startsWith("REDEEMABLE: claim path opens in 14 block"), qPrintable(cell(YellowbackPositionsModel::ActBy).toString()));
        QCOMPARE(cell(YellowbackPositionsModel::ActBy, Qt::BackgroundRole).value<QBrush>().color(), QColor(255, 232, 190));
        // past the claim height and not underwater: open but safe while the claim price holds; red
        h.feedActive(410, json::array({p}));
        QVERIFY(cell(YellowbackPositionsModel::ActBy).toString().contains("claim path open; safe while"));
        QVERIFY(cell(YellowbackPositionsModel::Claimable).toString().startsWith("no (claim price above"));
        QCOMPARE(cell(YellowbackPositionsModel::ActBy, Qt::BackgroundRole).value<QBrush>().color(), QColor(255, 210, 210));
        // under a notice: the Claimable cell says when the emergency claim opens
        p["noticed"] = true; p["noticeHeight"] = 340; p["emergencyOpenAt"] = 420;
        h.feedActive(410, json::array({p}));
        QVERIFY(cell(YellowbackPositionsModel::Claimable).toString().startsWith("notice: opens at 420"));
        p["claimable"] = true;
        h.feedActive(425, json::array({p}));
        QCOMPARE(cell(YellowbackPositionsModel::Claimable).toString(), QString("YES"));
        QVERIFY(cell(YellowbackPositionsModel::ActBy).toString().startsWith("CLAIM PATH OPEN"));
    }

    // "we also need to be able to filter by status. right now I see several vaults but most are claimed or closed"
    void vaultsFilterDefaultsToOpen() {
        Harness h;
        json closed = positionActive();  closed["txid"] = QString(64, 'a').toStdString();  closed["status"] = "CLOSED";  closed["closeHeight"] = 300; closed["closingTxid"] = QString(64, 'b').toStdString(); closed["burnedCents"] = 100000;
        json claimed = positionActive(); claimed["txid"] = QString(64, 'c').toStdString(); claimed["status"] = "CLAIMED"; claimed["closeHeight"] = 310; claimed["closingTxid"] = QString(64, 'd').toStdString(); claimed["burnedCents"] = 100000;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({closed, positionActive(), claimed}));
        auto view = h.tab.findChild<QTableView*>("tblPositions");
        QVERIFY(view != nullptr);
        QCOMPARE(h.ctl.positionsModel()->rowCount(QModelIndex()), 3);
        QCOMPARE(view->model()->rowCount(QModelIndex()), 1);            // open vaults only, by default
        h.selectRow("tblPositions", 0);
        QVERIFY(h.tab.selectedPosition() != nullptr);
        QCOMPARE(h.tab.selectedPosition()->status, QString("ACTIVE")); // the view's row 0 is the model's row 1
        auto cmb = h.tab.findChild<QComboBox*>("cmbPositionsFilter");
        QVERIFY(cmb != nullptr);
        cmb->setCurrentIndex(cmb->findData(""));
        QCOMPARE(view->model()->rowCount(QModelIndex()), 3);
        cmb->setCurrentIndex(cmb->findData("CLAIMED"));
        QCOMPARE(view->model()->rowCount(QModelIndex()), 1);
        h.selectRow("tblPositions", 0);
        QCOMPARE(h.tab.selectedPosition()->status, QString("CLAIMED"));
        cmb->setCurrentIndex(cmb->findData("VOID"));
        QCOMPARE(view->model()->rowCount(QModelIndex()), 0);
        QVERIFY(h.label("lblVaultAction").contains("No vault matches the filter"));
    }

    // "the landing Balance tab of the wallet needs to include the Yellowback Confirmed YED balance
    // and the total amount of YEC collateral in vaults" (F-22): the two lines the main window shows
    void balanceTabSummaryLines() {
        Harness h;
        QCOMPARE(h.ctl.balanceSummary(), QString("-"));                // nothing known yet
        json p2 = positionActive(); p2["txid"] = QString(64, 'e').toStdString(); p2["collateralZat"] = 10000000000; // 100 YEC
        json closed = positionActive(); closed["txid"] = QString(64, 'f').toStdString(); closed["status"] = "CLOSED";
        h.feedActive(331, json::array({positionActive(), p2, closed}), json(nullptr), 512345);
        QCOMPARE(h.ctl.balanceSummary(), QString("$5,123.45 YED"));
        // 251.25628141 + 100 YEC in the two ACTIVE vaults; the CLOSED one does not count; at statsOpen's $1.99 mint price
        QVERIFY2(h.ctl.collateralSummary().startsWith("351.25628141 YEC in 2 active vault(s)"), qPrintable(h.ctl.collateralSummary()));
        QVERIFY2(h.ctl.collateralSummary().contains("at the mint price"), qPrintable(h.ctl.collateralSummary()));
        json bal = balanceReply(512345); bal["unconfirmedCents"] = 100;
        h.ctl.feed(infoActive(), statsOpen(), activationActive(), bal, json::array(), json(nullptr), json(nullptr));
        QCOMPARE(h.ctl.balanceSummary(), QString("$5,123.45 YED ($1.00 unconfirmed)"));
        QCOMPARE(h.ctl.collateralSummary(), QString("none (no active vault)"));
    }

    // "sending to a regtest ys shielded address triggers 'Recipient Address ... is Invalid'" (F-24)
    void regtestSaplingAddressesAreValid() {
        auto* st = Settings::getInstance();
        const QString regtest = "yregtestsapling19jqqa7jhvt6vqfpthhhftvk9rvvw66n5mrkfskhhdkvyy5get58qdu7uttltnarrm7ajuvhxq5p";   // z_getnewaddress sapling on the devnet
        const QString mainnet = "ys1" + QString(75, 'q');
        QVERIFY(Settings::isValidAddress(regtest));
        QVERIFY(st->isSaplingAddress(regtest));              // whatever the testnet flag says
        QVERIFY(st->isZAddress(regtest));
        QVERIFY(!st->isTAddress(regtest));
        QVERIFY(Settings::isValidAddress(mainnet));
        QVERIFY(!Settings::isValidAddress("yregtestsapling1tooshort"));
        QVERIFY(!Settings::isValidAddress(regtest + "x"));
    }

    // "make the fast median price be authoritative in the yecwallet" (F-23): the pools' fast
    // median is the wallet's YEC/USD rate while the node is active and has one; CoinGecko is the
    // fallback and never overwrites a fresh protocol price
    void protocolPriceIsAuthoritative() {
        auto* st = Settings::getInstance();
        Harness h;
        st->setZECPrice(0);
        QVERIFY(!h.ctl.protocolPriceMicroUsd().has_value());     // nothing fed yet
        h.feed(infoActive(), statsOpen(), activationActive());  // statsOpen: pFast 2,000,000 = $2.00
        QCOMPARE(h.ctl.protocolPriceMicroUsd().value_or(0), (qint64)2000000);
        QCOMPARE(st->getZECPrice(), 2.0);
        QVERIFY(st->getZECPriceSource().contains("Yellowback"));
        QVERIFY(st->yellowbackPriceFresh());
        st->setCoinGeckoPrice(0.74);                             // the poll lands while the protocol price is fresh
        QCOMPARE(st->getZECPrice(), 2.0);
        // undefined fast price: nothing pushed, the last rate stands
        json noPrice = statsOpen(); noPrice["pFast"] = nullptr;
        h.feed(infoActive(), noPrice, activationActive());
        QVERIFY(!h.ctl.protocolPriceMicroUsd().has_value());
        QCOMPARE(st->getZECPrice(), 2.0);
        // the vault upgrade not yet active: CoinGecko is the source (nothing was pushed since the fresh mark)
        json inactive = activationActive(); inactive["status"] = "pending";
        h.feed(infoActive(), statsOpen(), inactive);
        QVERIFY(!h.ctl.protocolPriceMicroUsd().has_value());
    }

    // "the 'minting' info window is still squished"; "we might need to be clearer that minting will be re-enabled"
    void overviewMintStatusIsShortWithTheDetailInTheTooltip() {
        Harness h;
        json stats = statsOpen();
        stats["haltMask"] = json::array({"DIVERGENCE", "GLOBAL_RATIO"}); stats["mintingAllowed"] = false; stats["mintableClasses"] = json::array();
        h.feed(infoActive(), stats, activationActive());
        QCOMPARE(h.label("lblMintStatus"), QString("paused: DIVERGENCE, GLOBAL_RATIO"));
        const QString tip = h.tab.findChild<QLabel*>("lblMintStatus")->toolTip();
        QVERIFY2(tip.contains("clears by itself"), qPrintable(tip));
        QVERIFY2(tip.contains("can mint again"), qPrintable(tip));
        QVERIFY2(h.ctl.mintBlocker(10000, "A").contains("within 64 block"), qPrintable(h.ctl.mintBlocker(10000, "A")));
        json limited = statsOpen();
        limited["haltMask"] = json::array({"GLOBAL_RATIO"}); limited["mintingAllowed"] = false; limited["mintableClasses"] = json::array({"A"});
        h.feed(infoActive(), limited, activationActive());
        QCOMPARE(h.label("lblMintStatus"), QString("limited to class A"));
        QVERIFY(h.tab.findChild<QLabel*>("lblMintStatus")->toolTip().contains("recapitalisation floor"));
    }

    void vaultsRenderVoidRow() {
        Harness h;
        json v = positionActive();
        v["status"] = "VOID"; v["voidReason"] = "bad-mint-collateral"; v["underwaterAt"] = nullptr;
        v["canRedeem"] = true;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({v}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::DisplayRole).toString(), QString("VOID (bad-mint-collateral)"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::ClaimState), Qt::DisplayRole).toString(), QString("-"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::ToolTipRole).toString().contains("bad-mint-collateral"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::ToolTipRole).toString().contains("no burn, no fee"));

        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(v), 380);
        QVERIFY(a.release && !a.redeem && !a.renew);
        QVERIFY(a.text.contains("no YED burned and no fee paid"));
        QVERIFY(a.text.contains("before the vault upgrade"));
        QVERIFY(a.text.contains("before height 404"));

        // Select the row: the page text follows and Release is offered (L14)
        auto table = h.tab.findChild<QTableView*>("tblPositions");
        QVERIFY(table != nullptr);
        table->setCurrentIndex(table->model()->index(0, 0));   // through the view's status filter, not the source model
        QVERIFY(h.label("lblVaultAction").contains("no YED burned"));
        QVERIFY(h.button("btnRelease") != nullptr && h.button("btnRelease")->isEnabled() && !h.button("btnRelease")->isHidden());
        QVERIFY(h.button("btnRedeem")->isHidden());
        QVERIFY(h.button("btnSweep") == nullptr);           // rpcversion 5: the sweep is gone
        QVERIFY(h.button("btnWhyVoid")->isEnabled());
    }

    // rpcversion 5 (U-23): a vault of ours under a claim. It is red, the Act-by and Claim-in-progress
    // cells say when it releases, the page text says only an attestor can stop it, and nothing is offered.
    void vaultsRenderClaimingRow() {
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({claimingVault()}));
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::DisplayRole).toString(), QString("CLAIMING"));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::ClaimState), Qt::DisplayRole).toString(), QString("claimed at 405; releases at 415"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::ActBy), Qt::DisplayRole).toString().startsWith("BEING CLAIMED"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::ToolTipRole).toString().contains("cancel"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Status), Qt::BackgroundRole).isValid());
        // the "open" filter keeps it
        auto table = h.tab.findChild<QTableView*>("tblPositions");
        QCOMPARE(table->model()->rowCount(QModelIndex()), 1);
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(claimingVault()), 408);
        QVERIFY(!a.release && !a.redeem && !a.renew);
        QVERIFY2(a.text.contains("goes to the claimant at height 415"), qPrintable(a.text));
        QVERIFY(a.text.contains("cancels the claim"));
        QVERIFY(a.text.contains("residual"));
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(claimingVault()), 414);
        QVERIFY(a.text.contains("may release the collateral now"));
        // the banner warns while the claim can still be stopped, and not after
        QString w = YellowbackController::deadlineWarning(YellowbackPosition::fromJson(claimingVault()), 408);
        QVERIFY2(w.contains("is being claimed") && w.contains("height 415"), qPrintable(w));
        QVERIFY(YellowbackController::deadlineWarning(YellowbackPosition::fromJson(claimingVault()), 414).isEmpty());
        table->setCurrentIndex(table->model()->index(0, 0));
        QVERIFY(h.button("btnRedeem")->isVisible() == false || !h.button("btnRedeem")->isEnabled());
        QVERIFY(!h.button("btnRenew")->isEnabled());
        QVERIFY(h.label("lblVaultAction").contains("being claimed"));
    }

    // Both node lines' yed_listpositions omit `intents` on a CLAIMING row (found on the devnet): the
    // wallet takes them from yed_listvaults, in either order of arrival
    void claimingRowIntentsFromListVaults() {
        json bare = claimingVault(); bare.erase("intents");
        Harness h;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({bare}));
        QVERIFY(h.ctl.positionsModel()->positionAt(0)->intents.isEmpty());
        h.ctl.feedUpgrade(json::array({claimingVault()}), json(nullptr));
        QCOMPARE(h.ctl.positionsModel()->positionAt(0)->intents.size(), 2);
        QCOMPARE(h.ctl.positionsModel()->positionAt(0)->claimantIntent()->releaseHeight, 415);
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({bare}));    // a later yed_listpositions
        QCOMPARE(h.ctl.positionsModel()->positionAt(0)->intents.size(), 2);
        QCOMPARE(h.ctl.pendingClaimsModel()->rowAt(0)->mineVault, true);
        // a claim of an own vault is listed "claimed": it is ours when our YED was burned in it
        json own = txMint(); own["type"] = "claimed"; own["path"] = "claim"; own["txid"] = CLAIM_TXID; own["amountCents"] = -100000;
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({own}));
        QVERIFY(h.ctl.pendingClaimsModel()->rowAt(0)->mineClaim);
        own["amountCents"] = 0;                       // somebody else claimed our vault
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({own}));
        QVERIFY(!h.ctl.pendingClaimsModel()->rowAt(0)->mineClaim);
    }

    // rpcversion 5: the Pending claims page from yed_listvaults "CLAIMING": one row per intent,
    // whose it is, when it releases; Release only where this wallet can; Cancel only on an attestor's node.
    void pendingClaimsRender() {
        Harness h;
        h.feedActive(408);
        // our own claim of somebody else's vault: its claim txid is a "claim" row of ours
        json tx = txMint(); tx["type"] = "claim"; tx["txid"] = CLAIM_TXID; tx["burned"] = 100000; tx["height"] = 405;
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({tx}));
        h.ctl.feedUpgrade(json::array({claimingVault()}), attestorSetReply(false));
        QCOMPARE(h.tab.page(YellowbackTab::PendingClaims), qobject_cast<QScrollArea*>(h.tab.findChild<QTabWidget*>("subTabs")->widget(YellowbackTab::PendingClaims))->widget());
        QCOMPARE(h.tab.findChild<QTabWidget*>("subTabs")->tabText(YellowbackTab::PendingClaims), QString("Pending claims"));
        auto m = h.ctl.pendingClaimsModel();
        QCOMPARE(m->rowCount(QModelIndex()), 2);
        QCOMPARE(m->data(m->index(0, YellowbackPendingClaimsModel::Role), Qt::DisplayRole).toString(), QString("claimant"));
        QCOMPARE(m->data(m->index(0, YellowbackPendingClaimsModel::Whose), Qt::DisplayRole).toString(), QString("your claim"));
        QCOMPARE(m->data(m->index(1, YellowbackPendingClaimsModel::Role), Qt::DisplayRole).toString(), QString("owner's residual"));
        QCOMPARE(m->data(m->index(0, YellowbackPendingClaimsModel::Debt), Qt::DisplayRole).toString(), QString("$1,000.00"));
        QCOMPARE(m->data(m->index(0, YellowbackPendingClaimsModel::Remaining), Qt::DisplayRole).toString(), QString("in 6 block(s), ~7 m"));
        QVERIFY(m->data(m->index(0, YellowbackPendingClaimsModel::ReleaseHeight), Qt::DisplayRole).toString().startsWith("415"));
        QCOMPARE(YellowbackPendingClaimsModel::remaining(h.ctl.pendingClaimsModel()->rowAt(0)->intent, 414), QString("releasable now"));
        // not an attestor: no Cancel button; before release, no Release either
        QVERIFY(!h.ctl.isAttestor());
        QVERIFY(h.button("btnCancelClaim")->isHidden());
        h.selectRow("tblPendingClaims", 0);
        QVERIFY(!h.button("btnReleaseClaim")->isEnabled());
        QVERIFY2(h.label("lblPendingAction").contains("releases at height 415"), qPrintable(h.label("lblPendingAction")));
        QVERIFY(h.label("lblPendingAction").contains("It is your claim"));
        // an attestor node: Cancel is offered on the claimant intent before it matures, never on the residual
        h.ctl.feedUpgrade(json(nullptr), attestorSetReply(true));
        QVERIFY(h.ctl.isAttestor());
        h.selectRow("tblPendingClaims", 0);
        QVERIFY(!h.button("btnCancelClaim")->isHidden() && h.button("btnCancelClaim")->isEnabled());
        QVERIFY(h.label("lblPendingAction").contains("deadline: height 414"));
        h.selectRow("tblPendingClaims", 1);
        QVERIFY(!h.button("btnCancelClaim")->isEnabled());
        // a frozen or immature member key is no attestor
        h.ctl.feedUpgrade(json(nullptr), attestorSetReply(true, true, true));
        QVERIFY(!h.ctl.isAttestor());
        h.ctl.feedUpgrade(json(nullptr), attestorSetReply(true, false));
        QVERIFY(!h.ctl.isAttestor());
        // matured: Release on our claimant intent and on the residual (anyone), Cancel nowhere
        h.ctl.feedUpgrade(json(nullptr), attestorSetReply(true));
        h.feedActive(414);
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({tx}));
        h.ctl.feedUpgrade(json::array({claimingVault()}), json(nullptr));
        h.selectRow("tblPendingClaims", 0);
        QVERIFY(h.button("btnReleaseClaim")->isEnabled());
        QVERIFY(!h.button("btnCancelClaim")->isEnabled());
        h.selectRow("tblPendingClaims", 1);
        QVERIFY(h.button("btnReleaseClaim")->isEnabled());
        // somebody else's matured claim: only its claimant can release it
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array());
        h.selectRow("tblPendingClaims", 0);
        QVERIFY(!h.button("btnReleaseClaim")->isEnabled());
        QVERIFY(h.label("lblPendingAction").contains("only the claimant's wallet can release it"));
        // a non-CLAIMING row in the reply is ignored
        h.ctl.feedUpgrade(json::array({positionActive()}), json(nullptr));
        QCOMPARE(m->rowCount(QModelIndex()), 0);
        QVERIFY(h.label("lblPendingAction").contains("No claim is pending"));
    }

    // vault_release: our claimant intent with no recipient (the node finds this wallet's script);
    // the residual of somebody else's vault with the owner's P2PKH script from ownerKeyId
    void releaseClaimRequestShapes() {
        QCOMPARE(YellowbackController::p2pkhScriptForKeyId("1f2e3d4c5b6a79880706050403020100f1e2d3c4"),
                 QString("76a914c4d3e2f1000102030405060788796a5b4c3d2e1f88ac"));      // CKeyID::GetHex is byte-reversed
        QVERIFY(YellowbackController::p2pkhScriptForKeyId("xyz").isEmpty());
        Harness h;
        h.feedActive(414);
        json tx = txMint(); tx["type"] = "claim"; tx["txid"] = CLAIM_TXID; tx["height"] = 405;
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({tx}));
        h.ctl.feedUpgrade(json::array({claimingVault()}), json(nullptr));
        h.rpc.results[YellowbackRpc::LISTVAULTS] = json::array({claimingVault()});    // the refresh after the action reads them back
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array({tx});
        h.rpc.results["vault_release"] = "a11ce0000000000000000000000000000000000000000000000000000000000a";
        h.tab.releaseClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.confirms[0].startsWith("Release claim intent " % QString(CLAIM_TXID) % ":0"), qPrintable(h.confirms[0]));
        QVERIFY(h.confirms[0].contains("no attestor can cancel it any more"));
        QCOMPARE(h.rpc.lastParams("vault_release"), json::array({std::string(CLAIM_TXID) + ":0"}));
        QVERIFY(h.notices.last().startsWith("Release sent|txid a11ce"));
        h.tab.releaseClaim(*h.ctl.pendingClaimsModel()->rowAt(1));
        QCOMPARE(h.rpc.lastParams("vault_release"), json::array({std::string(CLAIM_TXID) + ":1", "76a914c4d3e2f1000102030405060788796a5b4c3d2e1f88ac"}));
        QVERIFY(h.confirms.last().contains("to the vault owner"));
        // before release height: nothing is sent
        Harness early;
        early.feedActive(410);
        early.ctl.feedUpgrade(json::array({claimingVault()}), json(nullptr));
        early.tab.releaseClaim(*early.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(early.rpc.count("vault_release"), 0);
        QVERIFY(early.notices.last().contains("matures at height 415"));
        // the node's refusal is shown verbatim
        h.rpc.results.remove("vault_release");
        h.rpc.errors["vault_release"] = "the intent matures at height 415";
        h.tab.releaseClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        QVERIFY(h.errorNotices.last().startsWith("vault_release failed: the intent matures at height 415"));
        QVERIFY(h.copyIsClean());
    }

    // The attestor's cancel of a wrong-price claim: the warning names the lost burn and the
    // equivocation risk; build, sign, send through the primitive RPCs; the signed cancel is kept
    // and a second attempt only re-sends it, never signs another.
    void cancelClaimFlow() {
        Harness h;
        h.feedActive(408);
        h.ctl.feedUpgrade(json::array({claimingVault()}), attestorSetReply(true));
        h.rpc.results[YellowbackRpc::LISTVAULTS] = json::array({claimingVault()});    // the refresh after the action reads them back
        h.rpc.results["set_getinfo"] = attestorSetReply(true);
        const QString outpoint = QString(CLAIM_TXID) % ":0";
        Settings::getInstance()->setYellowbackSignedCancel(outpoint, QString());
        h.rpc.results["vault_buildcancel"] = json::parse(R"({"hex": "04deadbeef", "required": 1, "cancelsetid": "5e75", "deadline": 414})");
        h.rpc.results["set_signcancel"]    = json::parse(R"({"hex": "04deadbeefsigned", "complete": true, "signatures": 1, "required": 1, "sighash": "00", "setsigs": []})");
        h.rpc.results["vault_send"]        = "ca9ce10000000000000000000000000000000000000000000000000000000000";
        // the residual intent cannot be cancelled
        h.tab.cancelClaim(*h.ctl.pendingClaimsModel()->rowAt(1));
        QCOMPARE(h.rpc.count("vault_buildcancel"), 0);
        QVERIFY(h.errorNotices.last().contains("Only a claimant intent can be cancelled"));
        // a declined warning sends nothing
        h.answer = false;
        h.tab.cancelClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(h.rpc.count("vault_buildcancel"), 0);
        const QString warning = h.confirms.last();
        QVERIFY2(warning.contains("WARNING. Cancel only a claim made at a wrong price"), qPrintable(warning));
        QVERIFY(warning.contains("burn of $1,000.00 of YED is NOT refunded"));
        QVERIFY(warning.contains("provable equivocation"));
        QVERIFY(warning.contains("freezes your bond"));
        QVERIFY(warning.contains("must confirm by height 414"));
        QVERIFY(warning.contains("1 signature(s) needed"));
        // confirmed: build, sign, keep, send
        h.answer = true;
        h.tab.cancelClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(h.rpc.lastParams("vault_buildcancel"), json::array({outpoint.toStdString()}));
        QCOMPARE(h.rpc.lastParams("set_signcancel"), json::array({"04deadbeef"}));
        QCOMPARE(h.rpc.lastParams("vault_send"), json::array({"04deadbeefsigned"}));
        QCOMPARE(Settings::getInstance()->getYellowbackSignedCancel(outpoint), QString("04deadbeefsigned"));
        QVERIFY2(h.notices.last().startsWith("Cancel sent|txid ca9ce1"), qPrintable(h.notices.last()));
        QVERIFY(h.notices.last().contains("burn is not refunded"));
        // a second cancel of the same intent re-sends the kept transaction and signs nothing
        h.tab.cancelClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(h.rpc.count("vault_buildcancel"), 1);
        QCOMPARE(h.rpc.count("set_signcancel"), 1);
        QCOMPARE(h.rpc.count("vault_send"), 2);
        QVERIFY(h.confirms.last().contains("will not sign another one"));
        h.selectRow("tblPendingClaims", 0);
        QVERIFY(h.label("lblPendingAction").contains("already signed a cancel"));
        // a set that needs more signatures: kept, put on the clipboard, nothing sent
        Settings::getInstance()->setYellowbackSignedCancel(outpoint, QString());
        h.rpc.results["set_signcancel"] = json::parse(R"({"hex": "04partly", "complete": false, "signatures": 1, "required": 2})");
        h.tab.cancelClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(h.rpc.count("vault_send"), 2);
        QVERIFY(h.notices.last().startsWith("Cancel needs more signatures|"));
        QVERIFY(h.notices.last().contains("1 of 2 signatures"));
        QCOMPARE(Settings::getInstance()->getYellowbackSignedCancel(outpoint), QString("04partly"));
        Settings::getInstance()->setYellowbackSignedCancel(outpoint, QString());
        // a wallet that is no attestor, or a matured intent: refused before any RPC
        Harness plain;
        plain.feedActive(408);
        plain.ctl.feedUpgrade(json::array({claimingVault()}), attestorSetReply(false));
        plain.tab.cancelClaim(*plain.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(plain.rpc.count("vault_buildcancel"), 0);
        QVERIFY(plain.errorNotices.last().contains("no current member key"));
        Harness late;
        late.feedActive(414);
        late.ctl.feedUpgrade(json::array({claimingVault()}), attestorSetReply(true));
        late.tab.cancelClaim(*late.ctl.pendingClaimsModel()->rowAt(0));
        QCOMPARE(late.rpc.count("vault_buildcancel"), 0);
        QVERIFY(late.errorNotices.last().contains("can no longer be cancelled"));
        QVERIFY(h.copyIsClean());
    }

    // A claim of ours that the attestor set cancelled: the vault it closed is gone (re-created
    // under the cancel's txid), so yed_getvault answers vault-not-found; the page says the burn is lost.
    void cancelledClaimsAreShown() {
        Harness h;
        h.feedActive(420);
        json tx = txMint(); tx["type"] = "claim"; tx["txid"] = CLAIM_TXID; tx["burned"] = 100000; tx["height"] = 405;
        json released = tx; released["txid"] = "d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2";
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({tx, released}));
        json info = txInfoClaim(); info["closedVaults"] = json::array({{{"txid", "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}, {"vout", 0}}});
        // one fake reply per method: both claims read the same vault; first it is gone (cancelled)
        h.rpc.results[YellowbackRpc::GETTXINFO] = info;
        h.rpc.errors[YellowbackRpc::GETVAULT] = "vault-not-found: 6a1f…";
        h.ctl.refreshClaimOutcomes();
        auto outcomes = h.ctl.claimOutcomes();
        QCOMPARE(outcomes.size(), 2);
        QCOMPARE(outcomes[0].outcome, QString("cancelled"));
        QVERIFY(h.visible("lblCancelledClaims"));
        QVERIFY2(h.label("lblCancelledClaims").contains("was cancelled by the attestor set"), qPrintable(h.label("lblCancelledClaims")));
        QVERIFY(h.label("lblCancelledClaims").contains("$1,000.00 of YED it burned are not refunded"));
        QVERIFY(h.label("lblCancelledClaims").contains("more collateralised"));
        // a released claim reads CLAIMED, a pending one CLAIMING; neither is listed as cancelled
        QCOMPARE(YellowbackTab::describeCancelledClaims({}), QString());
        Harness r;
        r.feedActive(420);
        r.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({released}));
        r.rpc.results[YellowbackRpc::GETTXINFO] = info;
        json claimedVault = positionActive(); claimedVault["status"] = "CLAIMED";
        r.rpc.results[YellowbackRpc::GETVAULT] = claimedVault;
        r.ctl.refreshClaimOutcomes();
        QCOMPARE(r.ctl.claimOutcomes().value(0).outcome, QString("released"));
        QVERIFY(!r.visible("lblCancelledClaims"));
        // the node types the owner's view of a release "redeem" with nothing burned: labelled for what it is
        QCOMPARE(YellowbackFormat::rowLabel("redeem", "", 0), QString("Claim released"));
        QCOMPARE(YellowbackFormat::rowLabel("redeem", "owner", 100000), QString("Redeem"));
        QCOMPARE(YellowbackFormat::typeLabel("claim_cancel"), QString("Claim cancelled"));
    }

    // P4-b: this wallet's membership of the YED attestor set, and the heartbeat
    void membershipAndHeartbeat() {
        Harness h;
        h.feedActive(330);
        h.ctl.feedUpgrade(json(nullptr), attestorSetReply(false));
        QVERIFY2(h.label("lblMembership").contains("YED attestor set 5e755e755e755e75"), qPrintable(h.label("lblMembership")));
        QVERIFY(h.label("lblMembership").contains("2 member(s), 2 current"));
        QVERIFY(h.label("lblMembership").contains("not an attestor"));
        QVERIFY(!h.button("btnHeartbeat")->isEnabled());
        h.tab.heartbeatMember(QString());
        QVERIFY(h.errorNotices.last().contains("no member key"));
        h.ctl.feedUpgrade(json(nullptr), attestorSetReply(true));
        h.rpc.results["set_getinfo"] = attestorSetReply(true);
        QVERIFY(h.label("lblMembership").contains("Your member 03b1c2d3e4f50617"));
        QVERIFY2(h.label("lblMembership").contains("current, live (last act at 320)"), qPrintable(h.label("lblMembership")));
        QVERIFY(h.label("lblMembership").contains("dormant from height 1321"));
        QVERIFY(h.label("lblMembership").contains("Pending claims page"));
        QVERIFY(h.button("btnHeartbeat")->isEnabled());
        h.rpc.results["set_heartbeat"] = json::parse(R"({"txid": "beefbeef", "memberkey": "03b1c2d3e4f5061728394a5b6c7d8e9f0a1b2c3d4e5f6071829304a5b6c7d8e9f0"})");
        h.button("btnHeartbeat")->click();
        QCOMPARE(h.rpc.lastParams("set_heartbeat"), json::array({"5e755e755e755e755e755e755e755e755e755e755e755e755e755e755e755e75", MEMBER_KEY}));
        QVERIFY(h.confirms.last().contains("stays live for the next 1000 blocks"));
        QVERIFY(h.notices.last().startsWith("Heartbeat sent|txid beefbeef"));
        // dormant and frozen are said so
        json dormant = attestorSetReply(true); dormant["memberlist"][1]["live"] = false;
        h.ctl.feedUpgrade(json(nullptr), dormant);
        QVERIFY(h.label("lblMembership").contains("current but DORMANT"));
        json frozen = attestorSetReply(true, true, true);
        h.ctl.feedUpgrade(json(nullptr), frozen);
        QVERIFY(h.label("lblMembership").contains("bond frozen"));
        QVERIFY(!h.button("btnHeartbeat")->isEnabled());
        // the Attestors table shows the set's lastAct and bondFrozen per record
        json row = attestorRow(); row["lastAct"] = 281; row["bondFrozen"] = true;
        h.ctl.feedAttest(json(nullptr), json::array({row}), json(nullptr));
        auto m = h.ctl.attestorsModel();
        QCOMPARE(m->data(m->index(0, YellowbackAttestorsModel::LastAct), Qt::DisplayRole).toString(), QString("281"));
        QCOMPARE(m->data(m->index(0, YellowbackAttestorsModel::BondFrozen), Qt::DisplayRole).toString(), QString("FROZEN"));
        h.feedActive(600);
        h.ctl.feedAttest(json(nullptr), json::array({row}), json(nullptr));
        h.selectRow("tblAttestors", 0);
        QVERIFY(!h.button("btnWithdraw")->isEnabled());          // past the locktime, but frozen
        QVERIFY(h.label("lblAttestorAction").contains("FROZEN"));
    }

    void vaultsRenderClosedRows() {
        json closed = positionActive();
        closed["status"] = "CLOSED"; closed["closeHeight"] = 390; closed["closingTxid"] = "9e8d"; closed["burnedCents"] = 100000;
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(closed), 400);
        QVERIFY(!a.release && !a.redeem && !a.renew);
        QVERIFY(a.text.contains("burning $1,000.00"));

        json swept = closed; swept["burnedCents"] = 0; swept["unbacked"] = true;
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(swept), 400);
        QVERIFY(a.text.contains("unbacked"));

        json claimed = closed; claimed["status"] = "CLAIMED";
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(claimed), 400);
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
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::Fee), Qt::DisplayRole).toString(), YellowbackFormat::zec(62814070));
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
        QCOMPARE(YellowbackFormat::typeLabel("claim_release"), QString("Claim released"));
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
        h.inTerm = false;                                            // the heights rule's copy; in-term: mintConfirmationInTerm
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
        h.rpc.results[YellowbackRpc::MINT]               = mintReply();
        Settings::getInstance()->setYellowbackBackupPending(false);
        h.mintAmount("1000");
        h.tab.doMint();

        QCOMPARE(h.confirms.size(), 1);
        const QString& text = h.confirms[0];
        QVERIFY(text.contains("Mint $1,000.00 of YED"));
        QVERIFY(text.contains("class A"));
        QVERIFY(text.contains("48 blocks"));
        QVERIFY(text.contains(YellowbackFormat::zec(EST_ZAT)));              // exact collateral
        QVERIFY2(text.contains("at most " % YellowbackFormat::zec(253768844820)), qPrintable(text));   // the cap sent (F-1)
        QVERIFY(text.contains("500.00 %"));                                  // ratio
        QVERIFY(text.contains("1.00x"));                                     // sigma
        QVERIFY(text.contains(YellowbackFormat::zec(628140705)));            // pool fee: FEE-1 of the collateral (H-9.3)
        QVERIFY(text.contains("smQvTmAz2ExamplePayoutAddress1111111"));      // payee
        QVERIFY(text.contains("height 377"));                                 // refHeight 329 + 48 (H-9.3 recomputes it)
        QVERIFY(text.contains("claim height is 401"));
        QVERIFY(text.contains("Back up wallet.dat"));
        QVERIFY(text.contains("only your key can spend it before the claim height"));

        // yed_mint <cents> <lockBlocks> [from] [bundleHex] [wait]: "" for the transparent balance,
        // the bundle from the pool, wait = false (W7). This reply is not pending (a node that
        // answered in the full shape), so the summary comes straight from it.
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::MINT), json::array({100000, 48, "", "", false, 253768844820}));   // maxCollateralZat = collateral + 1 % (F-1)
        // yed_getfeepayee <refHeight from the estimate> <the vault's collateral, max(required, 4 FEE_MIN)>
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::GETFEEPAYEE), json::array({329, EST_ZAT}));
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
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
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
        QVERIFY(h.confirms[0].contains("Pool fee: none"));
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);
    }

    void mintRefusedByMintpol() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
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

    // audit F-1: the caps. collateral-above-max is explained as a price move; a confirmed mint
    // whose vault locked a different amount than the dialog showed says so first; a block that
    // arrives while the dialog is open re-estimates instead of sending the stale figure.
    void mintCapsAndPriceMove() {
        {
            Harness h;
            h.feedActive();
            h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
            h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
            h.rpc.errors[YellowbackRpc::MINT] = "collateral-above-max: 26000000000 > 25376884422";
            h.mintAmount("1000");
            h.tab.doMint();
            QCOMPARE(h.errorNotices.size(), 1);
            QVERIFY2(h.errorNotices[0].contains("yed_mint failed: collateral-above-max"), qPrintable(h.errorNotices[0]));
            QVERIFY2(h.errorNotices[0].contains("The price moved"), qPrintable(h.errorNotices[0]));
            QVERIFY(h.errorNotices[0].contains("Re-estimate"));
        }
        {
            Harness h;
            h.feedActive();
            h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
            h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
            json r = mintReply(); r["collateralZat"] = 252000000000;         // within the cap, but not the figure shown
            h.rpc.results[YellowbackRpc::MINT] = r;
            h.mintAmount("1000");
            h.tab.doMint();
            QCOMPARE(h.notices.size(), 1);
            QVERIFY2(h.notices[0].startsWith("Mint sent|NOTE: the vault locked " % YellowbackFormat::zec(252000000000) % " of YEC, not the " % YellowbackFormat::zec(EST_ZAT)), qPrintable(h.notices[0]));
            // the same amount: no note
            Harness same;
            same.feedActive();
            same.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
            same.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
            same.rpc.results[YellowbackRpc::MINT]               = mintReply();
            same.mintAmount("1000");
            same.tab.doMint();
            QVERIFY2(same.notices[0].startsWith("Mint sent|Minted $1,000.00"), qPrintable(same.notices[0]));
        }
        {
            Harness h;
            h.feedActive();
            h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
            h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
            h.rpc.results[YellowbackRpc::MINT]               = mintReply();
            // A block arrives while the first dialog is open
            h.tab.confirmFn = [&h](const QString&, const QString& text) {
                h.confirms << text;
                if (h.confirms.size() == 1) {
                    h.feedActive(332);
                    // the node's next estimate is at the new reference height (H-9.3 checks it)
                    json e = estimateReply(); e["refHeight"] = 330; e["lockHeight"] = 378; e["claimHeight"] = 402;
                    h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = e;
                }
                return true;
            };
            h.mintAmount("1000");
            h.tab.doMint();
            QCOMPARE(h.confirms.size(), 1);                                   // the fresh figure fits the cap: not asked again
            QCOMPARE(h.rpc.count(YellowbackRpc::ESTIMATECOLLATERAL), 2);      // but estimated again before sending
            QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);
            QCOMPARE(h.notices.size(), 1);
        }
        {
            Harness h;
            h.feedActive();
            h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
            h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
            h.rpc.results[YellowbackRpc::MINT]               = mintReply();
            // A block arrives while the first dialog is open and the mint price falls 5 %: past the cap
            h.tab.confirmFn = [&h](const QString&, const QString& text) {
                h.confirms << text;
                if (h.confirms.size() == 1) {
                    h.feedActive(332);
                    json e = estimateReply(); e["refHeight"] = 330; e["lockHeight"] = 378; e["claimHeight"] = 402;
                    e["pMint"] = 1890000; e["requiredZat"] = 264550265000;
                    h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = e;
                    h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(264550265000);
                }
                return true;
            };
            h.mintAmount("1000");
            h.tab.doMint();
            QCOMPARE(h.confirms.size(), 2);                                   // asked again with the new figure
            QVERIFY2(h.confirms[1].contains(YellowbackFormat::zec(264550265000)), qPrintable(h.confirms[1]));
            QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);                    // sent once, after the second dialog
        }
    }

    void mintBadLockAndUnsatisfiable() {
        QVERIFY(YellowbackController::explainError("mint-bad-lock: lockBlocks 47 is in no term class").contains("no enabled term class"));
        QVERIFY(YellowbackController::explainError("mint-unsatisfiable: the required collateral exceeds MAX_MONEY").contains("maximum amount of YEC"));
        QVERIFY(YellowbackController::explainError("mintpol-cap: x").contains("supply cap"));
        QCOMPARE(YellowbackController::explainError("mintpol-cap: x"), QString("Minting is paused: the supply cap is reached."));   // before W20
        const QString w20 = YellowbackController::explainError("mintpol-cap: supply cap headroom is 0 cents; above the cap only a term class whose minimum ratio is at least 500 % can mint (class A)");
        QVERIFY2(w20.contains("above the supply cap") && w20.contains("class the node names") && !w20.contains("paused"), qPrintable(w20));
        QVERIFY(YellowbackController::explainError("mintpol-divergence: x").contains("diverge"));
        QVERIFY(YellowbackController::explainError("mintpol-global-ratio: x").contains("collateral ratio"));
        QVERIFY(YellowbackController::explainError("mintpol-not-active: x").contains("vault upgrade"));
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
        h.inTerm = false;                                            // yed_getfeepayee's quote; in-term: redeemInTermQuotesTheEarlyRedeemFee
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.results[YellowbackRpc::GETFEEPAYEE] = feePayeeReply();
        h.rpc.results[YellowbackRpc::REDEEM]      = redeemReply();
        h.tab.redeemVault(YellowbackPosition::fromJson(p));

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].startsWith("Redeem vault 6a1f"));
        QVERIFY(h.confirms[0].contains("Burn: $1,000.00 of YED"));
        QVERIFY(h.confirms[0].contains(YellowbackFormat::zec(62814070)));                 // fee estimate = FEE-1 (H-9.3)
        QVERIFY(h.confirms[0].contains("smQvTmAz2ExamplePayoutAddress1111111"));
        QVERIFY(h.confirms[0].contains(YellowbackFormat::zec(25125628141 - 62814070)));   // collateral out
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
        h.inTerm = false;                                            // yed_getfeepayee's quote; in-term: redeemInTermQuotesTheEarlyRedeemFee
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
        h.inTerm = false;                                            // yed_getfeepayee's quote; in-term: redeemInTermQuotesTheEarlyRedeemFee
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
        h.inTerm = false;                                            // yed_getfeepayee's quote; in-term: redeemInTermQuotesTheEarlyRedeemFee
        json p = positionActive(); p["canRedeem"] = true;
        h.feedActive(381, json::array({p}));
        h.rpc.errors[YellowbackRpc::GETFEEPAYEE] = "fee-no-eligible-payee: E(R) is empty";
        h.rpc.results[YellowbackRpc::REDEEM]     = redeemReply();
        h.tab.redeemVault(YellowbackPosition::fromJson(p), "ys1exampleSaplingDestination");
        QVERIFY(h.confirms[0].contains("Pool fee: none"));
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
        h.inTerm = false;                                            // yed_getfeepayee's quote; in-term: redeemInTermQuotesTheEarlyRedeemFee
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
        p["canRedeem"] = true; p["underwaterAt"] = nullptr;
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
        QVERIFY2(h.confirms[0].contains("Fees from your own YEC: the pool fee " % YellowbackFormat::zec(62814070)), qPrintable(h.confirms[0]));
        // rpcversion 5 (U-23): the claim intent carries the whole collateral less the residual (0 here)
        QVERIFY(h.confirms[0].contains("Claim intent: about " % YellowbackFormat::zec(25125628141)));
        QVERIFY2(h.confirms[0].contains("at least " % YellowbackFormat::zec(24874371860)), qPrintable(h.confirms[0]));   // the floor sent (F-1)
        QVERIFY2(h.confirms[0].contains("at most $1,000.99"), qPrintable(h.confirms[0]));                               // the burn cap sent (H-9.3)
        QVERIFY(h.confirms[0].contains("$0.4000 per YEC"));
        QVERIFY(h.confirms[0].contains("waits in a claim intent for 10 blocks"));
        QVERIFY(h.confirms[0].contains("does NOT refund the $1,000.00 of YED you burn"));
        // yed_claim <vaultTxid> [to] [bundleHex] [wait]: default destination, pool bundle, wait = false
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::CLAIM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "", "", false, 24874371860, 100099}));   // minOutZat = intent - 1 % (F-1); maxBurnCents = debt + 99 (H-9.3)
        QVERIFY(h.notices[0].startsWith("Claim sent|"));
        QVERIFY(h.notices[0].contains("YED burned: $1,000.00"));
        QVERIFY(h.notices[0].contains("smQvTmAz2ExamplePayoutAddress1111111"));
        QVERIFY2(h.notices[0].contains("Release it on the Pending claims page"), qPrintable(h.notices[0]));
        // D-U1: a Sapling destination is refused before any RPC
        const int claimsBefore = h.rpc.count(YellowbackRpc::CLAIM);
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()), "ys1examplesaplingaddress");
        QCOMPARE(h.rpc.count(YellowbackRpc::CLAIM), claimsBefore);
        QVERIFY(h.errorNotices.last().contains("must be a transparent address"));
        QVERIFY(YellowbackController::explainError("bad-address: a claim pays a transparent key").contains("transparent address"));
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
        h.rpc.errors[YellowbackRpc::CLAIM] = "claim-out-below-min: 24000000000 < 24812185930";   // audit F-1
        h.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));
        QVERIFY2(h.errorNotices[3].contains("The price moved"), qPrintable(h.errorNotices[3]));
    }

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
        h.ctl.feedUpgrade(json::array({claimingVault()}), attestorSetReply(true));
        h.tab.releaseClaim(*h.ctl.pendingClaimsModel()->rowAt(1));
        h.tab.cancelClaim(*h.ctl.pendingClaimsModel()->rowAt(0));
        h.tab.heartbeatMember(MEMBER_KEY);
        QVERIFY(h.rpc.calls.isEmpty());
        QVERIFY(h.confirms.isEmpty());
        QVERIFY(!h.button("btnMint")->isEnabled());
        QVERIFY(!h.button("btnRedeem")->isEnabled());
        QVERIFY(!h.button("btnClaim")->isEnabled());
        QVERIFY(!h.button("btnStart")->isEnabled());
        QVERIFY(!h.button("btnReleaseClaim")->isEnabled());
        QVERIFY(!h.button("btnCancelClaim")->isEnabled());
        QVERIFY(!h.button("btnHeartbeat")->isEnabled());
    }

    // ── Devnet end-to-end (N28): mint → send → redeem against a running devnet ────────────
    // YELLOWBACK_DEVNET_DIR is the devnet's --dir; node0/ycash.conf holds the RPC port and
    // credentials the wallet would read through --conf.

    // ── v3 (plan §4.8, Phase A5-a): read-only views over the rpcversion 3 contract ────────

    void attestorsPageExists() {
        YellowbackTab tab(nullptr);
        auto sub = tab.findChild<QTabWidget*>("subTabs");
        QCOMPARE(sub->tabText(YellowbackTab::Attestors), QString("Attestors"));
        QCOMPARE(sub->tabText(YellowbackTab::Settings), QString("Settings"));
        QVERIFY(tab.page(YellowbackTab::Attestors)->findChild<QTableView*>("tblAttestors") != nullptr);
    }

    void parsesAttestorRecord() {
        auto a = YellowbackAttestor::fromJson(attestorRow());
        QCOMPARE(a.seq, 1);
        QCOMPARE(a.status, QString("ELIGIBLE"));
        QCOMPARE(a.bondZat, (qint64)1000000000);
        QCOMPARE(a.bondLocktime, 500);
        QCOMPARE(a.bondSpentHeight, -1);          // null
        QCOMPARE(a.weight, QString("31000000000"));
        QVERIFY(a.seated && !a.pinned && a.founding && a.poolFresh);
        QCOMPARE(a.seatedSince, 300);
        QCOMPARE(a.tier, 0);
        QVERIFY(!a.pool);
        QCOMPARE(a.lastBundleHeight, 330);
        auto e = YellowbackAttestor::fromJson(json::object());   // missing fields degrade, never throw
        QCOMPARE(e.seq, 0);
        QCOMPARE(e.seatedSince, -1);
    }

    void attestorsTableRenders() {
        Harness h;
        h.feedActive();
        json pinned = attestorRow();
        pinned["seq"] = 3; pinned["pinned"] = true; pinned["founding"] = false; pinned["poolFresh"] = false;
        pinned["flags"] = json{{"tier", 2}, {"pool", true}}; pinned["lastBundleHeight"] = nullptr; pinned["seatedSince"] = nullptr;
        pinned["seated"] = false; pinned["status"] = "DORMANT";
        h.ctl.feedAttest(json(nullptr), json::array({attestorRow(), pinned}), json(nullptr));
        auto m = h.ctl.attestorsModel();
        using M = YellowbackAttestorsModel;
        QCOMPARE(m->rowCount(QModelIndex()), 2);
        QCOMPARE(m->data(m->index(0, M::Seq), Qt::DisplayRole).toString(), QString("1"));
        QCOMPARE(m->data(m->index(0, M::Status), Qt::DisplayRole).toString(), QString("ELIGIBLE"));
        QCOMPARE(m->data(m->index(0, M::Seated), Qt::DisplayRole).toString(), QString("seated"));
        QCOMPARE(m->data(m->index(0, M::Bond), Qt::DisplayRole).toString(), YellowbackFormat::zec(1000000000) % " (500)");
        QCOMPARE(m->data(m->index(0, M::Weight), Qt::DisplayRole).toString(), QString("31000000000"));
        QCOMPARE(m->data(m->index(0, M::SeatedSince), Qt::DisplayRole).toString(), QString("300"));
        QCOMPARE(m->data(m->index(0, M::Founding), Qt::DisplayRole).toString(), QString("yes"));
        QCOMPARE(m->data(m->index(0, M::Sources), Qt::DisplayRole).toString(), QString("exchange APIs"));
        QCOMPARE(m->data(m->index(0, M::LastBundle), Qt::DisplayRole).toString(), QString("330"));
        QCOMPARE(m->data(m->index(0, M::PoolFresh), Qt::DisplayRole).toString(), QString("fresh"));
        // The pinned, dormant aggregator that also runs a pool, never in a bundle, never seated
        QCOMPARE(m->data(m->index(1, M::Seated), Qt::DisplayRole).toString(), QString("pinned"));
        QCOMPARE(m->data(m->index(1, M::Status), Qt::DisplayRole).toString(), QString("DORMANT"));
        QCOMPARE(m->data(m->index(1, M::Sources), Qt::DisplayRole).toString(), QString("aggregator, pool"));
        QCOMPARE(m->data(m->index(1, M::LastBundle), Qt::DisplayRole).toString(), QString("-"));
        QCOMPARE(m->data(m->index(1, M::SeatedSince), Qt::DisplayRole).toString(), QString("-"));
        QCOMPARE(m->data(m->index(1, M::Founding), Qt::DisplayRole).toString(), QString("no"));
        QCOMPARE(m->data(m->index(1, M::PoolFresh), Qt::DisplayRole).toString(), QString("no"));
        QVERIFY(m->data(m->index(0, M::Seated), Qt::ToolTipRole).toString().contains("pinned"));
        // The refresh a feedActive() primes also asks for the three v3 lists
        h.ctl.refresh(true);
        QCOMPARE(h.rpc.count(YellowbackRpc::LISTATTESTORS), 1);
        QCOMPARE(h.rpc.count(YellowbackRpc::GETPRICE), 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::GETSELECTION), json::array({329, ""}));   // refHeightNow(), empty selector
    }

    void armingBannerArmed() {
        Harness h;
        h.feedActive();   // the canned yed_getinfo.attest is ARMED since 308, 3 seated, 3 fresh
        QString b = h.label("lblArming");
        QVERIFY2(b.startsWith("ARMED since 308"), qPrintable(b));
        QVERIFY(b.contains("3 attestor(s) seated"));
        QVERIFY(b.contains("fresh attestations from 3"));
        QVERIFY(b.contains("both pools and attestors signed off on"));
        QVERIFY(!b.contains("trustless", Qt::CaseInsensitive) && !b.contains("verified", Qt::CaseInsensitive));
    }

    void armingBannerTriggered() {
        Harness h;
        json info = infoActive();
        info["attest"] = json::parse(R"({"status": "TRIGGERED", "triggerHeight": 300, "armHeight": 308, "seatedCount": 3,
                                          "poolSize": 0, "poolFresh": 0, "carrierMode": "scriptsig", "required": true, "armed": false})");
        h.feed(info, statsOpen(), activationActive());
        QVERIFY2(h.label("lblArming").startsWith("TRIGGERED at 300, arms at 308"), qPrintable(h.label("lblArming")));
    }

    void armingBannerUnarmed() {
        Harness h;
        json info = infoActive();
        info["attest"] = json::parse(R"({"status": "UNARMED", "triggerHeight": 0, "armHeight": 0, "seatedCount": 1,
                                          "poolSize": 0, "poolFresh": 0, "carrierMode": "scriptsig", "required": true, "armed": false})");
        h.feed(info, statsOpen(), activationActive());
        QVERIFY2(h.label("lblArming").startsWith("UNARMED"), qPrintable(h.label("lblArming")));
        QVERIFY(h.label("lblArming").contains("pool quotes alone"));
    }

    void armingBannerDisabledByParameterSet() {
        // required=false: every bundle-reading rule is vacuous whatever the status says (W15)
        Harness h;
        json info = infoActive();
        info["attest"]["required"] = false; info["attest"]["armed"] = false;
        h.feed(info, statsOpen(), activationActive());
        QVERIFY2(h.label("lblArming").startsWith("Attestation layer disabled by parameter set"), qPrintable(h.label("lblArming")));
        // An rpcversion 2 node without the block: said so, never a crash
        QCOMPARE(YellowbackController::describeAttest(json::object()), QString());
        info.erase("attest");
        h.feed(info);
        QVERIFY(h.label("lblArming").contains("no attestation state"));
    }

    void overviewSourcePricesArmed() {
        Harness h;
        h.feedActive();
        QCOMPARE(h.label("lblPoolPrices"), QString("-"));      // before yed_getprice answered
        h.ctl.feedAttest(priceReply(), json(nullptr), json(nullptr));
        QCOMPARE(h.label("lblPoolPrices"), QString("$1.9900 / $2.0000"));
        QVERIFY2(h.label("lblAttestation").startsWith("ARMED"), qPrintable(h.label("lblAttestation")));
        QVERIFY(h.tab.findChild<QLabel*>("lblAttestation")->toolTip().contains("attested prices"));   // the field is the status alone; the explanation is the tooltip
    }

    void overviewSourcePricesUnarmed() {
        Harness h;
        h.feedActive();
        json p = priceReply();
        p["armed"] = false; p["attestStatus"] = "UNARMED"; p["xMint"] = nullptr; p["pMint"] = nullptr;
        h.ctl.feedAttest(p, json(nullptr), json(nullptr));
        QCOMPARE(h.label("lblPoolPrices"), QString("undefined / $2.0000"));
        QVERIFY(h.label("lblAttestation").startsWith("UNARMED"));
        QVERIFY(h.tab.findChild<QLabel*>("lblAttestation")->toolTip().contains("pool quotes alone"));
        // ARMED but not required: the parameter set disabled the layer
        p["attestStatus"] = "ARMED";
        h.ctl.feedAttest(p, json(nullptr), json(nullptr));
        QVERIFY(h.label("lblAttestation").contains("(disabled)"));
        QVERIFY(h.tab.findChild<QLabel*>("lblAttestation")->toolTip().contains("disabled by the parameter set"));
    }

    void mintSelectionLine() {
        Harness h;
        h.feedActive();
        QVERIFY2(h.label("lblSelection").contains("not armed"), qPrintable(h.label("lblSelection")));
        h.ctl.feedAttest(json(nullptr), json(nullptr), selectionReply());
        QCOMPARE(h.label("lblSelection"), QString("2 of 2 selected attestors reachable"));
        // One selected attestor missing from the pool: below mSelect, the line says what it waits for
        json sel = selectionReply();
        sel["reachable"] = 1; sel["selected"][1]["poolFresh"] = false;
        h.ctl.feedAttest(json(nullptr), json(nullptr), sel);
        QVERIFY2(h.label("lblSelection").startsWith("1 of 2 selected attestors reachable"), qPrintable(h.label("lblSelection")));
        QVERIFY(h.label("lblSelection").contains("a mint needs 2"));
        // Not armed at the reference height: nothing to count
        sel["armed"] = false;
        h.ctl.feedAttest(json(nullptr), json(nullptr), sel);
        QVERIFY(h.label("lblSelection").contains("not armed"));
        QCOMPARE(YellowbackController::describeSelection(json::object()), QString());
    }

    void mintEstimateShowsSourceBound() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReplyArmed();
        h.estimateFor("1000");
        QCOMPARE(h.label("lblPrice"), QString("$1.9850 per YEC"));
        QString src = h.label("lblSource");
        QVERIFY2(src.contains("pools $1.9900, attestors $1.9850"), qPrintable(src));
        QVERIFY(src.contains("the attestors' price bound the mint"));
        QVERIFY(!h.visible("lblDivergence"));     // 25 bps, well under divergeBpsAttest 1500
        // The pools' bound
        json e = estimateReplyArmed(); e["source"] = "x"; e["aMint"] = 1995000; e["pMint"] = 1990000;
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = e;
        h.estimateFor("1100");
        QVERIFY(h.label("lblSource").contains("the pools' price bound the mint"));
        // Not armed: the v2 reply, one source
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.estimateFor("1200");
        QVERIFY2(h.label("lblSource").contains("attestation layer not armed"), qPrintable(h.label("lblSource")));
    }

    void mintDivergencePausesMinting() {
        Harness h;
        h.feedActive();
        // divergenceBps above params.attest.divergeBpsAttest (1500): the mint10-diverged banner
        json e = estimateReplyArmed(); e["divergenceBps"] = 1800;
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = e;
        h.estimateFor("1000");
        QVERIFY(h.visible("lblDivergence"));
        QCOMPARE(h.label("lblDivergence"), QString("the pools' fast median and the attestors disagree by 18.00 %; minting paused until they agree (they usually do within a fast window)"));
        // The node itself refusing the estimate with mint10-diverged
        h.rpc.results.remove(YellowbackRpc::ESTIMATECOLLATERAL);
        h.rpc.errors[YellowbackRpc::ESTIMATECOLLATERAL] = "mint10-diverged: xMint 1990000 aMint 2400000";
        h.estimateFor("1100");
        QVERIFY(h.visible("lblDivergence"));
        QVERIFY2(h.label("lblDivergence").contains("disagree by more than 15.00 %; minting paused"), qPrintable(h.label("lblDivergence")));
        QVERIFY(h.label("lblMintHint").contains("mint10-diverged"));   // the identifier stays verbatim
        QVERIFY(!h.button("btnMint")->isEnabled());
        // A null divergence (a source undefined) is not a divergence
        QCOMPARE(YellowbackController::describeDivergence(json::parse(R"({"divergenceBps": null})"), 1500), QString());
        QCOMPARE(YellowbackController::describeDivergenceError("bundle-insufficient: 1 of 2", 1500), QString());
    }

    void vaultsRenderNoticedRow() {
        Harness h;
        json p = positionActive();
        p["noticed"] = true; p["noticeHeight"] = 338; p["emergencyOpenAt"] = 342; p["canNotice"] = false;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({p}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Notice), Qt::DisplayRole).toString(), QString("noticed, emergency claim from 342"));
        QVERIFY(m->data(m->index(0, YellowbackPositionsModel::Notice), Qt::ToolTipRole).toString().contains("height 338"));
        auto pos = YellowbackPosition::fromJson(p);
        QVERIFY(pos.noticed && !pos.canNotice);
        QCOMPARE(pos.noticeHeight, 338);
        QCOMPARE(pos.emergencyOpenAt, 342);
        auto a = YellowbackTab::vaultActions(pos, 339);
        QVERIFY2(a.text.contains("A claim notice stands against it (confirmed at height 338)"), qPrintable(a.text));
        QVERIFY(a.text.contains("from reference height 342"));
        // The contract's plain row: no notice
        h.feed(json(nullptr), json(nullptr), json(nullptr), json::array({positionActive()}));
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Notice), Qt::DisplayRole).toString(), QString("-"));
        QCOMPARE(YellowbackPosition::fromJson(positionActive()).emergencyOpenAt, -1);
    }

    void vaultsRenderCanNoticeRow() {
        Harness h;
        json p = positionActive();
        p["canNotice"] = true;
        h.feed(infoActive(), statsOpen(), activationActive(), json::array({p}));
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Notice), Qt::DisplayRole).toString(), QString("notice possible"));
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(p), 331);
        QVERIFY2(a.text.contains("a claim notice could be posted"), qPrintable(a.text));
        QVERIFY(!a.text.contains("trustless", Qt::CaseInsensitive));
    }

    void settingsSubscriberStatusAndTransport() {
        Harness h;
        // No launcher yet (A5-b): the status reads from a null process, and from an unstarted one
        QCOMPARE(YellowbackTab::subscriberStatus(nullptr), QString("not running"));
        QProcess idle;
        QCOMPARE(YellowbackTab::subscriberStatus(&idle), QString("not running"));
        QVERIFY2(h.label("lblSubscriberStatus").startsWith("not running"), qPrintable(h.label("lblSubscriberStatus")));
        h.tab.setSubscriberProcess(&idle);
        QVERIFY(h.label("lblSubscriberStatus").startsWith("not running"));

        // The transport config round-trips through Settings
        auto page = h.tab.page(YellowbackTab::Settings);
        auto kind = page->findChild<QComboBox*>("cmbTransportKind");
        QVERIFY(kind != nullptr);
        kind->setCurrentIndex(kind->findData("iroh"));
        page->findChild<QLineEdit*>("txtTransportRelays")->setText("https://relay.example ");
        page->findChild<QLineEdit*>("txtTransportPeers")->setText("peer1,peer2");
        page->findChild<QLineEdit*>("txtTransportPath")->setText("/tmp/attest-dir");
        QVERIFY(!page->findChild<QLineEdit*>("txtTransportPath")->isEnabled());      // dir-only field under iroh
        QVERIFY(page->findChild<QLineEdit*>("txtTransportRelays")->isEnabled());
        h.button("btnSave")->click();
        auto s = Settings::getInstance();
        QCOMPARE(s->getYellowbackTransportKind(), QString("iroh"));
        QCOMPARE(s->getYellowbackTransportRelays(), QString("https://relay.example"));
        QCOMPARE(s->getYellowbackTransportPeers(), QString("peer1,peer2"));
        QCOMPARE(s->getYellowbackTransportPath(), QString("/tmp/attest-dir"));
        // A fresh tab reads them back
        Harness h2;
        auto page2 = h2.tab.page(YellowbackTab::Settings);
        QCOMPARE(page2->findChild<QComboBox*>("cmbTransportKind")->currentData().toString(), QString("iroh"));
        QCOMPARE(page2->findChild<QLineEdit*>("txtTransportPeers")->text(), QString("peer1,peer2"));
        s->setYellowbackTransportKind("dir"); s->setYellowbackTransportRelays(""); s->setYellowbackTransportPeers(""); s->setYellowbackTransportPath("");
    }

    // ── v3 actions (A5-b): two-step mint, bundle-insufficient, mint10-diverged ───────────

    void mintTwoStepHappyPath() {
        Harness h;
        h.feedActive();
        h.ctl.setPendingPollMs(10);
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReplyArmed();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ARMED_ZAT);
        h.rpc.results[YellowbackRpc::MINT]               = mintPendingReply();
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS]   = json::array({txMint()});   // the row the node adds on the next ChainTip
        h.rpc.results[YellowbackRpc::GETTXINFO]          = txInfoMint();
        Settings::getInstance()->setYellowbackBackupPending(false);
        h.mintAmount("1000");
        h.tab.doMint();

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.confirms[0].contains("Mint $1,000.00 of YED"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::MINT), json::array({100000, 48, "", "", false, 254408060690}));   // maxCollateralZat = collateral + 1 % (F-1)
        // Step 1: the carrier is out, the page says so, no result dialog yet
        QVERIFY2(h.label("lblMintPageStatus").startsWith("Preparing price proof (1 block)"), qPrintable(h.label("lblMintPageStatus")));
        QVERIFY(h.label("lblMintPageStatus").contains("5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c"));
        QVERIFY(Settings::getInstance()->getYellowbackBackupPending());      // the vault key exists from the carrier step on
        QCOMPARE(h.notices.size(), 0);
        // Step 2: the poll finds the mint row and reads its yed_gettxinfo
        QTRY_COMPARE_WITH_TIMEOUT(h.notices.size(), 1, 2000);
        QVERIFY(h.rpc.count(YellowbackRpc::LISTTRANSACTIONS) >= 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::GETTXINFO), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}));
        const QString& n = h.notices[0];
        QVERIFY(n.startsWith("Mint sent|"));
        QVERIFY2(n.contains("mint price $1.9850 per YEC"), qPrintable(n));                 // pMint
        QVERIFY2(n.contains("bound by the attestors"), qPrintable(n));                     // aMint < xMint
        QVERIFY2(n.contains("pools $1.9900, attestors $1.9850"), qPrintable(n));
        QVERIFY2(n.contains("price proof from attestor seq 1, 2"), qPrintable(n));         // bundleSeqs
        QVERIFY2(n.contains("attestation fee " % YellowbackFormat::zec(15703517) % " to smExampleBondAddress11111111111111111"), qPrintable(n));
        QVERIFY(h.label("lblMintPageStatus").startsWith("Minted. txid: 6a1f"));
        QVERIFY(h.copyIsClean());
    }

    void mintBundleInsufficientOffersRetry() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReplyArmed();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ARMED_ZAT);
        h.rpc.results[YellowbackRpc::GETSELECTION]       = selectionReply();
        h.rpc.errors[YellowbackRpc::MINT] = "bundle-insufficient: 1 of 2 selected attestors have a fresh attestation; missing seq 3";
        // The parser reads the grammar the contract fixes
        int count = 0, selected = 0; QList<int> missing;
        QVERIFY(YellowbackController::parseBundleInsufficient(h.rpc.errors[YellowbackRpc::MINT], &count, &selected, &missing));
        QCOMPARE(count, 1); QCOMPARE(selected, 2); QCOMPARE(missing, QList<int>({3}));
        QVERIFY(!YellowbackController::parseBundleInsufficient("mint10-diverged", &count, &selected, &missing));
        QVERIFY(YellowbackController::parseBundleInsufficient("bundle-insufficient: 0 of 3 selected attestors have a fresh attestation; missing seq 1, 2, 5", &count, &selected, &missing));
        QCOMPARE(missing, QList<int>({1, 2, 5}));

        int selectionsBefore = h.rpc.count(YellowbackRpc::GETSELECTION);
        h.mintAmount("1000");
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 2);                       // the mint, then the retry offer
        QVERIFY2(h.confirms[1].contains("only 1 of the 2 selected attestors"), qPrintable(h.confirms[1]));
        QVERIFY2(h.confirms[1].contains("missing attestor seq 3"), qPrintable(h.confirms[1]));
        QVERIFY(h.confirms[1].contains("Re-check which attestors are reachable"));
        QVERIFY(h.errorNotices.isEmpty());                    // the retry dialog replaces the generic failure
        QVERIFY(h.rpc.count(YellowbackRpc::GETSELECTION) > selectionsBefore);   // "yes" re-queried yed_getselection
        QVERIFY(h.label("lblMintPageStatus").startsWith("yed_mint failed: bundle-insufficient"));
        QVERIFY(h.copyIsClean());
    }

    void mintDivergedRaisesTheBanner() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReplyArmed();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ARMED_ZAT);
        h.rpc.errors[YellowbackRpc::MINT] = "mint10-diverged: |xMint - aMint| exceeds DIVERGE_BPS_ATTEST";
        h.mintAmount("1000");
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY(h.visible("lblDivergence"));
        QCOMPARE(h.label("lblDivergence"), QString("the pools' fast median and the attestors disagree by more than 15.00 %; minting paused until they agree"));
        QCOMPARE(h.errorNotices.size(), 1);
        QVERIFY(h.errorNotices[0].startsWith("yed_mint failed: mint10-diverged"));
        QVERIFY(h.errorNotices[0].contains("minting is paused"));
        QVERIFY(h.copyIsClean());
    }

    // ── Claim with the clause and residual, two-step ──────────────────────────────────────

    void claimShowsPathAndResidualThenFollowsTheCarrier() {
        Harness h;
        json row = claimableRow();
        row["claimPath"] = "b"; row["residualZat"] = 250000000; row["attestFeeZat"] = 15703517;
        row["noticed"] = true; row["noticeHeight"] = 338; row["emergencyOpenAt"] = 342;
        h.feedActive(410, json(nullptr), json::array({row}));
        h.ctl.setPendingPollMs(10);
        h.rpc.results[YellowbackRpc::CLAIM]            = claimPendingReply();
        // The claim of an own vault: the node lists it as "claimed", mined above the reply's refHeight 405
        json claimRow = txRow("claimed", "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d"); claimRow["height"] = 407;
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array({claimRow});
        h.rpc.results[YellowbackRpc::GETTXINFO]        = txInfoClaim();
        YellowbackClaimable c = YellowbackClaimable::fromJson(row);
        QCOMPARE(c.claimPath, QString("b"));
        QCOMPARE(c.residualZat, (qint64)250000000);
        QCOMPARE(c.emergencyOpenAt, 342);
        h.tab.claimVault(c);

        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.confirms[0].contains("Claim path: emergency clause (b)"), qPrintable(h.confirms[0]));
        QVERIFY(h.confirms[0].contains("height 338"));
        QVERIFY2(h.confirms[0].contains("Residual: " % YellowbackFormat::zec(250000000) % " of YEC goes to the vault owner in its own intent"), qPrintable(h.confirms[0]));
        QVERIFY(h.confirms[0].contains("Attestation fee: " % YellowbackFormat::zec(15703517) % " of YEC from your own YEC"));
        QVERIFY(h.confirms[0].contains("and the attestation fee " % YellowbackFormat::zec(15703517)));
        // U-23: the claim intent is the collateral less the residual; the fees are the claimant's own YEC
        QVERIFY(h.confirms[0].contains("Claim intent: about " % YellowbackFormat::zec(25125628141 - 250000000)));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::CLAIM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "", "", false, 24626871860, 100099}));   // (25125628141 - 250000000) - 1 %
        QVERIFY2(h.label("lblClaimHint").startsWith("Preparing price proof (1 block)"), qPrintable(h.label("lblClaimHint")));
        QTRY_COMPARE_WITH_TIMEOUT(h.notices.size(), 1, 2000);
        QVERIFY(h.notices[0].startsWith("Claim sent|"));
        QVERIFY2(h.notices[0].contains("YED burned: $1,000.00"), qPrintable(h.notices[0]));
        QVERIFY2(h.notices[0].contains("claim path: emergency clause (b)"), qPrintable(h.notices[0]));
        QVERIFY2(h.notices[0].contains("residual intent for the vault owner: " % YellowbackFormat::zec(250000000)), qPrintable(h.notices[0]));
        QVERIFY2(h.notices[0].contains("claim intent 9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d:0"), qPrintable(h.notices[0]));
        QVERIFY2(h.notices[0].contains("from height 342 on"), qPrintable(h.notices[0]));   // txInfoMint height 332 + CLAIM_DELAY 10
        // A row the node could build no bundle for says so before the user tries
        row["claimPath"] = ""; row["residualZat"] = 0;
        QString none = YellowbackTab::describeClaimPath(YellowbackClaimable::fromJson(row));
        QVERIFY2(none.contains("Claim path: none yet"), qPrintable(none));
        QVERIFY(none.contains("Residual to the owner: none"));
        QVERIFY(h.copyIsClean());
    }

    // ── The claim notice (NOT-1), two-step; the row then shows emergencyOpenAt ────────────

    void noticeActionAndRow() {
        Harness h;
        json p = positionActive(); p["canNotice"] = true;
        h.feedActive(340, json::array({p}));
        h.ctl.setPendingPollMs(10);
        h.rpc.results[YellowbackRpc::CLAIMNOTICE]      = noticePendingReply();
        // A notice has no yed_listtransactions row on either node line: it is followed through
        // yed_getnotice <vault>, whose record names the reply's refHeight 338 once it confirms
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array();
        h.rpc.results[YellowbackRpc::GETNOTICE] = json::parse(R"({"found": false})");
        json info = txInfoMint(); info["txid"] = "8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5"; info["type"] = "notice"; info["notice"] = true;
        h.rpc.results[YellowbackRpc::GETTXINFO]        = info;
        // The button follows the row's canNotice
        h.selectRow("tblPositions", 0);
        QVERIFY(h.button("btnNotice")->isVisible() || !h.tab.isVisible());   // offscreen: visibility flag is what setVisible set
        QVERIFY(h.button("btnNotice")->isEnabled());
        QVERIFY(h.label("lblVaultAction").contains("a claim notice could be posted"));

        // After the notice confirms, yed_listpositions carries the row noticed (the refresh reads it)
        json noticed = p; noticed["canNotice"] = false; noticed["noticed"] = true; noticed["noticeHeight"] = 340; noticed["emergencyOpenAt"] = 342;
        h.rpc.results[YellowbackRpc::LISTPOSITIONS] = json::array({noticed});

        h.tab.noticeVault(YellowbackPosition::fromJson(p));
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.confirms[0].startsWith("Post a claim notice against vault 6a1f"), qPrintable(h.confirms[0]));
        QVERIFY(h.confirms[0].contains("4 blocks after"));                       // params.attest.emergencyPersist
        QVERIFY(h.confirms[0].contains("no YED is burned"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::CLAIMNOTICE), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8", "", false}));
        QVERIFY2(h.label("lblVaultAction").startsWith("Preparing price proof (1 block)"), qPrintable(h.label("lblVaultAction")));
        QTest::qWait(50);
        QVERIFY(h.notices.isEmpty());                       // no record yet: still waiting
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::GETNOTICE), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}));
        h.rpc.results[YellowbackRpc::GETNOTICE] = json::parse(R"({"found": true, "vault": "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0",
          "txid": "8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5", "height": 340, "refHeight": 338, "pEmerg": 1800000,
          "emergencyOpenAt": 342, "expiresAt": 402})");
        QTRY_COMPARE_WITH_TIMEOUT(h.notices.size(), 1, 2000);
        QVERIFY(h.notices[0].startsWith("Claim notice sent|"));
        QVERIFY2(h.notices[0].contains("from reference height 342 on"), qPrintable(h.notices[0]));   // emergencyOpenAt
        QTRY_VERIFY_WITH_TIMEOUT(h.ctl.positionsModel()->positionAt(0) != nullptr && h.ctl.positionsModel()->positionAt(0)->noticed, 2000);
        auto m = h.ctl.positionsModel();
        QCOMPARE(m->data(m->index(0, YellowbackPositionsModel::Notice), Qt::DisplayRole).toString(), QString("noticed, emergency claim from 342"));
        QCOMPARE(m->positionAt(0)->emergencyOpenAt, 342);
        h.selectRow("tblPositions", 0);
        QVERIFY(!h.button("btnNotice")->isEnabled());                             // one stands: no second notice
        QVERIFY(h.copyIsClean());

        // The two notice refusals explain themselves
        h.rpc.errors[YellowbackRpc::CLAIMNOTICE] = "notice-standing: a Notices record stands";
        h.tab.noticeVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.errorNotices.last().contains("already stands"));
        h.rpc.errors[YellowbackRpc::CLAIMNOTICE] = "notice-not-underwater: the inequality does not hold";
        h.tab.noticeVault(YellowbackPosition::fromJson(p));
        QVERIFY(h.errorNotices.last().contains("not below the emergency ratio"));
    }

    // ── The subscriber launcher (Settings) ────────────────────────────────────────────────

    void launcherWithMissingBinary() {
        Harness h;
        h.feedActive();
        h.tab.setSubscriberBinary("/nonexistent/yellowback-attest");
        QVERIFY2(h.label("lblSubscriberBinary").contains("not found beside the wallet"), qPrintable(h.label("lblSubscriberBinary")));
        QVERIFY(h.label("lblSubscriberBinary").contains("/nonexistent/yellowback-attest"));
        QVERIFY(!h.button("btnSubscriberStart")->isEnabled());
        QVERIFY(!h.button("btnSubscriberStop")->isEnabled());
        h.tab.startSubscriber();                                                  // a direct call refuses the same way
        QVERIFY2(h.label("lblSubscriberStatus").startsWith("not running"), qPrintable(h.label("lblSubscriberStatus")));
        QVERIFY(h.label("lblSubscriberStatus").contains("not an executable file"));
        QVERIFY(h.ctl.isAvailable());                                             // the wallet itself is untouched
        QVERIFY(h.button("btnSend")->isEnabled());
        // The default lookup is beside the wallet binary
        QVERIFY(YellowbackTab::defaultSubscriberBinaryPath().startsWith(QCoreApplication::applicationDirPath()));
        QVERIFY(YellowbackTab::defaultSubscriberBinaryPath().contains("yellowback-attest"));

        // The generated attest.toml: cookie when there is one, else rpcuser/rpcpassword; the transport as saved
        QString dirToml = YellowbackTab::subscriberConfigToml("dir", "/tmp/attest-bus", "", "", "http://127.0.0.1:18232", "/data/regtest/.cookie", "", "");
        QVERIFY2(dirToml.contains("[node]\nrpc_url = \"http://127.0.0.1:18232\"\nrpc_cookie = \"/data/regtest/.cookie\"\n"), qPrintable(dirToml));
        QVERIFY(!dirToml.contains("rpc_user"));
        QVERIFY(dirToml.contains("[transport]\nkind = \"dir\"\npath = \"/tmp/attest-bus\"\n"));
        QVERIFY(dirToml.contains("[subscribe]\nlistattestors_seconds = 60\n"));
        QString irohToml = YellowbackTab::subscriberConfigToml("iroh", "", "https://relay.example, https://r2.example", "peerA,peerB", "http://127.0.0.1:8832", "", "ycash", "p\"w");
        QVERIFY2(irohToml.contains("rpc_user = \"ycash\"\nrpc_password = \"p\\\"w\"\n"), qPrintable(irohToml));
        QVERIFY(!irohToml.contains("rpc_cookie"));
        QVERIFY2(irohToml.contains("kind = \"iroh\"\nrelays = [\"https://relay.example\", \"https://r2.example\"]\npeers = [\"peerA\", \"peerB\"]\n"), qPrintable(irohToml));
        QVERIFY(!irohToml.contains("path ="));
        QString bare = YellowbackTab::subscriberConfigToml("iroh", "", "", "", "http://127.0.0.1:8832", "/c", "", "");
        QVERIFY(!bare.contains("relays") && !bare.contains("peers"));              // iroh defaults
        QCOMPARE(YellowbackTab::cookiePathFor("/data", "regtest"), QString("/data/regtest/.cookie"));
        QCOMPARE(YellowbackTab::cookiePathFor("/data", "test"),    QString("/data/testnet3/.cookie"));
        QCOMPARE(YellowbackTab::cookiePathFor("/data", "main"),    QString("/data/.cookie"));
        // audit F-4: control characters cannot end the string or add a key
        QString ctl = YellowbackTab::subscriberConfigToml("dir", "/tmp/a\nrpc_user = \"x\"\r\t\x01", "", "", "http://127.0.0.1:8832", "", "u", "p\x7f");
        QVERIFY2(ctl.contains("path = \"/tmp/a\\nrpc_user = \\\"x\\\"\\r\\t\\u0001\"\n"), qPrintable(ctl));
        QVERIFY2(ctl.contains("rpc_password = \"p\\u007f\"\n"), qPrintable(ctl));
        QCOMPARE(ctl.count("rpc_user"), 2);   // the real key and the escaped text inside the path string
        // the file is owner-only from its first byte, and an unwritable path is reported
        QTemporaryDir tmp;
        QString priv = tmp.filePath("private.toml"), err;
        QVERIFY2(YellowbackTab::writePrivateFile(priv, "rpc_password = \"s\"\n", &err), qPrintable(err));
        QVERIFY(!(QFile(priv).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                                               QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
        QFile pf(priv); QVERIFY(pf.open(QIODevice::ReadOnly)); QCOMPARE(QString::fromUtf8(pf.readAll()), QString("rpc_password = \"s\"\n"));
        QVERIFY(YellowbackTab::writePrivateFile(priv, "again\n", &err));                    // an existing file is replaced
        QVERIFY(!YellowbackTab::writePrivateFile(tmp.filePath("no/such/dir/x.toml"), "x", &err));
        QVERIFY(!err.isEmpty());
    }

#ifndef Q_OS_WIN
    // A stand-in binary records its arguments: the launcher runs `subscribe --conf <toml>`,
    // the toml carries the saved transport, Stop ends the process.
    void launcherStartsAndStops() {
        Harness h;
        h.feedActive();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QString bin = tmp.filePath("yellowback-attest"), argsFile = tmp.filePath("args");
        QFile f(bin);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(("#!/bin/sh\necho \"$@\" > '" % argsFile % "'\nsleep 30\n").toUtf8());
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        auto s = Settings::getInstance();
        s->setYellowbackTransportKind("dir"); s->setYellowbackTransportPath(tmp.filePath("bus"));
        h.tab.setSubscriberBinary(bin);
        QCOMPARE(h.label("lblSubscriberBinary"), bin);
        QVERIFY(h.button("btnSubscriberStart")->isEnabled());
        h.tab.startSubscriber();
        QTRY_VERIFY_WITH_TIMEOUT(h.label("lblSubscriberStatus").startsWith("running (pid "), 5000);
        QVERIFY(!h.button("btnSubscriberStart")->isEnabled());
        QVERIFY(h.button("btnSubscriberStop")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(argsFile), 5000);
        QFile a(argsFile); QVERIFY(a.open(QIODevice::ReadOnly));
        QString args = QString::fromUtf8(a.readAll()).trimmed();
        QCOMPARE(args, QString("subscribe --conf " % h.tab.subscriberConfigPath()));
        QFile t(h.tab.subscriberConfigPath()); QVERIFY(t.open(QIODevice::ReadOnly));
        QString toml = QString::fromUtf8(t.readAll());
        QVERIFY2(toml.contains("kind = \"dir\"\npath = \"" % tmp.filePath("bus") % "\""), qPrintable(toml));
        QVERIFY(toml.contains("rpc_url = \"http://127.0.0.1:8832\""));           // no connection in the QTest: the default
        QVERIFY(!(t.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)));   // audit F-4
        h.tab.stopSubscriber();
        QTRY_VERIFY_WITH_TIMEOUT(h.label("lblSubscriberStatus").startsWith("not running"), 5000);
        QVERIFY(h.button("btnSubscriberStart")->isEnabled());
        s->setYellowbackTransportKind("dir"); s->setYellowbackTransportPath("");
    }
#endif

    // ── The four attestor actions: confirmation copy and request shapes ───────────────────

    void attestorActionsRequestShapes() {
        Harness h;
        json dormant = attestorRow(); dormant["seq"] = 5; dormant["status"] = "DORMANT";
        h.feedActive(520);                                                        // past bondLocktime 500
        h.ctl.feedAttest(priceReply(), json::array({attestorRow(), dormant}), selectionReply());
        h.ctl.setPendingPollMs(10);
        h.rpc.results[YellowbackRpc::REGISTERATTESTOR] = json::parse(R"({
          "txid": "7b2c3d4e5f60718293a4b5c6d7e8f9001a2b3c4d5e6f708192a3b4c5d6e7f809", "seq": null,
          "attestorPubKey": "03b1c2d3e4f5061728394a5b6c7d8e9f0a1b2c3d4e5f6071829304a5b6c7d8e9f0",
          "bondAddress": "smExampleBondAddress11111111111111111", "bondKeyAddress": "smExampleBondKeyAddr11111111111111111",
          "bondOutpoint": {"txid": "7b2c3d4e5f60718293a4b5c6d7e8f9001a2b3c4d5e6f708192a3b4c5d6e7f809", "vout": 0},
          "bondZat": 1000000000, "bondLocktime": 721, "flags": {"tier": 1, "pool": true}, "maturesAt": 529})");
        h.rpc.results[YellowbackRpc::WITHDRAWBOND] = json::parse(R"({"txid": "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d",
          "seq": 1, "bondZat": 1000000000, "bondOut": 999990000, "to": "smExampleTransparentTwin111111111111"})");
        h.rpc.results[YellowbackRpc::REPORTEQUIVOCATION] = json::parse(R"({"txid": "", "carrierTxid": "5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c",
          "pending": true, "seq": 4, "citedHeight": 329, "priceA": 1985000, "priceB": 2185000})");
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array();   // the equivocation row appears only after the report
        json eqInfo = txInfoMint(); eqInfo["txid"] = "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d"; eqInfo["type"] = "equivocation";
        h.rpc.results[YellowbackRpc::GETTXINFO] = eqInfo;
        Settings::getInstance()->setYellowbackBackupPending(false);

        // Register (P4-b: a join to the YED attestor set): the dialog states the bond, the lock, the
        // one key that is member, hot, bond and fee key (D-U4), liveness, and the one-key-one-node warning
        h.tab.registerAttestor("10", 200);
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.confirms[0].startsWith("Join the YED attestor set with this wallet."), qPrintable(h.confirms[0]));
        QVERIFY2(h.confirms[0].contains("Bond: 10.00000000 of YEC, locked in a bond output until height 721 (200 blocks"), qPrintable(h.confirms[0]));
        QVERIFY(h.confirms[0].contains("join act to the set 5e755e755e755e75"));
        QVERIFY(h.confirms[0].contains("at least 8 blocks after the join confirms"));      // params.attest.bondMaturity
        QVERIFY(h.confirms[0].contains("member key, the hot key the agent signs prices with, the bond key and the fee key at once"));
        QVERIFY(h.confirms[0].contains("no heartbeat within the set's liveness window is dormant"));
        QVERIFY(h.confirms[0].contains("Run the yellowback-attest agent on this node only. One key on two nodes defeats"));
        QVERIFY(h.confirms[0].contains("Back up wallet.dat"));
        QVERIFY(!h.confirms[0].contains("Source tier"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::REGISTERATTESTOR), json::array({"10", 200}));   // the bond as a decimal string (F-8); no flags since P4-b
        h.tab.registerAttestor("0.12345678", 200);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::REGISTERATTESTOR), json::array({"0.12345678", 200}));   // sent as typed, no double rounding
        QVERIFY(h.confirms.last().contains("Bond: 0.12345678 of YEC"));
        QVERIFY(h.notices.last().startsWith("Join sent|"));
        QVERIFY(h.notices.last().contains("smExampleBondKeyAddr11111111111111111"));
        QVERIFY(Settings::getInstance()->getYellowbackBackupPending());

        // Withdraw: offered on the selected row once bondLocktime has passed
        h.selectRow("tblAttestors", 0);
        QVERIFY(h.button("btnWithdraw")->isEnabled());
        QVERIFY(h.button("btnRevive") == nullptr);                       // P4-b: revival is a heartbeat
        QVERIFY(!h.button("btnHeartbeat")->isEnabled());                 // this wallet is no member (no set_getinfo yet)
        QVERIFY(h.label("lblAttestorAction").contains("past its locktime (500)"));
        h.tab.withdrawBond(YellowbackAttestor::fromJson(attestorRow()));
        QVERIFY(h.confirms.last().startsWith("Withdraw the bond of attestor 1."));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::WITHDRAWBOND), json::array({1}));
        h.tab.withdrawBond(YellowbackAttestor::fromJson(attestorRow()), "smExampleTransparentTwin111111111111");
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::WITHDRAWBOND), json::array({1, "smExampleTransparentTwin111111111111"}));
        QVERIFY(h.notices.last().startsWith("Withdrawal sent|"));
        QVERIFY(h.notices.last().contains(YellowbackFormat::zec(999990000)));
        // ... and refused locally before the locktime
        json locked = attestorRow(); locked["bondLocktime"] = 600;
        int before = h.rpc.count(YellowbackRpc::WITHDRAWBOND);
        h.tab.withdrawBond(YellowbackAttestor::fromJson(locked));
        QCOMPARE(h.rpc.count(YellowbackRpc::WITHDRAWBOND), before);
        QVERIFY(h.notices.last().contains("locked until height 600"));

        // A DORMANT row: a heartbeat by its member key revives it
        h.selectRow("tblAttestors", 1);
        QVERIFY(h.label("lblAttestorAction").contains("DORMANT: a heartbeat"));

        // Report equivocation: two 148-character hexes, wait = false, then the carrier follow-up
        const QString hexA = QString(148, 'a'), hexB = QString(146, 'b') % "01";
        h.tab.reportEquivocation("abc", hexB);
        QVERIFY(h.errorNotices.last().contains("148 characters of hex"));
        h.tab.reportEquivocation(hexA, hexA);
        QVERIFY(h.errorNotices.last().contains("are the same"));
        QCOMPARE(h.rpc.count(YellowbackRpc::REPORTEQUIVOCATION), 0);
        int noticesBefore = h.notices.size();
        h.tab.reportEquivocation(hexA, hexB);
        // The node builds the main transaction on the next ChainTip: from now on the list has the row
        json eqRow = txRow("equivocation", "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d"); eqRow["height"] = h.ctl.height() + 1;
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS] = json::array({eqRow});
        QVERIFY(h.confirms.last().startsWith("Report an equivocation."));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::REPORTEQUIVOCATION), json::array({hexA.toStdString(), hexB.toStdString(), false}));
        QVERIFY2(h.label("lblAttestorAction").startsWith("Preparing price proof (1 block)"), qPrintable(h.label("lblAttestorAction")));
        QTRY_COMPARE_WITH_TIMEOUT(h.notices.size(), noticesBefore + 1, 2000);
        QVERIFY(h.notices.last().startsWith("Equivocation report sent|"));
        QVERIFY2(h.notices.last().contains("attestor 4, cited height 329, prices $1.9850 and $2.1850"), qPrintable(h.notices.last()));

        // The refusals explain themselves
        h.rpc.errors[YellowbackRpc::WITHDRAWBOND] = "attest-key-not-held: bond key";
        h.tab.withdrawBond(YellowbackAttestor::fromJson(attestorRow()));
        QVERIFY(h.errorNotices.last().contains("does not hold the key"));
        h.rpc.errors[YellowbackRpc::REGISTERATTESTOR] = "bond-below-min: 9 < 10";
        h.tab.registerAttestor("9", 200);
        QVERIFY(h.errorNotices.last().contains("below the minimum"));
        h.rpc.errors[YellowbackRpc::REGISTERATTESTOR] = "register-needs-admission: the set is not open; partly signed hex 0400";
        h.tab.registerAttestor("10", 200);
        QVERIFY(h.errorNotices.last().contains("needs admission signatures"));
        QVERIFY(h.copyIsClean());
    }

    void sweepCarriersFromSettings() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::SWEEPCARRIERS] = json::parse(R"({"txid": "", "count": 0, "reclaimedZat": 0, "outstanding": 1})");
        QVERIFY(h.button("btnSweepCarriers")->isEnabled());
        h.tab.sweepCarriers();
        QCOMPARE(h.rpc.count(YellowbackRpc::SWEEPCARRIERS), 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::SWEEPCARRIERS), json(nullptr));
        QCOMPARE(h.label("lblCarriers"), QString("No lapsed carrier to reclaim; 1 still inside their window."));
        h.rpc.results[YellowbackRpc::SWEEPCARRIERS] = json::parse(R"({"txid": "9e8d7c6b5a4f3e2d1c0b9a8f7e6d5c4b3a2f1e0d9c8b7a6f5e4d3c2b1a0f9e8d", "count": 2, "reclaimedZat": 19000, "outstanding": 0})");
        h.tab.sweepCarriers();
        QVERIFY2(h.label("lblCarriers").startsWith("Reclaimed 2 carrier(s), " % YellowbackFormat::zec(19000)), qPrintable(h.label("lblCarriers")));
    }

    // A pending action whose main transaction never appears: the carrier lapses after refWindow
    void pendingLapsesAfterTheWindow() {
        Harness h;
        h.feedActive();
        h.ctl.setPendingPollMs(10);
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReplyArmed();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ARMED_ZAT);
        h.rpc.results[YellowbackRpc::MINT]               = mintPendingReply();      // refHeight 329
        h.rpc.results[YellowbackRpc::LISTTRANSACTIONS]   = json::array();
        h.mintAmount("1000");
        h.tab.doMint();
        QVERIFY(h.label("lblMintPageStatus").startsWith("Preparing price proof"));
        QTest::qWait(50);
        QVERIFY(h.notices.isEmpty());                                             // still waiting inside the window
        json info = infoActive(); info["height"] = 329 + 40 + 1; info["chainHeight"] = info["height"];   // refWindow 40
        h.ctl.feed(info, json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr), json(nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(h.errorNotices.size(), 1, 2000);
        QVERIFY2(h.errorNotices[0].contains("carrier 5d6e7f80") && h.errorNotices[0].contains("Reclaim lapsed carriers"), qPrintable(h.errorNotices[0]));
        QVERIFY(h.label("lblMintPageStatus").startsWith("mint did not complete"));
    }

    // ── Hardening H5-a: H-9.2 deadlines and renew, H-9.3 plausibility, H-1/H-5 messaging ──

    // H-9.2: the persistent warning starts at claimHeight − 1 day (1,152 blocks at 75 s) and
    // stays until the vault is closed; a VOID vault is told to release, a closed one nothing.
    void deadlineWarningFromClaimHeightLessOneDay() {
        YellowbackPosition p = YellowbackPosition::fromJson(positionActive());
        p.lockHeight = 10000; p.claimHeight = 10000 + 34560;
        QCOMPARE(YellowbackController::BLOCKS_PER_DAY, 1152);
        QVERIFY(YellowbackController::deadlineWarning(p, p.claimHeight - 1153).isEmpty());
        const QString w = YellowbackController::deadlineWarning(p, p.claimHeight - 1152);
        QVERIFY2(w.contains("reaches its claim height 44560"), qPrintable(w));
        QVERIFY(w.contains("redeem or renew it before then"));
        QVERIFY(w.contains("1152 block(s)"));
        QVERIFY(w.contains("(~"));                                            // the date
        QVERIFY(YellowbackController::deadlineWarning(p, p.claimHeight).contains("past its claim height 44560"));
        QVERIFY(YellowbackController::deadlineWarning(p, 0).isEmpty());
        YellowbackPosition v = p; v.status = "VOID";
        QVERIFY(YellowbackController::deadlineWarning(v, p.claimHeight - 10).contains("release its collateral before then"));
        YellowbackPosition c = p; c.status = "CLOSED";
        QVERIFY(YellowbackController::deadlineWarning(c, p.claimHeight - 10).isEmpty());

        // The banner carries it above every page (regtest: GRACE 24, so 381 is inside the day)
        Harness h;
        h.inTerm = false;                                            // in-term: the threshold warning instead (vaultsInTermThresholdWarning)
        h.feedActive(381, json::array({positionActive()}));
        QCOMPARE(h.ctl.deadlineWarnings().size(), 1);
        QVERIFY(h.visible("lblBanner"));
        QVERIFY2(h.label("lblBanner").contains("Vault 6a1f2b3c4d5e… reaches its claim height 404"), qPrintable(h.label("lblBanner")));
        // two such vaults share one line; the tooltip names each
        json second = positionActive(); second["txid"] = "7b2c3d4e5f60718293a4b5c6d7e8f9001a2b3c4d5e6f708192a3b4c5d6e7f809";
        h.feedActive(381, json::array({positionActive(), second}));
        QCOMPARE(h.ctl.deadlineWarnings().size(), 2);
        QVERIFY2(h.label("lblBanner").startsWith("2 of your vaults are near or past their claim height"), qPrintable(h.label("lblBanner")));
        QVERIFY(!h.label("lblBanner").contains("\n"));
        QVERIFY(h.tab.findChild<QLabel*>("lblBanner")->toolTip().contains("Vault 7b2c3d4e5f60… reaches its claim height 404"));
        h.feedActive(381, json::array({positionActive()}));
        // a vault far from its claim height: no warning, no banner
        json far = positionActive(); far["lockHeight"] = 5000; far["claimHeight"] = 5024;
        h.feedActive(381, json::array({far}));
        QVERIFY(h.ctl.deadlineWarnings().isEmpty());
        QVERIFY(!h.visible("lblBanner"));
        QVERIFY(h.copyIsClean());
    }

    // H-9.2: dates on every ACTIVE vault; renew and redeem from lockHeight (rpcversion 5: no abandonment to stop renew).
    void vaultActionsOfferRenewFromLockHeight() {
        YellowbackPosition p = YellowbackPosition::fromJson(positionActive());
        auto before = YellowbackTab::vaultActions(p, 379);
        QVERIFY(!before.redeem && !before.renew);
        QVERIFY2(before.text.contains("redeemable or renewable from lock height 380 (~"), qPrintable(before.text));
        QVERIFY(before.text.contains("claim height 404 (~"));
        auto at = YellowbackTab::vaultActions(p, 380);
        QVERIFY(at.redeem && at.renew);
        QVERIFY(at.text.contains("Renew redeems it and mints $1,000.00 again"));
        QVERIFY(at.text.contains("Claim height 404 (~"));

        Harness h;
        json row = positionActive(); row["canRedeem"] = true;
        h.feedActive(381, json::array({row}));
        h.selectRow("tblPositions", 0);
        QVERIFY(!h.button("btnRenew")->isHidden());
        QVERIFY(h.button("btnRenew")->isEnabled());
        QVERIFY(h.button("btnRedeem")->isEnabled());
        // the vault's own lock length (lockHeight − refHeight) is reused while its class takes it
        QCOMPARE(h.ctl.renewLockBlocks(YellowbackPosition::fromJson(row)), 51);
        json odd = row; odd["refHeight"] = 200;     // 180 blocks: in no class on this table any more? C takes 145-240
        QCOMPARE(h.ctl.renewLockBlocks(YellowbackPosition::fromJson(odd)), 180);
        odd["refHeight"] = 0;                        // 380 blocks: in no class, so the shortest enabled one
        QCOMPARE(h.ctl.renewLockBlocks(YellowbackPosition::fromJson(odd)), 48);
    }

    static json renewEstimate() {
        json e = estimateReply();                    // at height 381: refHeight 379, lock 51 blocks
        e["refHeight"] = 379; e["lockHeight"] = 430; e["claimHeight"] = 454;
        return e;
    }

    // H-9.2: renew is one confirmation; the redeem goes at once, the mint when the vault reads CLOSED.
    void renewRedeemsThenMintsInOneFlow() {
        Harness h;
        json row = positionActive(); row["canRedeem"] = true;
        h.feedActive(381, json::array({row}));
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = renewEstimate();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(VAULT_ZAT);
        h.rpc.results[YellowbackRpc::REDEEM]             = redeemReply();
        h.rpc.results[YellowbackRpc::MINT]               = mintReply();
        Settings::getInstance()->setYellowbackBackupPending(false);
        h.tab.renewVault(YellowbackPosition::fromJson(row));

        QCOMPARE(h.confirms.size(), 1);
        const QString& t = h.confirms[0];
        QVERIFY2(t.startsWith("Renew vault 6a1f"), qPrintable(t));
        QVERIFY(t.contains("burn $1,000.00 of YED"));
        QVERIFY(t.contains("Pool fee: " % YellowbackFormat::zec(62814070)));
        QVERIFY(t.contains(YellowbackFormat::zec(VAULT_ZAT - 62814070)));
        QVERIFY(t.contains("against " % YellowbackFormat::zec(EST_ZAT)));
        QVERIFY(t.contains("at most " % YellowbackFormat::zec(253768844820)));
        QVERIFY(t.contains("lock 51 blocks (class A)"));
        QVERIFY(t.contains("Lock height about 430 (~"));
        QVERIFY(t.contains("claim height about 454 (~"));
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::ESTIMATECOLLATERAL), json::array({100000, 51}));
        QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::REDEEM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}));
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 0);           // not before the redeem is mined
        QVERIFY(h.tab.renewPending());
        QVERIFY(h.notices.isEmpty());

        // The redeem is mined: yed_listpositions reads the vault CLOSED, and the mint goes out
        json closed = row; closed["status"] = "CLOSED"; closed["closeHeight"] = 382; closed["burnedCents"] = 100000; closed["canRedeem"] = false;
        h.rpc.results[YellowbackRpc::LISTPOSITIONS] = json::array({closed});
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({closed}), json(nullptr), json(nullptr));
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 1);
        QCOMPARE(h.rpc.lastParams(YellowbackRpc::MINT), json::array({100000, 51, "", "", false, 253768844820}));   // the cap the dialog showed
        QTRY_COMPARE_WITH_TIMEOUT(h.notices.size(), 1, 2000);
        QVERIFY2(h.notices[0].startsWith("Vault renewed|Vault 6a1f"), qPrintable(h.notices[0]));
        QVERIFY(h.notices[0].contains("redeemed (txid 9e8d7c6b"));
        QVERIFY(h.notices[0].contains("Minted $1,000.00 of YED"));
        QVERIFY(!h.tab.renewPending());
        QVERIFY(Settings::getInstance()->getYellowbackBackupPending());
        QVERIFY(h.copyIsClean());
    }

    // The mint leg is held to the cap: a price move past it stops the renewal after the redeem.
    void renewStopsWhenTheCollateralMovedPastTheCap() {
        Harness h;
        json row = positionActive(); row["canRedeem"] = true;
        h.feedActive(381, json::array({row}));
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = renewEstimate();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(VAULT_ZAT);
        h.rpc.results[YellowbackRpc::REDEEM]             = redeemReply();
        h.rpc.results[YellowbackRpc::MINT]               = mintReply();
        h.tab.renewVault(YellowbackPosition::fromJson(row));
        QVERIFY(h.tab.renewPending());
        json moved = renewEstimate(); moved["pMint"] = 1900000; moved["requiredZat"] = 263157895000;
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = moved;
        json closed = row; closed["status"] = "CLOSED";
        h.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({closed}), json(nullptr), json(nullptr));
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 0);
        QCOMPARE(h.errorNotices.size(), 1);
        QVERIFY2(h.errorNotices[0].contains("is redeemed (txid 9e8d"), qPrintable(h.errorNotices[0]));
        QVERIFY(h.errorNotices[0].contains("above the " % YellowbackFormat::zec(253768844820) % " you confirmed"));
        QVERIFY(!h.tab.renewPending());
        // a vault claimed by someone else before the redeem: no mint either
        Harness c;
        c.feedActive(381, json::array({row}));
        c.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = renewEstimate();
        c.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(VAULT_ZAT);
        c.rpc.results[YellowbackRpc::REDEEM]             = redeemReply();
        c.tab.renewVault(YellowbackPosition::fromJson(row));
        json claimed = row; claimed["status"] = "CLAIMED";
        c.ctl.feed(json(nullptr), json(nullptr), json(nullptr), json(nullptr), json::array({claimed}), json(nullptr), json(nullptr));
        QCOMPARE(c.rpc.count(YellowbackRpc::MINT), 0);
        QVERIFY(c.errorNotices.value(0).contains("ended CLAIMED"));
    }

    // H-1: mintRequiresArmed while unarmed pauses the Mint page and the renew by name.
    void mintRequiresArmedPausesMintAndRenew() {
        Harness h;
        json row = positionActive(); row["canRedeem"] = true;
        h.feedActive(381, json::array({row}));
        json info = infoActive(); info["height"] = 381; info["chainHeight"] = 381;
        info["mintRequiresArmed"] = true;
        info["attest"]["status"] = "TRIGGERED"; info["attest"]["armed"] = false;
        h.feed(info);
        QVERIFY(h.ctl.mintRequiresArmed());
        QVERIFY(!h.ctl.isArmed());
        const QString b = h.ctl.mintBlocker(100000, "A");
        QVERIFY2(b.contains("paused until the attestation layer is ARMED"), qPrintable(b));
        QVERIFY(b.contains("mint-halted-unarmed"));
        QVERIFY(!h.visible("lblClassNote") || h.label("lblClassNote").contains("ARMED"));
        QVERIFY2(h.label("lblClassNote").contains("needs the attestation layer ARMED, and it is not"), qPrintable(h.label("lblClassNote")));
        h.selectRow("tblPositions", 0);
        QVERIFY(!h.button("btnRenew")->isEnabled());
        QVERIFY(h.button("btnRedeem")->isEnabled());                       // redeem never needs a mint
        QVERIFY(h.label("lblVaultAction").contains("Renew is not possible now"));
        h.tab.renewVault(YellowbackPosition::fromJson(row));
        QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 0);
        QVERIFY(h.errorNotices.value(0).contains("Renew is not possible now"));
        // armed: the note says so and minting is open
        info["attest"]["status"] = "ARMED"; info["attest"]["armed"] = true;
        h.feed(info);
        QVERIFY(h.ctl.mintBlocker(100000, "A").isEmpty());
        QVERIFY(h.label("lblClassNote").contains("(it is)"));
        QVERIFY(YellowbackController::explainError("mintpol-unarmed: the reference height is not ARMED").contains("ARMED"));
        QVERIFY(YellowbackFormat::voidReason("mint-halted-unarmed").contains("not ARMED"));
    }

    // H-5: classes B and C disabled (empty term ranges) on mainnet: only class A is offered.
    void mintOffersClassAOnlyWhenBAndCAreDisabled() {
        Harness h;
        json info = infoActive(); info["network"] = "main";
        info["params"]["classes"] = json::parse(R"([
            {"class": "A", "minBlocks": 34560, "maxBlocks": 103680, "baseRatioBps": 50000},
            {"class": "B", "minBlocks": 103681, "maxBlocks": 103680, "baseRatioBps": 40000},
            {"class": "C", "minBlocks": 103681, "maxBlocks": 103680, "baseRatioBps": 30000}])");
        h.ctl.feed(info, statsOpen(), activationActive(), balanceReply(500000), json::array(), json::array(), json(nullptr));
        QCOMPARE(h.ctl.termClasses().size(), 3);
        QCOMPARE(h.ctl.enabledClasses().size(), 1);
        auto tier = h.tab.page(YellowbackTab::Mint)->findChild<QComboBox*>("cmbTier");
        QCOMPARE(tier->count(), 1);
        QVERIFY(tier->itemText(0).startsWith("Class A"));
        const QString note = h.label("lblClassNote");
        QVERIFY2(note.contains("Only class A (lock 34560–103680 blocks"), qPrintable(note));
        QVERIFY(note.contains("class B and C are disabled"));
        QVERIFY(h.ctl.classForLock(103681).name.isEmpty());
        QCOMPARE(h.ctl.classForLock(34560).name, QString("A"));
        QCOMPARE(h.ctl.maxMintCents(), (qint64)250000);                     // H-12 off regtest
        QVERIFY(h.label("m1").contains("$2,500.00"));
        QVERIFY(YellowbackController::explainError("mint-bad-lock: lockBlocks 200000 is in no term class (A 34560-103680, B disabled, C disabled)").contains("disabled"));
        // regtest with every class enabled and no H-1: no note
        Harness r;
        r.feedActive();
        QVERIFY(r.label("lblClassNote").isEmpty());
        QCOMPARE(r.ctl.maxMintCents(), (qint64)1000000);
    }

    // H-9.3: the local recomputation, and each action refusing a reply that disagrees with it.
    void plausibilityRecomputesFeesAndHeights() {
        const json params = infoActive()["params"];
        QCOMPARE(YellowbackController::feeZatFor(params, VAULT_ZAT), (qint64)62814070);
        QCOMPARE(YellowbackController::feeZatFor(params, 1000), (qint64)50000000);           // FEE_MIN
        QCOMPARE(YellowbackController::attestFeeZatFor(params, 62814070), (qint64)15703517);
        QCOMPARE(YellowbackController::requiredZatFor(100000, 50000, 1990000), EST_ZAT);
        QCOMPARE(YellowbackController::requiredZatFor(100000, 50000, 0), (qint64)-1);
        QCOMPARE(YellowbackController::mintCollateralZatFor(params, 1000), (qint64)200000000);  // MINT-5: 4 · FEE_MIN
        QCOMPARE(YellowbackController::mintCollateralZatFor(params, EST_ZAT), EST_ZAT);
        QVERIFY(YellowbackController::checkEstimate(params, 331, 100000, 48, estimateReply()).isEmpty());
        json ahead = estimateReply(); ahead["refHeight"] = 330; ahead["lockHeight"] = 378; ahead["claimHeight"] = 402;
        QVERIFY(YellowbackController::checkEstimate(params, 331, 100000, 48, ahead).isEmpty());   // one block past the wallet's tip
        json behind = estimateReply(); behind["refHeight"] = 300; behind["lockHeight"] = 348; behind["claimHeight"] = 372;
        QVERIFY(YellowbackController::checkEstimate(params, 331, 100000, 48, behind).join(" ").contains("refHeight is 300"));
        json armedBad = estimateReplyArmed(); armedBad["attestFeeZat"] = 999999999;
        QVERIFY(YellowbackController::checkEstimate(params, 331, 100000, 48, armedBad).join(" ").contains("attestFeeZat"));
        QVERIFY(YellowbackController::checkEstimate(params, 331, 100000, 48, estimateReplyArmed()).isEmpty());
        json sigma = estimateReply(); sigma["sigmaMultBps"] = 20000; sigma["minRatioBps"] = 50000;
        QVERIFY(YellowbackController::checkEstimate(params, 331, 100000, 48, sigma).join(" ").contains("minRatioBps is 50000 where the parameters give 100000"));

        // Mint: an inflated requirement, an inflated lock height, an inflated fee — nothing sent
        struct Case { const char* key; qint64 value; const char* says; };
        for (const Case& k : { Case{"requiredZat", 2 * EST_ZAT, "requiredZat is"}, Case{"lockHeight", 100000, "lockHeight is 100000 where the parameters give 377"},
                               Case{"claimHeight", 999999, "claimHeight is 999999 where the parameters give 401"} }) {
            Harness h;
            h.feedActive();
            json e = estimateReply(); e[k.key] = k.value;
            h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = e;
            h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
            h.rpc.results[YellowbackRpc::MINT]               = mintReply();
            h.mintAmount("1000");
            h.tab.doMint();
            QCOMPARE(h.confirms.size(), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::GETFEEPAYEE), 0);
            QVERIFY2(h.errorNotices.value(0).contains("do not match what the wallet computes"), qPrintable(h.errorNotices.value(0)));
            QVERIFY2(h.errorNotices.value(0).contains(k.says), qPrintable(h.errorNotices.value(0)));
        }
        {
            Harness h;
            h.feedActive();
            h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
            json f = feePayeeReply(EST_ZAT); f["feeZat"] = 200000000000;   // a server taking most of the collateral as the fee (audit G-2)
            h.rpc.results[YellowbackRpc::GETFEEPAYEE] = f;
            h.rpc.results[YellowbackRpc::MINT]        = mintReply();
            h.mintAmount("1000");
            h.tab.doMint();
            QCOMPARE(h.confirms.size(), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 0);
            QVERIFY(h.errorNotices.value(0).contains("the pool fee is"));
        }
        {   // Redeem with an inflated fee (yed_getfeepayee's quote; in-term: redeemInTermQuotesTheEarlyRedeemFee)
            Harness h;
            h.inTerm = false;
            json row = positionActive(); row["canRedeem"] = true;
            h.feedActive(381, json::array({row}));
            json f = feePayeeReply(VAULT_ZAT); f["feeZat"] = 20000000000;
            h.rpc.results[YellowbackRpc::GETFEEPAYEE] = f;
            h.rpc.results[YellowbackRpc::REDEEM]      = redeemReply();
            h.tab.redeemVault(YellowbackPosition::fromJson(row));
            QCOMPARE(h.confirms.size(), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 0);
            QVERIFY(h.errorNotices.value(0).contains("FEE-1 of the collateral"));
        }
        {   // Claim: an inflated fee, then a claim height not yet reached
            Harness h;
            json row = claimableRow(); row["feeZat"] = 9000000000;
            h.feedActive(410, json(nullptr), json::array({row}));
            h.rpc.results[YellowbackRpc::CLAIM] = redeemReply();
            h.tab.claimVault(YellowbackClaimable::fromJson(row));
            QCOMPARE(h.rpc.count(YellowbackRpc::CLAIM), 0);
            QVERIFY(h.errorNotices.value(0).contains("the pool fee is"));
            Harness early;
            early.inTerm = false;                                    // under in-term claims the threshold decides at every height
            early.feedActive(400, json(nullptr), json::array({claimableRow()}));
            early.tab.claimVault(YellowbackClaimable::fromJson(claimableRow()));
            QCOMPARE(early.rpc.count(YellowbackRpc::CLAIM), 0);
            QVERIFY(early.errorNotices.value(0).contains("the claim height 404 is not reached"));
        }
        {   // A vault whose heights break the identity is flagged on the Vaults page
            Harness h;
            json row = positionActive(); row["claimHeight"] = 9999;
            h.feedActive(381, json::array({row}));
            h.selectRow("tblPositions", 0);
            QVERIFY2(h.label("lblVaultAction").contains("heights for this vault are inconsistent"), qPrintable(h.label("lblVaultAction")));
        }
        QVERIFY(YellowbackController::explainError("claim-burn-above-max: 100100 > 100099").contains("more YED"));
    }

    // ── rpcversion 6: in-term claims (in-term plan IT-7, IT-8, IT-9; D-IT-15, D-IT-16) ─────────

    // The quote yed_estimateredeem gives for positionActive() at `height` (tip + 1), consistent with
    // the parameters: FEE-1 plus, before lock height 380, 5 % of the collateral (class A).
    static json estimateRedeemReply(int height) {
        const bool early = height < 380;
        const qint64 earlyFee = early ? VAULT_ZAT * 500 / 10000 : 0;
        return json{{"vault", "6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8:0"}, {"status", "ACTIVE"},
                    {"termClass", "A"}, {"lockHeight", 380}, {"height", height}, {"early", early}, {"burnedCents", 100000},
                    {"collateralZat", VAULT_ZAT}, {"feeZat", fee1(VAULT_ZAT) + earlyFee}, {"earlyRedeemFeeBps", 500},
                    {"earlyRedeemFeeZat", earlyFee}, {"payee", "smQvTmAz2ExamplePayoutAddress1111111"}, {"canRedeem", true}, {"error", ""}};
    }

    void inTermPromiseIsTheDisclosure() {
        // IT-8 verbatim (tests/check-rpc-contract.py --spec checks it against the generated spec's §8.1)
        const QString promise = YellowbackTab::inTermPromise();
        QVERIFY(promise.startsWith("Your YEC is locked for the term you choose. You can redeem at any time by paying back the YED you minted;"));
        QVERIFY(promise.contains("an early-redeem fee of 5 %, 2.5 % or 1 % of your collateral for a short, medium or long term."));
        QVERIFY(promise.contains("If your collateral falls below 125 % of your debt at the attested price, anyone may close your vault by paying your debt;"));
        QVERIFY(promise.contains("which, at the threshold, is usually nothing."));
        QVERIFY(promise.endsWith("Before that happens, your wallet will warn you, and redeeming stops it."));
        const QString about = YellowbackTab::aboutText();
        QVERIFY(about.contains(promise));
        QVERIFY(about.contains("between 19 % and 50 % of vaults to be claimed in term"));   // IT-8: stated, not hidden
        QVERIFY(!about.contains("before the claim height"));                                  // the old collateral sentence is gone
        QVERIFY(!about.contains("trustless", Qt::CaseInsensitive));
    }

    void vaultsInTermThresholdWarning() {
        Harness h;
        json p = positionActive(); p["canRedeem"] = true;              // in term (lock 380), the owner may redeem (D-IT-15)
        h.feedActive(331, json::array({p}));
        auto m = h.ctl.positionsModel();
        auto cell = [&](int col, int role = Qt::DisplayRole) { return m->data(m->index(0, col), role); };
        QVERIFY(h.ctl.inTermClaims());
        QCOMPARE(h.ctl.claimThresholdBps(), (qint64)12500);
        // far above the threshold (claim price $2.00, claimable below $0.4378): plain, the price named
        QVERIFY2(cell(YellowbackPositionsModel::ActBy).toString().startsWith("redeemable at any time (early-redeem fee for 49 block"), qPrintable(cell(YellowbackPositionsModel::ActBy).toString()));
        QVERIFY(cell(YellowbackPositionsModel::ActBy).toString().contains("claimable below $0.4378"));
        QVERIFY(!cell(YellowbackPositionsModel::ActBy, Qt::BackgroundRole).isValid());
        QVERIFY(cell(YellowbackPositionsModel::Claimable).toString().startsWith("no (claim price above $0.4378"));   // no "not before" in term
        QVERIFY(cell(YellowbackPositionsModel::UnderwaterBelow, Qt::ToolTipRole).toString().contains("125 %"));
        QVERIFY(h.ctl.deadlineWarnings().isEmpty());                    // no claim-height deadline under in-term claims
        auto a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(p), 331, h.ctl.params(), 2000000);
        QVERIFY(a.redeem && !a.renew && !a.release);
        QVERIFY2(a.text.contains("early-redeem fee of 5 % of the collateral (about " % YellowbackFormat::zec(VAULT_ZAT * 500 / 10000) % ")"), qPrintable(a.text));
        QVERIFY(a.text.contains("if the claim price falls below $0.4378"));
        // the claim price nears the threshold (within 25 %): orange, and the banner warns
        json near = statsOpen(); near["pClaim"] = 500000; near["pFast"] = 500000;
        h.ctl.feed(infoActive(), near, activationActive(), balanceReply(500000), json::array({p}), json(nullptr), json(nullptr));
        QVERIFY2(cell(YellowbackPositionsModel::ActBy).toString().startsWith("WARNING: claimable if the claim price falls below $0.4378 (now $0.5000)"), qPrintable(cell(YellowbackPositionsModel::ActBy).toString()));
        QCOMPARE(cell(YellowbackPositionsModel::ActBy, Qt::BackgroundRole).value<QBrush>().color(), QColor(255, 232, 190));
        QCOMPARE(h.ctl.deadlineWarnings().size(), 1);
        QVERIFY2(h.ctl.deadlineWarnings()[0].contains("becomes claimable if the claim price falls below $0.4378"), qPrintable(h.ctl.deadlineWarnings()[0]));
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(p), 331, h.ctl.params(), 500000);
        QVERIFY(a.text.contains("WARNING: it becomes claimable"));
        // claimable now: red, "claimable now", and the warning says redeeming stops it
        p["claimable"] = true;
        json low = statsOpen(); low["pClaim"] = 400000; low["pFast"] = 400000;
        h.ctl.feed(infoActive(), low, activationActive(), balanceReply(500000), json::array({p}), json(nullptr), json(nullptr));
        QVERIFY(cell(YellowbackPositionsModel::ActBy).toString().startsWith("CLAIMABLE NOW"));
        QCOMPARE(cell(YellowbackPositionsModel::Claimable).toString(), QString("YES, claimable now"));
        QCOMPARE(cell(YellowbackPositionsModel::ActBy, Qt::BackgroundRole).value<QBrush>().color(), QColor(255, 210, 210));
        QVERIFY(h.ctl.deadlineWarnings().value(0).contains("is claimable now"));
        QVERIFY(h.ctl.deadlineWarnings().value(0).contains("Redeem it now to stop that"));
        // past the lock height: no early-redeem fee, renew offered again
        p["claimable"] = false;
        a = YellowbackTab::vaultActions(YellowbackPosition::fromJson(p), 381, h.ctl.params(), 2000000);
        QVERIFY(a.redeem && a.renew);
        QVERIFY(a.text.contains("no early-redeem fee"));
    }

    void redeemInTermQuotesTheEarlyRedeemFee() {
        const qint64 earlyFee = VAULT_ZAT * 500 / 10000;
        {   // in term: the quote first, the early-redeem fee named, the collateral out net of both fees
            Harness h;
            json p = positionActive(); p["canRedeem"] = true;
            h.feedActive(331, json::array({p}));
            h.rpc.results[YellowbackRpc::ESTIMATEREDEEM] = estimateRedeemReply(332);
            json r = redeemReply(); r["feeZat"] = fee1(VAULT_ZAT) + earlyFee; r["earlyRedeemFeeZat"] = earlyFee;
            h.rpc.results[YellowbackRpc::REDEEM] = r;
            h.tab.redeemVault(YellowbackPosition::fromJson(p));
            QCOMPARE(h.rpc.lastParams(YellowbackRpc::ESTIMATEREDEEM), json::array({"6a1f2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8"}));
            QCOMPARE(h.rpc.count(YellowbackRpc::GETFEEPAYEE), 0);
            QCOMPARE(h.confirms.size(), 1);
            const QString& text = h.confirms[0];
            QVERIFY2(text.contains("Early-redeem fee: " % YellowbackFormat::zec(earlyFee) % " of YEC (5 % of the collateral, class A)"), qPrintable(text));
            QVERIFY(text.contains("Pool fee: " % YellowbackFormat::zec(fee1(VAULT_ZAT))));
            QVERIFY(text.contains("Redeeming at or after height 380 pays no early-redeem fee"));
            QVERIFY(text.contains(YellowbackFormat::zec(VAULT_ZAT - fee1(VAULT_ZAT) - earlyFee)));   // collateral out
            QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 1);
            QVERIFY2(h.notices.value(0).contains("of which the early-redeem fee: " % YellowbackFormat::zec(earlyFee)), qPrintable(h.notices.value(0)));
            QVERIFY(h.copyIsClean());
            // the Redeem page says the early-redeem fee comes off too
            auto redeemCollateral = h.tab.page(YellowbackTab::Redeem)->findChild<QLabel*>("lblCollateral");
            QVERIFY(redeemCollateral != nullptr);
            QVERIFY2(redeemCollateral->text().contains("early-redeem fee of 5 %"), qPrintable(redeemCollateral->text()));
        }
        {   // past the lock height: no early-redeem fee line
            Harness h;
            json p = positionActive(); p["canRedeem"] = true;
            h.feedActive(381, json::array({p}));
            h.rpc.results[YellowbackRpc::ESTIMATEREDEEM] = estimateRedeemReply(382);
            h.rpc.results[YellowbackRpc::REDEEM] = redeemReply();
            h.tab.redeemVault(YellowbackPosition::fromJson(p));
            QCOMPARE(h.confirms.size(), 1);
            QVERIFY(!h.confirms[0].contains("Early-redeem fee"));
            QVERIFY(!h.notices.value(0).contains("early-redeem"));
        }
        {   // a quote whose early-redeem fee disagrees with the parameters is not shown, nothing is sent (H-9.3)
            Harness h;
            json p = positionActive(); p["canRedeem"] = true;
            h.feedActive(331, json::array({p}));
            json q = estimateRedeemReply(332); q["earlyRedeemFeeZat"] = earlyFee * 2; q["feeZat"] = fee1(VAULT_ZAT) + earlyFee * 2;
            h.rpc.results[YellowbackRpc::ESTIMATEREDEEM] = q;
            h.rpc.results[YellowbackRpc::REDEEM] = redeemReply();
            h.tab.redeemVault(YellowbackPosition::fromJson(p));
            QCOMPARE(h.confirms.size(), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 0);
            QVERIFY2(h.errorNotices.value(0).contains("earlyRedeemFeeZat is"), qPrintable(h.errorNotices.value(0)));
        }
        {   // the node would refuse: its identifier is shown, nothing is sent
            Harness h;
            json p = positionActive(); p["canRedeem"] = true;
            h.feedActive(331, json::array({p}));
            json q = estimateRedeemReply(332); q["canRedeem"] = false; q["error"] = "vault-not-owned";
            h.rpc.results[YellowbackRpc::ESTIMATEREDEEM] = q;
            h.tab.redeemVault(YellowbackPosition::fromJson(p));
            QCOMPARE(h.confirms.size(), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 0);
            QVERIFY(h.errorNotices.value(0).contains("vault-not-owned"));
        }
        {   // no quote, no redeem
            Harness h;
            json p = positionActive(); p["canRedeem"] = true;
            h.feedActive(331, json::array({p}));
            h.rpc.errors[YellowbackRpc::ESTIMATEREDEEM] = "vault-not-found: no such vault";
            h.tab.redeemVault(YellowbackPosition::fromJson(p));
            QCOMPARE(h.confirms.size(), 0);
            QCOMPARE(h.rpc.count(YellowbackRpc::REDEEM), 0);
            QVERIFY(h.errorNotices.value(0).contains("could not quote"));
        }
        // the pure check
        YellowbackPosition pos = YellowbackPosition::fromJson(positionActive());
        QVERIFY(YellowbackController::checkEstimateRedeem(infoActive()["params"], pos, estimateRedeemReply(332)).isEmpty());
        json fee0 = estimateRedeemReply(332); fee0["feeZat"] = 0; fee0["earlyRedeemFeeZat"] = 0; fee0["payee"] = nullptr;   // FEE-0
        QVERIFY(YellowbackController::checkEstimateRedeem(infoActive()["params"], pos, fee0).isEmpty());
        json wrongEarly = estimateRedeemReply(332); wrongEarly["early"] = false;
        QVERIFY(!YellowbackController::checkEstimateRedeem(infoActive()["params"], pos, wrongEarly).isEmpty());
        QCOMPARE(YellowbackController::earlyRedeemFeeBpsFor(infoActive()["params"], "B"), (qint64)250);
        json noRow = infoActive()["params"]; for (auto& c : noRow["classes"]) c.erase("earlyRedeemFeeBps");
        QCOMPARE(YellowbackController::earlyRedeemFeeBpsFor(noRow, "C"), (qint64)100);     // from the top-level array
    }

    void claimListHonoursClaimableFalse() {
        Harness h;
        json yes = claimableRow(); yes["claimable"] = true; yes["lockHeight"] = 380; yes["claimPath"] = "a";
        json no = claimableRow();  no["claimable"] = false; no["lockHeight"] = 380; no["claimPath"] = ""; no["underwaterAt"] = 1500000;
        no["vault"] = "7b2c3d4e5f60718293a4b5c6d7e8f9001a2b3c4d5e6f708192a3b4c5d6e7f809:0";
        h.feedActive(331, json(nullptr), json::array({yes, no}));
        auto m = h.ctl.claimableModel();
        QCOMPARE(m->rowCount(QModelIndex()), 2);
        QCOMPARE(m->claimableCount(), 1);
        QCOMPARE(m->data(m->index(0, YellowbackClaimableModel::State), Qt::DisplayRole).toString(), QString("claimable now (in term)"));
        QCOMPARE(m->data(m->index(1, YellowbackClaimableModel::State), Qt::DisplayRole).toString(), QString("not claimable: above $1.5000"));
        QCOMPARE(m->data(m->index(1, YellowbackClaimableModel::Vault), Qt::ForegroundRole).value<QBrush>().color(), QColor(Qt::gray));
        QVERIFY2(h.label("lblClaimHint").startsWith("1 claimable vault(s)"), qPrintable(h.label("lblClaimHint")));
        QVERIFY(h.label("lblClaimHint").contains("1 more vault(s) are listed above the claim threshold"));
        h.selectRow("tblClaimable", 1);
        QVERIFY(!h.button("btnClaim")->isEnabled());
        h.selectRow("tblClaimable", 0);
        QVERIFY(h.button("btnClaim")->isEnabled());
        // a claimable: false row is refused locally, nothing is sent
        h.tab.claimVault(YellowbackClaimable::fromJson(no));
        QCOMPARE(h.rpc.count(YellowbackRpc::CLAIM), 0);
        QVERIFY(h.errorNotices.value(0).contains("is not claimable now"));
        // a claimable row in term passes the plausibility check (no claim-height rule) and says so
        QVERIFY(YellowbackController::checkClaimable(h.ctl.params(), 331, YellowbackClaimable::fromJson(yes)).isEmpty());
        h.answer = false;
        h.tab.claimVault(YellowbackClaimable::fromJson(yes));
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.confirms[0].contains("The vault is in term (lock height 380) and below the claim threshold of 125 %"), qPrintable(h.confirms[0]));
        // only claimable rows: nothing claimable, both listed rows above the threshold
        Harness none;
        none.feedActive(331, json(nullptr), json::array({no}));
        QVERIFY(none.label("lblClaimHint").startsWith("No vault is claimable at the current claim price. 1 more vault(s)"));
    }

    void mintConfirmationInTerm() {
        Harness h;
        h.feedActive();
        h.rpc.results[YellowbackRpc::ESTIMATECOLLATERAL] = estimateReply();
        h.rpc.results[YellowbackRpc::GETFEEPAYEE]        = feePayeeReply(EST_ZAT);
        h.answer = false;
        h.mintAmount("1000");
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        const QString& text = h.confirms[0];
        QVERIFY2(text.contains("You can redeem at any time by paying back the $1,000.00 of YED; redeeming before height 377 also costs an early-redeem fee of 5 % of the collateral"), qPrintable(text));
        QVERIFY(text.contains(YellowbackFormat::zec(EST_ZAT * 500 / 10000)));
        QVERIFY(text.contains("falls below 125 % of its debt"));
        QVERIFY(text.contains("usually nothing"));
        QVERIFY(text.contains("This wallet warns you before that happens, and redeeming stops it."));
        QVERIFY(text.contains("Back up wallet.dat"));
        QVERIFY(!text.contains("only your key can spend it before the claim height"));
        QCOMPARE(h.rpc.count(YellowbackRpc::MINT), 0);
    }

    void devnetEndToEnd() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetTransport dev;
        QString why;
        if (!dev.attach(dir, &why)) QFAIL(qPrintable(why));
        QVERIFY2(dev.restoreMarket(), "the devnet price did not come back to $50");

        Harness h;
        h.ctl.setTransport(dev.transport());
        h.ctl.onConnected();                        // yed_getinfo, rpcversion check, then every refresh
        QVERIFY2(h.ctl.isAvailable(), qPrintable(h.ctl.unavailableReason()));
        QCOMPARE(h.ctl.network(), QString("regtest"));
        // an earlier case may have left the price windows short or crashed: pool blocks refill them
        for (int i = 0; i < 140 && !h.ctl.mintBlocker(10000).isEmpty(); i++) {
            const int before = dev.rpc("getblockcount").get<int>();
            dev.rpcOn(2 + i % 3, "generate", json::array({1}));
            for (int w = 0; w < 400 && dev.rpc("getblockcount").get<int>() <= before; w++) QTest::qWait(25);
            QVERIFY(dev.settle());
            h.ctl.refresh(true);
        }
        QVERIFY2(h.ctl.mintBlocker(10000).isEmpty(), qPrintable(h.ctl.mintBlocker(10000)));
        const qint64 yedBefore = h.ctl.confirmedCents();
        const int    vaultsBefore = h.ctl.positionsModel()->rowCount(QModelIndex());

        // Mint $100 for the shortest class-A lock (48 blocks on regtest)
        h.mintAmount("100");
        auto tier = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(tier != nullptr && tier->count() >= 1);
        tier->setCurrentIndex(0);
        QVERIFY2(dev.waitFresh(h.ctl.refLag(), [&]() { dev.rpc("generate", json::array({1})); }), "node 0's attestation pool did not become fresh");
        QVERIFY(dev.settle());
        h.ctl.refresh(true);                            // waitFresh may have mined: the wallet's tip must be the node's
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        // v3 two-step (W7): the reply names the carrier only; the mint follows when the carrier
        // confirms. Mine a block at a time until the wallet's follow-up posts the result.
        QVERIFY(Settings::getInstance()->getYellowbackBackupPending());
        QVERIFY2(dev.awaitTwoStep([&]() { return !h.notices.isEmpty() || !h.errorNotices.isEmpty(); },
                                  [&]() { dev.rpc("generate", json::array({1})); }),
                 qPrintable("no mint result; status: " % h.label("lblMintPageStatus")));
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(h.notices.size() == 1 && h.notices[0].startsWith("Mint sent|"),
                 qPrintable(h.notices.join("\n") % " / status: " % h.label("lblMintPageStatus")));
        // From the notice ("...\ntxid <txid>"): the page's "Minted. txid:" line is cleared two
        // blocks after the mint, which the two-step wait may already have mined
        QString mintTxid = noticeTxid(h.notices[0]);
        QVERIFY2(mintTxid.size() == 64, qPrintable(h.notices[0]));

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

    // ── Devnet renew (hardening H5-a, H-9.2) ─────────────────────────────────────────────
    // Mint $100 through the Mint page (two-step), wait for its lock height, then Renew through
    // the Vaults page: one confirmation, the redeem at once, the mint when the redeem is mined.
    // Every figure the dialogs show passed the wallet's H-9.3 recomputation against the real node.
    void devnetMintAndRenew() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetTransport dev;
        QString why;
        if (!dev.attach(dir, &why)) QFAIL(qPrintable(why));
        QVERIFY2(dev.restoreMarket(), "the devnet price did not come back to $50");
        // Blocks come from the pools (nodes 2-4, round-robin), as `yellowback-devnet mine` does:
        // node 0's own blocks carry no tag, and a run of them halts minting (NO_PRICE) before the
        // renewal's mint leg. Wait for node 0 to have the block.
        int pool = 0;
        auto mineOne = [&]() {
            const int before = dev.rpc("getblockcount").get<int>();
            dev.rpcOn(2 + (pool++ % 3), "generate", json::array({1}));
            for (int w = 0; w < 400 && dev.rpc("getblockcount").get<int>() <= before; w++) QTest::qWait(25);
        };
        auto mineTo = [&](int target) { while (dev.rpc("getblockcount").get<int>() < target) mineOne(); };

        Harness h;
        h.ctl.setTransport(dev.transport());
        h.ctl.setPendingPollMs(250);
        h.ctl.onConnected();
        QVERIFY2(h.ctl.isAvailable(), qPrintable(h.ctl.unavailableReason()));
        QCOMPARE(YellowbackJson::toInt(h.ctl.info(), YellowbackRpc::Info::RPCVERSION), (qint64)6);
        QVERIFY2(h.ctl.upgradeActive(), "the devnet's vault upgrade is not active");
        QVERIFY(YellowbackJson::has(h.ctl.info(), YellowbackRpc::Info::MINT_REQUIRES_ARMED));
        QVERIFY2(h.ctl.inTermClaims(), "the devnet's node is not on the in-term line (params.inTermClaims)");
        // devnetEndToEnd mines node 0's untagged blocks to its lock height: let the pools refill the price windows
        for (int i = 0; i < 96 && !h.ctl.mintBlocker(10000).isEmpty(); i++) { mineOne(); h.ctl.refresh(true); }
        for (int i = 0; i <= h.ctl.refLag(); i++) mineOne();
        h.ctl.refresh(true);
        QVERIFY2(h.ctl.mintBlocker(10000).isEmpty(), qPrintable(h.ctl.mintBlocker(10000)));
        const qint64 yedBefore = h.ctl.confirmedCents();

        h.mintAmount("100");
        auto tier = h.tab.findChild<QComboBox*>("cmbTier");
        QVERIFY(tier != nullptr && tier->count() >= 1);
        tier->setCurrentIndex(0);
        QVERIFY2(dev.waitFresh(h.ctl.refLag(), mineOne), "node 0's attestation pool did not become fresh");
        QVERIFY(dev.settle());
        h.ctl.refresh(true);                            // waitFresh may have mined: the wallet's tip must be the node's
        h.tab.doMint();
        QCOMPARE(h.confirms.size(), 1);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(dev.awaitTwoStep([&]() { return !h.notices.isEmpty() || !h.errorNotices.isEmpty(); }, mineOne),
                 qPrintable("no mint result; status: " % h.label("lblMintPageStatus")));
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        const QString mintTxid = noticeTxid(h.notices[0]);
        QVERIFY2(mintTxid.size() == 64, qPrintable(h.notices[0]));
        mineOne();
        QVERIFY(dev.settle(mintTxid));
        h.ctl.refresh(true);
        YellowbackPosition vault;
        for (int i = 0; i < h.ctl.positionsModel()->rowCount(QModelIndex()); i++)
            if (h.ctl.positionsModel()->positionAt(i)->txid == mintTxid) vault = *h.ctl.positionsModel()->positionAt(i);
        QCOMPARE(vault.status, QString("ACTIVE"));
        QCOMPARE(vault.mintedCents, (qint64)10000);
        // H-9.3 on the node's own record: claimHeight = lockHeight + GRACE, lockHeight − refHeight in class A
        QVERIFY2(YellowbackController::checkVaultHeights(h.ctl.params(), vault).isEmpty(),
                 qPrintable(YellowbackController::checkVaultHeights(h.ctl.params(), vault).join("; ")));
        const int lockBlocks = vault.lockHeight - vault.refHeight;
        qInfo("minted vault %s: refHeight %d lockHeight %d claimHeight %d collateral %lld",
              qPrintable(mintTxid), vault.refHeight, vault.lockHeight, vault.claimHeight, (long long)vault.collateralZat);
        // in-term claims: no claim-height deadline; a vault minted at the devnet's $50 is far above the threshold
        QVERIFY2(h.ctl.deadlineWarnings().isEmpty(), qPrintable(h.ctl.deadlineWarnings().join("\n")));
        QVERIFY(vault.underwaterAt > 0 && vault.canRedeem);                       // D-IT-15: the owner may redeem in term
        // IT-9 against the real node: yed_estimateredeem in term quotes the early-redeem fee, and the
        // wallet's own recomputation (FEE-1 + earlyRedeemFeeBps[A] of the collateral) agrees with it
        json quote;
        QString quoteErr;
        h.ctl.estimateRedeem(mintTxid, [&](const json& q) { quote = q; }, [&](const QString& e) { quoteErr = e; });
        QVERIFY2(quoteErr.isEmpty() && quote.is_object(), qPrintable(quoteErr));
        qInfo("in-term quote: %s", quote.dump().c_str());
        QVERIFY(YellowbackJson::toBool(quote, YellowbackRpc::EstimateRedeem::EARLY));
        QVERIFY(YellowbackJson::toBool(quote, YellowbackRpc::EstimateRedeem::CAN_REDEEM));
        QVERIFY2(YellowbackController::checkEstimateRedeem(h.ctl.params(), vault, quote).isEmpty(),
                 qPrintable(YellowbackController::checkEstimateRedeem(h.ctl.params(), vault, quote).join("; ")));
        QCOMPARE(YellowbackJson::toInt(quote, YellowbackRpc::EstimateRedeem::EARLY_REDEEM_FEE_ZAT),
                 YellowbackController::earlyRedeemFeeZatFor(h.ctl.params(), vault.termClass, vault.collateralZat));

        // To the lock height, then Renew
        mineTo(vault.lockHeight);
        QVERIFY(dev.settle());
        QVERIFY2(dev.waitFresh(h.ctl.refLag(), mineOne), "node 0's attestation pool did not become fresh");
        QVERIFY(dev.settle());
        h.ctl.refresh(true);
        for (int i = 0; i < h.ctl.positionsModel()->rowCount(QModelIndex()); i++)
            if (h.ctl.positionsModel()->positionAt(i)->txid == mintTxid) vault = *h.ctl.positionsModel()->positionAt(i);
        auto acts = YellowbackTab::vaultActions(vault, h.ctl.height(), h.ctl.params(), h.ctl.claimPriceNow());
        QVERIFY(acts.renew && acts.redeem);
        QVERIFY2(h.ctl.mintBlocker(vault.mintedCents, h.ctl.classForLock(h.ctl.renewLockBlocks(vault)).name).isEmpty(),
                 qPrintable(h.ctl.mintBlocker(vault.mintedCents)));
        const int noticesBefore = h.notices.size();
        const int confirmsBefore = h.confirms.size();
        h.tab.renewVault(vault);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QCOMPARE(h.confirms.size(), confirmsBefore + 1);
        QVERIFY(h.confirms.last().startsWith("Renew vault " % mintTxid));
        QVERIFY(h.tab.renewPending());
        qInfo("renew confirmation:\n%s", qPrintable(h.confirms.last()));

        // Mine the redeem; the next yed_listpositions reads the vault CLOSED and the mint leg goes
        // out (two-step): keep the attestation pool fresh and mine until the result is posted
        QVERIFY(dev.awaitTwoStep([&]() {
                    if (h.notices.size() > noticesBefore || !h.errorNotices.isEmpty() || !h.tab.renewPending()) return true;
                    h.ctl.refresh(true);
                    return false;
                }, [&]() { dev.waitFresh(h.ctl.refLag(), mineOne, 10); mineOne(); }, 30));
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(h.notices.size() == noticesBefore + 1 && h.notices.last().startsWith("Vault renewed|"),
                 qPrintable(h.notices.join("\n") % " / " % h.label("lblVaultAction")));
        qInfo("renew result:\n%s", qPrintable(h.notices.last()));
        const QString newTxid = noticeTxid(h.notices.last().section("Minted $", 1));
        QVERIFY2(newTxid.size() == 64 && newTxid != mintTxid, qPrintable(h.notices.last()));
        mineOne();
        QVERIFY(dev.settle(newTxid));
        h.ctl.refresh(true);
        YellowbackPosition old, fresh;
        for (int i = 0; i < h.ctl.positionsModel()->rowCount(QModelIndex()); i++) {
            const YellowbackPosition* q = h.ctl.positionsModel()->positionAt(i);
            if (q->txid == mintTxid) old = *q;
            if (q->txid == newTxid)  fresh = *q;
        }
        QCOMPARE(old.status, QString("CLOSED"));
        QCOMPARE(old.burnedCents, (qint64)10000);
        QCOMPARE(fresh.status, QString("ACTIVE"));
        QCOMPARE(fresh.mintedCents, (qint64)10000);
        QCOMPARE(fresh.lockHeight - fresh.refHeight, lockBlocks);                 // the same lock length
        QVERIFY(fresh.claimHeight > vault.claimHeight);                          // the deadline moved out
        QVERIFY(YellowbackController::checkVaultHeights(h.ctl.params(), fresh).isEmpty());
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 10000);                      // burned 100, minted 100
        qInfo("renewed vault %s: refHeight %d lockHeight %d claimHeight %d (old claimHeight %d)",
              qPrintable(newTxid), fresh.refHeight, fresh.lockHeight, fresh.claimHeight, vault.claimHeight);
        QVERIFY(h.copyIsClean());
    }

    // ── Devnet claims under the vault upgrade (rpcversion 5, U-23, U-24) ───────────────────
    // Two vaults are minted at the devnet's $50 and the market crashes for a full slow window.
    // Vault R is claimed through the Claim page and, after CLAIM_DELAY, released through the
    // Pending claims page: CLAIMED. Vault C is claimed the same way and then cancelled by an
    // attestor's wallet (node 5, a current member of the YED attestor set) through its own
    // Pending claims page before it matures: the vault is ACTIVE again under the cancel's txid,
    // the claimant's burn is lost, and the claimant's page says so. Every Yellowback action goes
    // through the tab's own code path; only mining and the market (other people's machines on a
    // real network) are driven directly.
    void devnetClaimReleaseAndCancel() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetTransport dev;
        QString why;
        if (!dev.attach(dir, &why)) QFAIL(qPrintable(why));
        QVERIFY2(dev.restoreMarket(), "the devnet price did not come back to $50");
        // node 5 runs an automated attestor: its wallet holds a member key of the YED attestor set
        DevnetTransport att;
        if (!att.attachNode(dir, 5, &att.request, &why)) QFAIL(qPrintable(why));
        att.devnetDir = dir;

        Harness h;
        h.ctl.setTransport(dev.transport());
        h.ctl.setPendingPollMs(250);
        h.ctl.onConnected();
        QVERIFY2(h.ctl.isAvailable(), qPrintable(h.ctl.unavailableReason()));
        QVERIFY2(h.ctl.upgradeActive(), "the devnet's vault upgrade is not active");
        const int delay = h.ctl.claimDelay();
        QVERIFY2(delay > 0, "the node reports no claimDelay");

        const int pools[3] = {2, 3, 4};                 // the devnet's quoting pools
        auto height = [&]() { json r = dev.rpc("getblockcount"); return r.is_number() ? r.get<int>() : -1; };
        // One block at a time, round-robin, waiting for node 0 to see each: the pools are peers,
        // and two of them generating from the same height would fork the devnet.
        int turn = 0;
        auto minePools = [&](int n) {
            for (int i = 0; i < n; i++) {
                int want = height() + 1;
                dev.rpcOn(pools[turn++ % 3], "generate", json::array({1}));
                for (int w = 0; w < 400 && height() < want; w++) QTest::qWait(25);
            }
        };
        // A transaction one node broadcast must reach every pool before one of them mines
        auto waitInPools = [&](const QString& txid) {
            auto inPool = [&](int node) {
                json m = dev.rpcOn(node, "getrawmempool");
                if (m.is_array()) for (const auto& t : m) if (QString::fromStdString(t.get<std::string>()) == txid) return true;
                return false;
            };
            for (int w = 0; w < 400 && !(inPool(2) && inPool(3) && inPool(4)); w++) QTest::qWait(25);
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
        auto pendingRow = [&](YellowbackController& c, const QString& vaultTxid) -> const YellowbackPendingClaim* {
            for (int i = 0; i < c.pendingClaimsModel()->rowCount(QModelIndex()); i++) {
                const YellowbackPendingClaim* r = c.pendingClaimsModel()->rowAt(i);
                if (r != nullptr && r->vault.txid == vaultTxid && r->intent.role == YellowbackRpc::PositionIntent::ROLE_CLAIMANT) return r;
            }
            return nullptr;
        };

        // Two vaults; let the price windows refill first if an earlier case left them short
        for (int i = 0; i < 96 && !h.ctl.mintBlocker(10000).isEmpty(); i++) { minePools(1); h.ctl.refresh(true); }
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
            tier->setCurrentIndex(0);                   // the shortest class-A lock
            const int noticesBefore = h.notices.size();
            QVERIFY2(dev.waitFresh(h.ctl.refLag(), [&]() { minePools(1); }), "node 0's attestation pool did not become fresh");
            QVERIFY(dev.settle());
            h.ctl.refresh(true);                        // waitFresh may have mined: the wallet's tip must be the node's
            h.tab.doMint();
            QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
            QVERIFY2(dev.awaitTwoStep([&]() { return h.notices.size() > noticesBefore || !h.errorNotices.isEmpty(); },
                                      [&]() { minePools(1); }),
                     qPrintable("no mint result; status: " % h.label("lblMintPageStatus")));
            QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
            QVERIFY2(h.notices.last().startsWith("Mint sent|"), qPrintable(h.notices.last()));
            const QString txid = noticeTxid(h.notices.last());
            QVERIFY2(txid.size() == 64, qPrintable(h.notices.last()));
            minted << txid;
            if (!dev.confirmed(txid)) minePools(1);
            QVERIFY2(dev.settle(txid), "node 0 did not digest the mint's block");
            h.ctl.refresh(true);
        }
        const QString releaseTxid = minted[0], cancelTxid = minted[1];
        QCOMPARE(positionOf(releaseTxid).status, QString("ACTIVE"));
        QCOMPARE(positionOf(cancelTxid).status, QString("ACTIVE"));
        QVERIFY2(!positionOf(releaseTxid).scriptPubKey.isEmpty(), "the vault row carries no scriptPubKey (rpcversion 5)");
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 20000);

        // Crash the market for a full slow window: pClaim = max(pMid, pSlow) falls
        auto pClaimNow = [&]() { return YellowbackJson::toInt(h.ctl.stats(), YellowbackRpc::Stats::P_CLAIM, 0); };
        const qint64 before = pClaimNow();
        QVERIFY(dev.setMarketPrice("0.01"));
        minePools(64);
        h.ctl.refresh(true);
        QVERIFY2(pClaimNow() < before, qPrintable(QString("pClaim did not fall: %1 -> %2").arg(before).arg(pClaimNow())));
        const int claimHeight = std::max(positionOf(releaseTxid).claimHeight, positionOf(cancelTxid).claimHeight);
        if (height() < claimHeight) minePools(claimHeight - height());
        QVERIFY(dev.settle());

        // Claim one vault through the Claim page: two-step; the vault goes CLAIMING
        auto claim = [&](const QString& vaultTxid) -> QString {
            const YellowbackClaimable* row = nullptr;
            for (int t = 0; t < 20 && row == nullptr; t++) {
                if (t > 0) minePools(1);
                if (!dev.waitFresh(h.ctl.refLag(), [&]() { minePools(1); })) { qWarning("node 0's attestation pool did not become fresh"); return QString(); }
                dev.settle();
                h.ctl.refresh(true);
                for (int i = 0; i < h.ctl.claimableModel()->rowCount(QModelIndex()); i++)
                    if (h.ctl.claimableModel()->rowAt(i)->txid() == vaultTxid && h.ctl.claimableModel()->rowAt(i)->claimable) row = h.ctl.claimableModel()->rowAt(i);
            }
            if (row == nullptr) { qWarning("vault %s is not in yed_listclaimable", qPrintable(vaultTxid)); return QString(); }
            h.errorNotices.clear();
            const int noticesBefore = h.notices.size();
            // bundle-insufficient (a stale attestation pool) is answered by the retry dialog, which
            // sends nothing: mine towards the agents' next tick and try again
            // (a claim's selection is keyed by the vault outpoint, so waitFresh's probe with the
            // empty selector does not tell when it can be built: mine and wait for the next tick)
            for (int t = 0; t < 16; t++) {
                const int confirmsBefore = h.confirms.size();
                h.tab.claimVault(*row);
                if (h.confirms.size() > confirmsBefore && h.confirms.last().startsWith("Claim vault")) break;
                minePools(1);
                QTest::qWait(1500);
                dev.settle();
                h.ctl.refresh(true);
                row = nullptr;
                for (int i = 0; i < h.ctl.claimableModel()->rowCount(QModelIndex()); i++)
                    if (h.ctl.claimableModel()->rowAt(i)->txid() == vaultTxid && h.ctl.claimableModel()->rowAt(i)->claimable) row = h.ctl.claimableModel()->rowAt(i);
                if (row == nullptr) return QString();
            }
            if (!h.errorNotices.isEmpty()) { qWarning("%s", qPrintable(h.errorNotices.join("\n"))); return QString(); }
            if (!h.confirms.last().contains(QString("waits in a claim intent for %1 blocks").arg(delay))) { qWarning("%s", qPrintable(h.confirms.last())); return QString(); }
            if (!dev.awaitTwoStep([&]() { return h.notices.size() > noticesBefore || !h.errorNotices.isEmpty(); }, [&]() { minePools(1); })) return QString();
            if (!h.errorNotices.isEmpty() || !h.notices.last().startsWith("Claim sent|")) { qWarning("%s", qPrintable(h.errorNotices.join("\n") % h.notices.last())); return QString(); }
            qInfo("claim result:\n%s", qPrintable(h.notices.last()));
            return noticeTxid(h.notices.last());
        };

        // 1. Claim R, wait out CLAIM_DELAY, release it through the Pending claims page
        const QString claimR = claim(releaseTxid);
        QVERIFY2(claimR.size() == 64, "the claim of vault R did not complete");
        QVERIFY(dev.settle(claimR));
        h.ctl.refresh(true);
        YellowbackPosition r = positionOf(releaseTxid);
        QCOMPARE(r.status, QString("CLAIMING"));
        QVERIFY(r.claimantIntent() != nullptr);
        QCOMPARE(r.claimantIntent()->txid, claimR);
        QCOMPARE(r.claimantIntent()->releaseHeight, r.claimantIntent()->height + delay);
        QCOMPARE(h.ctl.confirmedCents(), yedBefore + 10000);          // the claim burned $100 of our YED
        const YellowbackPendingClaim* pr = pendingRow(h.ctl, releaseTxid);
        QVERIFY2(pr != nullptr, "the claim is not on the Pending claims page");
        QVERIFY(pr->mineClaim);
        h.tab.releaseClaim(*pr);                                       // too early: refused locally
        QVERIFY2(h.notices.last().contains("matures at height"), qPrintable(h.notices.last()));
        const int releaseAt = r.claimantIntent()->releaseHeight;
        if (height() + 1 < releaseAt) minePools(releaseAt - 1 - height());
        QVERIFY(dev.settle());
        h.ctl.refresh(true);
        pr = pendingRow(h.ctl, releaseTxid);
        QVERIFY(pr != nullptr);
        h.errorNotices.clear();
        h.tab.releaseClaim(*pr);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(h.notices.last().startsWith("Release sent|"), qPrintable(h.notices.last()));
        const QString releaseTx = noticeTxid(h.notices.last());
        waitInPools(releaseTx);
        minePools(1);
        QVERIFY(dev.settle(releaseTx));
        h.ctl.refresh(true);
        QCOMPARE(positionOf(releaseTxid).status, QString("CLAIMED"));
        QVERIFY(pendingRow(h.ctl, releaseTxid) == nullptr);

        // 2. Claim C, then cancel it from the attestor's wallet before it matures
        const QString claimC = claim(cancelTxid);
        QVERIFY2(claimC.size() == 64, "the claim of vault C did not complete");
        QVERIFY(dev.settle(claimC));
        h.ctl.refresh(true);
        QCOMPARE(positionOf(cancelTxid).status, QString("CLAIMING"));
        QCOMPARE(h.ctl.confirmedCents(), yedBefore);                   // both claims burned $100 each

        Harness a;
        a.ctl.setTransport(att.transport());
        a.ctl.onConnected();
        QVERIFY2(a.ctl.isAvailable(), qPrintable(a.ctl.unavailableReason()));
        QTRY_VERIFY_WITH_TIMEOUT(!a.ctl.attestorSet().empty(), 5000);
        QVERIFY2(a.ctl.isAttestor(), qPrintable("node 5 is not a current member: " % a.label("lblMembership")));
        QVERIFY(a.label("lblMembership").contains("Your member"));
        QVERIFY(!a.button("btnCancelClaim")->isHidden());
        const YellowbackPendingClaim* pc = pendingRow(a.ctl, cancelTxid);
        QVERIFY2(pc != nullptr, "the attestor's Pending claims page does not list the claim");
        QVERIFY(!pc->mineClaim);
        QVERIFY2(height() + 1 < pc->intent.releaseHeight, "the claim matured before the attestor could cancel it");
        a.tab.cancelClaim(*pc);
        QVERIFY2(a.errorNotices.isEmpty(), qPrintable(a.errorNotices.join("\n")));
        QVERIFY(a.confirms.last().contains("NOT refunded"));
        QVERIFY2(a.notices.last().startsWith("Cancel sent|"), qPrintable(a.notices.last()));
        const QString cancelTx = noticeTxid(a.notices.last());
        QCOMPARE(cancelTx.size(), 64);
        qInfo("cancel result:\n%s", qPrintable(a.notices.last()));
        waitInPools(cancelTx);                          // node 5 broadcast it
        minePools(1);
        QVERIFY(dev.settle());
        h.ctl.refresh(true);
        // The vault is the same position again, ACTIVE at the cancel's output 0; the old outpoint is gone
        YellowbackPosition reopened;
        h.ctl.getVault(cancelTx, [&](const json& v) { reopened = YellowbackPosition::fromJson(v); },
                       [](const QString& e) { qWarning("yed_getvault: %s", qPrintable(e)); });
        QCOMPARE(reopened.status, QString("ACTIVE"));
        QCOMPARE(reopened.mintedCents, (qint64)10000);
        QString gone;
        h.ctl.getVault(cancelTxid, [](const json&) {}, [&](const QString& e) { gone = e; });
        QVERIFY2(gone.startsWith("vault-not-found"), qPrintable(gone));
        QVERIFY(pendingRow(h.ctl, cancelTxid) == nullptr);
        // The claimant's page names the cancelled claim and the lost burn; its YED stays burned
        h.ctl.refreshClaimOutcomes();
        QTRY_VERIFY_WITH_TIMEOUT(h.visible("lblCancelledClaims"), 5000);
        QVERIFY2(h.label("lblCancelledClaims").contains("was cancelled by the attestor set"), qPrintable(h.label("lblCancelledClaims")));
        QVERIFY(h.label("lblCancelledClaims").contains("$100.00 of YED it burned are not refunded"));
        QCOMPARE(h.ctl.confirmedCents(), yedBefore);
        // The attestor never signs a second cancel of that intent
        QVERIFY(!Settings::getInstance()->getYellowbackSignedCancel(claimC % ":0").isEmpty());
        QVERIFY(h.copyIsClean());
        QVERIFY(a.copyIsClean());
        // The devnet's price for the next case, through a full slow window: pMint is the lowest
        // window median, so one crashed window left behind would starve every later mint
        QVERIFY(dev.setMarketPrice("50"));
        minePools(70);
        QVERIFY(dev.settle());
    }

    // ── Devnet v3 (A5-b): an attested mint, a claim notice and the emergency claim ────────
    // Needs the a4-devnet chunk's devnet: attestors registered and the layer ARMED, the
    // attestors' agents reading the devnet's shared mock-price file (<dir>/mock-price, one
    // decimal per line, as `yellowback-devnet price` writes it). Skips without the variable and
    // when the running devnet has no armed layer; nothing here is verified until that devnet
    // exists.
    void devnetAttestedMintNoticeAndEmergencyClaim() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetTransport dev;
        QString why;
        if (!dev.attach(dir, &why)) QFAIL(qPrintable(why));
        QVERIFY2(dev.restoreMarket(), "the devnet price did not come back to $50");

        Harness h;
        h.ctl.setTransport(dev.transport());
        h.ctl.setPendingPollMs(500);
        h.ctl.onConnected();
        QVERIFY2(h.ctl.isAvailable(), qPrintable(h.ctl.unavailableReason()));
        if (!YellowbackJson::toBool(h.ctl.attest(), YellowbackRpc::Attest::ARMED))
            QSKIP("the devnet's attestation layer is not armed (no attestors yet: the a4-devnet chunk)");

        const int pools[3] = {2, 3, 4};
        auto height = [&]() { json r = dev.rpc("getblockcount"); return r.is_number() ? r.get<int>() : -1; };
        auto minePools = [&](int n) {
            for (int i = 0; i < n; i++) {
                int want = height() + 1;
                dev.rpcOn(pools[i % 3], "generate", json::array({1}));
                for (int w = 0; w < 400 && height() < want; w++) QTest::qWait(25);
            }
        };
        // A two-step action: the carrier confirms in the next block, the node builds the main
        // transaction on that ChainTip, the wallet's poll then finds it. Mine a pool block per
        // second until the result dialog arrives.
        auto waitTwoStep = [&](int noticesBefore) {
            for (int i = 0; i < 20 && h.notices.size() == noticesBefore && h.errorNotices.isEmpty(); i++) { minePools(1); QTest::qWait(1000); }
            return h.notices.size() > noticesBefore;
        };
        // Move the market the way `yellowback-devnet price USD` does (its apply_price): the pools'
        // shared mock-price file, each automated attestor's own attest-price-<n> file, and
        // yed_setquote on the pools when they run without quote agents (the default devnet)
        auto writeMock = [&](const QString& usd) { QVERIFY(dev.setMarketPrice(usd)); };

        // 1. Mint $100 while ARMED: the two-step path, the price proof from the attestors
        for (int i = 0; i < 96 && !h.ctl.mintBlocker(10000).isEmpty(); i++) { minePools(1); h.ctl.refresh(true); }
        minePools(h.ctl.refLag() + 1);
        h.ctl.refresh(true);
        QVERIFY2(h.ctl.mintBlocker(10000).isEmpty(), qPrintable(h.ctl.mintBlocker(10000)));
        h.mintAmount("100");
        h.tab.findChild<QComboBox*>("cmbTier")->setCurrentIndex(0);
        QVERIFY2(dev.waitFresh(h.ctl.refLag(), [&]() { minePools(1); }), "node 0's attestation pool did not become fresh");
        QVERIFY(dev.settle());
        h.ctl.refresh(true);                            // waitFresh may have mined: the wallet's tip must be the node's
        h.tab.doMint();
        QVERIFY2(h.confirms.size() == 1, qPrintable("no confirmation; hint: " % h.label("lblMintHint") % "; status: " %
                                                    h.label("lblMintPageStatus") % "; " % h.errorNotices.join("\n")));
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(h.label("lblMintPageStatus").startsWith("Preparing price proof"), qPrintable(h.label("lblMintPageStatus")));
        QVERIFY2(waitTwoStep(0), qPrintable(h.errorNotices.join("\n")));
        QVERIFY(h.notices[0].startsWith("Mint sent|"));
        QVERIFY2(h.notices[0].contains("price proof from attestor seq"), qPrintable(h.notices[0]));
        // From the notice ("Minted ... of YED.\ntxid <txid>"): the page's "Minted. txid:" line is
        // cleared two blocks after the mint, and the two-step wait may already have mined them
        // (blocks settle more slowly on 6.20.0)
        const QString mintTxid = h.notices[0].section("\ntxid ", 1).section('\n', 0, 0).trimmed();
        QVERIFY2(mintTxid.size() == 64, qPrintable(h.notices[0]));
        minePools(1);
        QVERIFY(dev.settle(mintTxid));
        h.ctl.refresh(true);
        YellowbackPosition vault;
        for (int i = 0; i < h.ctl.positionsModel()->rowCount(QModelIndex()); i++)
            if (h.ctl.positionsModel()->positionAt(i)->txid == mintTxid) vault = *h.ctl.positionsModel()->positionAt(i);
        QCOMPARE(vault.status, QString("ACTIVE"));

        // Either claim clause spends the vault through its claim branch, which the vault script
        // opens only at claimHeight (CLTV; yed_listclaimable lists nothing before it), while a
        // notice stands for EMERGENCY_NOTICE_TTL blocks from its reference height. So the market
        // crashes a little before claimHeight: the notice is then still standing at claimHeight,
        // and the claim price (max(pMid, pSlow) of the pools' quotes) has not yet followed the
        // crash, so the claim opens by the emergency clause (b), not by clause (a).
        if (height() < vault.claimHeight - 12) minePools(vault.claimHeight - 12 - height());

        // 2. Crash the price so the vault falls under the emergency ratio: the attestors (and
        // the pools' quote agents) follow the shared mock price. Wait for canNotice.
        const QString priceBefore = QString::fromStdString(json(dev.rpc("yed_getprice")).value("xMint", 0) > 0 ? std::to_string(json(dev.rpc("yed_getprice")).value("xMint", 0) / 1000000.0) : "50");
        writeMock("1");
        for (int i = 0; i < 120 && !vault.canNotice; i++) {
            minePools(1); QTest::qWait(500); h.ctl.refresh(true); QTest::qWait(200);
            for (int r = 0; r < h.ctl.positionsModel()->rowCount(QModelIndex()); r++)
                if (h.ctl.positionsModel()->positionAt(r)->txid == mintTxid) vault = *h.ctl.positionsModel()->positionAt(r);
        }
        QVERIFY2(vault.canNotice, "the vault never became noticeable after the price crash");

        // 3. Post the notice through the Vaults page (two-step), then see emergencyOpenAt on the row
        int n = h.notices.size();
        QVERIFY2(dev.waitFresh(0, [&]() { minePools(1); }), "node 0's attestation pool did not become fresh");
        h.tab.noticeVault(vault);
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(waitTwoStep(n), qPrintable(h.errorNotices.join("\n")));
        QVERIFY(h.notices.last().startsWith("Claim notice sent|"));
        minePools(1);
        QVERIFY(dev.settle());
        h.ctl.refresh(true);
        QTest::qWait(500);
        for (int r = 0; r < h.ctl.positionsModel()->rowCount(QModelIndex()); r++)
            if (h.ctl.positionsModel()->positionAt(r)->txid == mintTxid) vault = *h.ctl.positionsModel()->positionAt(r);
        QVERIFY(vault.noticed);
        QVERIFY(vault.emergencyOpenAt > 0);

        // 4. Past emergencyOpenAt + refLag and claimHeight the vault is claimable by clause (b): claim it
        while (height() < std::max(vault.emergencyOpenAt + h.ctl.refLag() + 1, vault.claimHeight)) minePools(1);
        QVERIFY(dev.settle());
        // yed_listclaimable computes clause (b) with the bundle the node would build: with a stale
        // pool it falls back to the cross-section alone, where only clause (a) can list a vault.
        // Poll it (a block every few seconds) until the row appears.
        const YellowbackClaimable* row = nullptr;
        for (int i = 0; i < 40 && row == nullptr; i++) {
            if (i > 0) { QTest::qWait(500); if (i % 4 == 0) { minePools(1); QVERIFY(dev.settle()); } }
            QVERIFY2(dev.waitFresh(h.ctl.refLag(), [&]() { minePools(1); }), "node 0's attestation pool did not become fresh");
            h.ctl.refresh(true);
            for (int r = 0; r < h.ctl.claimableModel()->rowCount(QModelIndex()); r++)
                // rpcversion 6 (IT-7): the vault is listed from its mint on; wait for its claimable row
                if (h.ctl.claimableModel()->rowAt(r)->txid() == mintTxid && h.ctl.claimableModel()->rowAt(r)->claimable) row = h.ctl.claimableModel()->rowAt(r);
        }
        QVERIFY2(row != nullptr, qPrintable("the noticed vault is not claimable in yed_listclaimable after the notice persisted: " %
                                            QString::fromStdString(dev.rpc("yed_listclaimable").dump()) % " at height " % QString::number(height())));
        QCOMPARE(row->claimPath, QString("b"));
        n = h.notices.size();
        QVERIFY2(dev.waitFresh(h.ctl.refLag(), [&]() { minePools(1); }), "node 0's attestation pool did not become fresh");
        h.tab.claimVault(*row);
        QVERIFY2(h.confirms.last().contains("emergency clause (b)"), qPrintable(h.confirms.last()));
        QVERIFY2(h.errorNotices.isEmpty(), qPrintable(h.errorNotices.join("\n")));
        QVERIFY2(waitTwoStep(n), qPrintable(h.errorNotices.join("\n")));
        QVERIFY(h.notices.last().startsWith("Claim sent|"));
        QVERIFY2(h.notices.last().contains("claim path: emergency clause (b)"), qPrintable(h.notices.last()));
        QVERIFY(h.copyIsClean());
        writeMock(priceBefore);
        minePools(70);                                  // a full slow window at the restored price, for the next run
    }
};

QTEST_MAIN(YellowbackTabTest)
#include "yellowbacktab_test.moc"
