// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

#include "yellowbackcontroller.h"

#include <algorithm>
#include "yellowbackrpc.h"
#include "vaultrpc.h"
#include "controller.h"
#include "connection.h"
#include "mainwindow.h"
#include "settings.h"

using json = nlohmann::json;

static json rpcPayload(const char* method, const json& params) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "yellowback"},
        {"method", method}
    };
    if (!params.is_null())
        payload["params"] = params;
    return payload;
}

YellowbackController::YellowbackController(MainWindow* main, Controller* rpc) : QObject(nullptr) {
    this->main = main;
    this->rpc  = rpc;

    positions    = new YellowbackPositionsModel(this);
    claimable    = new YellowbackClaimableModel(this);
    transactions = new YellowbackTxModel(this);
    attestors    = new YellowbackAttestorsModel(this);
    pendingClaims = new YellowbackPendingClaimsModel(this);

    reason = tr("Not connected to ycashd yet.");
}

YellowbackController::~YellowbackController() {
    // the models are children of this QObject
}

Connection* YellowbackController::connection() {
    return rpc ? rpc->getConnection() : nullptr;
}

void YellowbackController::log(const QString& line) {
    if (main != nullptr && main->logger != nullptr) main->logger->write(line);
}

// ── Generic call ──────────────────────────────────────────────────────────────────────────

void YellowbackController::call(const char* method, const json& params, OkFn ok, ErrFn err) {
    if (transport) {
        transport(QString(method), params, ok,
            [=, this](const QString& msg) {
                if (isIndexUnhealthy(msg)) {
                    healthy = false;
                    setAvailability(false, tr("the index reports it is unhealthy (%1). Fix: restart ycashd with -reindex-yellowback.").arg(msg));
                }
                if (err) err(msg);
            });
        return;
    }
    auto conn = connection();
    if (conn == nullptr) {
        if (err) err(tr("Not connected to ycashd."));
        return;
    }

    conn->doRPCSafe(rpcPayload(method, params),
        [=, this](const json& result) {
            if (ok) ok(result);
        },
        [=, this](QNetworkReply* reply, const json& parsed) {
            QString msg;
            if (!parsed.is_discarded() && parsed.is_object() &&
                parsed.find("error") != parsed.end() && parsed["error"].is_object() &&
                parsed["error"].find("message") != parsed["error"].end() && parsed["error"]["message"].is_string()) {
                msg = QString::fromStdString(parsed["error"]["message"].get<json::string_t>());
                if (parsed["error"].find("code") != parsed["error"].end() && parsed["error"]["code"].is_number_integer()) {
                    // Keep the identifier: the code is what makes "Method not found" unambiguous
                    int code = parsed["error"]["code"].get<json::number_integer_t>();
                    if (code == YellowbackRpc::RpcErrors::METHOD_NOT_FOUND_CODE && !msg.contains(YellowbackRpc::RpcErrors::METHOD_NOT_FOUND))
                        msg = QString(YellowbackRpc::RpcErrors::METHOD_NOT_FOUND) % " (" % msg % ")";
                }
            } else if (reply != nullptr) {
                msg = reply->errorString();
            } else {
                msg = tr("Unknown error");
            }
            // Every gated command answers `yellowback-unhealthy: …` while the index is
            // unhealthy; take the tab down at once instead of waiting for the next yed_getinfo.
            if (isIndexUnhealthy(msg)) {
                healthy = false;
                setAvailability(false, tr("the index reports it is unhealthy (%1). Fix: restart ycashd with -reindex-yellowback.").arg(msg));
            }
            if (err) err(msg);
        });
}

bool YellowbackController::isMethodNotFound(const QString& m) {
    return m.contains(YellowbackRpc::RpcErrors::METHOD_NOT_FOUND, Qt::CaseInsensitive);
}

bool YellowbackController::isIndexUnhealthy(const QString& m) {
    return m.startsWith(YellowbackRpc::Errors::INDEX_UNHEALTHY, Qt::CaseInsensitive);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────────────────

void YellowbackController::setAvailability(bool avail, const QString& why) {
    bool changed = (avail != available) || (why != reason);
    available = avail;
    reason    = why;
    if (changed)
        emit availabilityChanged(available, reason);
}

void YellowbackController::onConnected() {
    versionOk = false;
    enabled   = false;
    lastRefreshHeight = -1;

    call(YellowbackRpc::GETINFO, json(nullptr),
        [=, this](const json& info) {
            applyInfo(info);
            if (versionOk) refresh(true);
        },
        [=, this](const QString& e) {
            if (isMethodNotFound(e)) {
                // rpcversion 5 (U-22): the yed_* commands exist only where the vault upgrade and the
                // network's YED attestor set are both configured; no conf flag enables them
                setAvailability(false, tr("the connected ycashd has no Yellowback RPCs (%1). Yellowback runs where the vault upgrade "
                                          "and the network's YED attestor set are configured; this network has none yet "
                                          "(on regtest: -nuparams=6d5b7a31:<height> -yellowbackattestorset=<setid>).").arg(e));
            } else {
                setAvailability(false, tr("yed_getinfo failed: %1").arg(e));
            }
            emit infoUpdated();
        });
}

// The whole of the availability decision, from one yed_getinfo result. Also the entry point
// of the offline feed, so the version and enabled checks live here and not in onConnected.
void YellowbackController::applyInfo(const json& info) {
    using namespace YellowbackRpc::Info;
    infoJson = info.is_object() ? info : json::object();
    enabled  = YellowbackJson::toBool(info, ENABLED, true);
    int version = (int)YellowbackJson::toInt(info, RPCVERSION, -1);

    if (!enabled) {
        versionOk = false;
        setAvailability(false, tr("Yellowback is not enabled on this ycashd: the vault upgrade or the network's YED attestor set is not configured."));
        emit infoUpdated();
        return;
    }
    if (version != Settings::getYellowbackRpcVersion()) {
        versionOk = false;
        setAvailability(false, tr("this YecWallet understands Yellowback RPC version %1 but the node reports version %2. "
                                  "Update YecWallet (or ycashd) so the two match; the Yellowback tab is disabled until then.")
                                  .arg(Settings::getYellowbackRpcVersion()).arg(version));
        emit infoUpdated();
        return;
    }
    versionOk = true;

    net              = YellowbackJson::toStr (info, NETWORK, net);
    indexHeight      = (int)YellowbackJson::toInt(info, HEIGHT, indexHeight);
    tipHeight        = (int)YellowbackJson::toInt(info, CHAIN_HEIGHT, tipHeight);
    indexStartHeight = (int)YellowbackJson::toInt(info, START_HEIGHT, indexStartHeight);
    // v2 has no `synced` field: the index moves inside ConnectBlock (V2), so its tip is the
    // chain tip whenever the node is not in initial block download or a reindex.
    synced           = indexHeight >= 0 && indexHeight == tipHeight;
    healthy          = YellowbackJson::toBool(info, HEALTHY, false);
    if (info.is_object() && info.find(PARAMS) != info.end() && info[PARAMS].is_object())
        paramsJson = info[PARAMS];

    if (!healthy) {
        QString why = YellowbackJson::toStr(info, UNHEALTHY_REASON, tr("unknown reason"));
        setAvailability(false, tr("the index reports it is unhealthy (%1). "
                                  "Fix: restart ycashd with -reindex-yellowback.").arg(why));
    } else if (!synced) {
        setAvailability(false, tr("the Yellowback index is at height %1 while the chain is at %2 (initial block download or reindex). "
                                  "Actions are disabled until they match.").arg(indexHeight).arg(tipHeight));
    } else {
        setAvailability(true, QString());
    }
    emit infoUpdated();
}

void YellowbackController::refresh(bool force) {
    if (!versionOk || !enabled) return;

    call(YellowbackRpc::GETINFO, json(nullptr),
        [=, this](const json& info) {
            applyInfo(info);
            if (force || indexHeight != lastRefreshHeight) {
                lastRefreshHeight = indexHeight;
                refreshStats();
                refreshActivation();
                refreshBalance();
                refreshPositions();
                refreshClaimable();
                refreshTransactions();
                refreshPrice();
                refreshAttestors();
                refreshSelection();
                refreshPendingClaims();
                refreshAttestorSet();
                refreshVaultInfo();
                refreshClaimOutcomes();
            }
        },
        [=, this](const QString& e) {
            setAvailability(false, tr("yed_getinfo failed: %1").arg(e));
            emit infoUpdated();
        });
}

void YellowbackController::feed(const json& info, const json& stats, const json& activation,
                                const json& balance, const json& positionsArr, const json& claimableArr,
                                const json& transactionsArr) {
    if (!info.is_null())            applyInfo(info);
    if (!stats.is_null())           applyStats(stats);
    if (!activation.is_null())      applyActivation(activation);
    if (!balance.is_null())         applyBalance(balance);
    if (!positionsArr.is_null())    applyPositions(positionsArr);
    if (!claimableArr.is_null())    applyClaimable(claimableArr);
    if (!transactionsArr.is_null()) applyTransactions(transactionsArr);
}

void YellowbackController::feedAttest(const json& price, const json& attestorsArr, const json& selection) {
    if (!price.is_null())        applyPrice(price);
    if (!attestorsArr.is_null()) applyAttestors(attestorsArr);
    if (!selection.is_null())    applySelection(selection);
}

void YellowbackController::feedUpgrade(const json& claimingVaults, const json& attestorSet, const json& vaultInfo) {
    if (!vaultInfo.is_null())      vaultInfoJson = vaultInfo.is_object() ? vaultInfo : json::object();
    if (!claimingVaults.is_null()) applyClaimingVaults(claimingVaults);
    if (!attestorSet.is_null())    applyAttestorSet(attestorSet);
}

// ── rpcversion 5: the vault upgrade, the attestor set, pending claims ─────────────────────

const json& YellowbackController::upgrade() const {
    return YellowbackJson::obj(infoJson, YellowbackRpc::Info::UPGRADE);
}

bool YellowbackController::upgradeActive() const {
    return YellowbackJson::toStr(upgrade(), YellowbackRpc::Upgrade::STATUS) == YellowbackRpc::Upgrade::STATUS_ACTIVE;
}

QString YellowbackController::attestorSetId() const {
    QString id = YellowbackJson::toStr(paramsJson, YellowbackRpc::Params::ATTESTOR_SET_ID);
    return id.isEmpty() ? YellowbackJson::toStr(upgrade(), YellowbackRpc::Upgrade::ATTESTOR_SET_ID) : id;
}

int YellowbackController::claimDelay() const {
    const qint64 d = YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::CLAIM_DELAY, -1);
    return (int)(d >= 0 ? d : YellowbackJson::toInt(upgrade(), YellowbackRpc::Upgrade::CLAIM_DELAY, 0));
}

YellowbackSetMember YellowbackSetMember::fromJson(const json& j) {
    using namespace VaultRpc::Member;
    YellowbackSetMember m;
    m.key          = YellowbackJson::toStr (j, KEY);
    m.status       = YellowbackJson::toStr (j, STATUS);
    m.current      = YellowbackJson::toBool(j, CURRENT);
    m.live         = YellowbackJson::toBool(j, LIVE);
    m.joinHeight   = (int)YellowbackJson::toInt(j, JOIN_HEIGHT);
    m.lastAct      = (int)YellowbackJson::toInt(j, LAST_ACT);
    // bondvalue is YEC (decimal) like every Ycash amount (vault-rpc.md, Conventions)
    m.bondValue    = j.is_object() && j.find(BOND_VALUE) != j.end() && j[BOND_VALUE].is_number()
                         ? (qint64)std::llround(j[BOND_VALUE].get<double>() * 100000000.0) : 0;
    m.bondLocktime = (int)YellowbackJson::toInt(j, BOND_LOCKTIME);
    m.bondFrozen   = YellowbackJson::toBool(j, BOND_FROZEN);
    m.wallet       = YellowbackJson::toBool(j, WALLET);
    return m;
}

QString YellowbackSetMember::describe(int height, int livenessWindow) const {
    if (status != VaultRpc::Member::STATUS_ACTIVE)
        return bondFrozen ? QObject::tr("%1, bond frozen").arg(status) : status;
    if (!current) return QObject::tr("joined at %1, not yet current (maturing)").arg(joinHeight);
    QString s = live ? QObject::tr("current, live (last act at %1)").arg(lastAct)
                     : QObject::tr("current but DORMANT: no act since height %1, beyond the %2-block liveness window").arg(lastAct).arg(livenessWindow);
    if (live && livenessWindow > 0 && height > 0)
        s += QObject::tr("; dormant from height %1 without a heartbeat").arg(lastAct + livenessWindow + 1);
    if (bondFrozen) s += QObject::tr("; bond frozen");
    return s;
}

QList<YellowbackSetMember> YellowbackController::setMembers() const {
    QList<YellowbackSetMember> out;
    if (setJson.is_object() && setJson.find(VaultRpc::Set::MEMBER_LIST) != setJson.end() && setJson[VaultRpc::Set::MEMBER_LIST].is_array())
        for (auto& m : setJson[VaultRpc::Set::MEMBER_LIST]) out.append(YellowbackSetMember::fromJson(m));
    return out;
}

QList<YellowbackSetMember> YellowbackController::walletMembers() const {
    QList<YellowbackSetMember> out;
    for (const auto& m : setMembers()) if (m.wallet) out.append(m);
    return out;
}

bool YellowbackController::isAttestor() const {
    for (const auto& m : walletMembers())
        if (m.status == VaultRpc::Member::STATUS_ACTIVE && m.current && !m.bondFrozen) return true;
    return false;
}

int YellowbackController::setLivenessWindow() const {
    return (int)YellowbackJson::toInt(setJson, VaultRpc::Set::LIVENESS_WINDOW, 0);
}

int YellowbackController::setCancelThreshold() const {
    return (int)YellowbackJson::toInt(setJson, VaultRpc::Set::CANCEL_THRESHOLD, 1);
}

QString YellowbackController::p2pkhScriptForKeyId(const QString& keyIdHex) {
    static const QRegularExpression hex40("^[0-9a-fA-F]{40}$");
    if (!hex40.match(keyIdHex).hasMatch()) return QString();
    QString internal;
    for (int i = 38; i >= 0; i -= 2) internal += keyIdHex.mid(i, 2);    // uint160::GetHex prints the bytes reversed
    return "76a914" % internal.toLower() % "88ac";
}

void YellowbackController::applyClaimingVaults(const json& arr) {
    claimingJson = arr.is_array() ? arr : json::array();
    rebuildPendingClaims();
}

void YellowbackController::rebuildPendingClaims() {
    using namespace YellowbackRpc;
    QSet<QString> myClaims, myVaults;
    for (int i = 0; i < transactions->rowCount(QModelIndex()); i++) {
        const YellowbackTx* t = transactions->txAt(i);
        if (t != nullptr && t->type == Transaction::TYPE_CLAIM) myClaims.insert(t->txid);
    }
    for (int i = 0; ; i++) {
        const YellowbackPosition* p = positions->positionAt(i);
        if (p == nullptr) break;
        myVaults.insert(p->txid);
    }
    QList<YellowbackPendingClaim> rows;
    for (auto& v : claimingJson) {
        YellowbackPosition vault = YellowbackPosition::fromJson(v);
        if (vault.status != Position::STATUS_CLAIMING) continue;
        for (const auto& in : vault.intents) {
            YellowbackPendingClaim c;
            c.vault = vault;
            c.intent = in;
            c.mineClaim = myClaims.contains(in.txid);
            c.mineVault = myVaults.contains(vault.txid);
            rows.append(c);
        }
    }
    pendingClaims->setNewData(rows, indexHeight);
    emit pendingClaimsUpdated();
}

void YellowbackController::applyAttestorSet(const json& s) {
    setJson = s.is_object() ? s : json::object();
    emit attestorSetUpdated();
}

void YellowbackController::refreshPendingClaims() {
    call(YellowbackRpc::LISTVAULTS, json::array({YellowbackRpc::Position::STATUS_CLAIMING, 1000, 0}),
        [=, this](const json& arr) { applyClaimingVaults(arr); },
        [=, this](const QString& e) { log("yed_listvaults: " + e); });
}

void YellowbackController::refreshAttestorSet() {
    const QString id = attestorSetId();
    if (id.isEmpty()) return;
    call(VaultRpc::SET_GETINFO, json::array({id.toStdString()}),
        [=, this](const json& s) { applyAttestorSet(s); },
        [=, this](const QString& e) { log("set_getinfo: " + e); });
}

void YellowbackController::refreshVaultInfo() {
    call(VaultRpc::VAULT_GETINFO, json(nullptr),
        [=, this](const json& v) { vaultInfoJson = v.is_object() ? v : json::object(); emit infoUpdated(); },
        [=, this](const QString& e) { log("vault_getinfo: " + e); });
}

// The claimant intent is output 0 of the claim (yed_claim); the claim closed exactly one vault
// (yed_gettxinfo.closedVaults). That vault is CLAIMING until the intent is spent: CLAIMED after a
// release, and erased after an attestor cancel, which re-creates it at the cancel's output 0 under
// a new txid (U-23, U-24), so yed_getvault answers vault-not-found for the old one.
void YellowbackController::refreshClaimOutcomes() {
    using namespace YellowbackRpc;
    for (int i = 0; i < transactions->rowCount(QModelIndex()); i++) {
        const YellowbackTx* t = transactions->txAt(i);
        if (t == nullptr || t->type != Transaction::TYPE_CLAIM || t->expired || t->height <= 0) continue;
        const QString claimTxid = t->txid;
        if (outcomes.contains(claimTxid) && (outcomes[claimTxid].outcome == "released" || outcomes[claimTxid].outcome == "cancelled")) continue;
        YellowbackClaimOutcome base;
        base.claimTxid = claimTxid;
        base.burnedCents = t->burned;
        base.height = t->height;
        call(GETTXINFO, json::array({claimTxid.toStdString()}),
            [=, this](const json& info) {
                QString vaultTxid;
                if (info.is_object() && info.find(TxInfo::CLOSED_VAULTS) != info.end() && info[TxInfo::CLOSED_VAULTS].is_array() &&
                    !info[TxInfo::CLOSED_VAULTS].empty())
                    vaultTxid = YellowbackJson::toStr(info[TxInfo::CLOSED_VAULTS][0], "txid");
                if (vaultTxid.isEmpty()) return;
                call(GETVAULT, json::array({vaultTxid.toStdString()}),
                    [=, this](const json& v) {
                        YellowbackClaimOutcome o = base;
                        o.vaultTxid = vaultTxid;
                        const QString st = YellowbackJson::toStr(v, Position::STATUS);
                        o.outcome = st == Position::STATUS_CLAIMED ? "released" : st == Position::STATUS_CLAIMING ? "pending" : st;
                        outcomes[claimTxid] = o;
                        emit claimOutcomesUpdated();
                    },
                    [=, this](const QString& e) {
                        if (!e.startsWith(Errors::VAULT_NOT_FOUND)) { log("yed_getvault: " + e); return; }
                        YellowbackClaimOutcome o = base;
                        o.vaultTxid = vaultTxid;
                        o.outcome = "cancelled";
                        outcomes[claimTxid] = o;
                        emit claimOutcomesUpdated();
                    });
            },
            [=, this](const QString& e) { log("yed_gettxinfo: " + e); });
    }
}

void YellowbackController::applyPrice(const json& p) {
    priceJson = p.is_object() ? p : json::object();
    emit priceUpdated();
}

void YellowbackController::applyAttestors(const json& arr) {
    QList<YellowbackAttestor> list;
    if (arr.is_array())
        for (auto& it : arr) list.append(YellowbackAttestor::fromJson(it));
    attestors->setNewData(list, indexHeight);
    emit attestorsUpdated();
}

void YellowbackController::applySelection(const json& s) {
    selectionJson = s.is_object() ? s : json::object();
    emit selectionUpdated();
}

void YellowbackController::refreshPrice() {
    call(YellowbackRpc::GETPRICE, json(nullptr),
        [=, this](const json& p) { applyPrice(p); },
        [=, this](const QString& e) { log("yed_getprice: " + e); });
}

void YellowbackController::refreshAttestors() {
    call(YellowbackRpc::LISTATTESTORS, json(nullptr),
        [=, this](const json& arr) { applyAttestors(arr); },
        [=, this](const QString& e) { log("yed_listattestors: " + e); });
}

void YellowbackController::refreshSelection() {
    // A MINT built now cites refHeightNow() with the empty selector (W9), as yed_estimatecollateral does.
    call(YellowbackRpc::GETSELECTION, json::array({refHeightNow(), ""}),
        [=, this](const json& s) { applySelection(s); },
        [=, this](const QString& e) { log("yed_getselection: " + e); });
}

const json& YellowbackController::attest() const {
    return YellowbackJson::obj(infoJson, YellowbackRpc::Info::ATTEST);
}

qint64 YellowbackController::divergeBpsAttest() const {
    return YellowbackJson::toInt(YellowbackJson::obj(paramsJson, YellowbackRpc::Params::ATTEST),
                                 YellowbackRpc::ParamsAttest::DIVERGE_BPS_ATTEST, 0);
}

// ── v3 attestation layer: banner, selection line, divergence (plan §4.8) ──────────────────

QString YellowbackController::describeAttest(const json& attest) {
    using namespace YellowbackRpc::Attest;
    if (!attest.is_object() || attest.empty()) return QString();
    // `required` false: every bundle-reading rule is vacuous whatever the status says (W15)
    if (!YellowbackJson::toBool(attest, REQUIRED, true))
        return tr("Attestation layer disabled by parameter set: prices come from pool quotes alone.");
    QString status = YellowbackJson::toStr(attest, STATUS);
    if (status == STATUS_UNARMED)
        return tr("UNARMED: prices come from pool quotes alone; %1 attestor(s) seated, the layer arms once enough bonds have matured.")
                   .arg(YellowbackJson::toInt(attest, SEATED_COUNT));
    if (status == STATUS_TRIGGERED)
        return tr("TRIGGERED at %1, arms at %2: from then on every mint and claim carries an attested price.")
                   .arg(YellowbackJson::toInt(attest, TRIGGER_HEIGHT)).arg(YellowbackJson::toInt(attest, ARM_HEIGHT));
    if (status == STATUS_ARMED)
        return tr("ARMED since %1: every mint and claim carries a price both pools and attestors signed off on. "
                  "%2 attestor(s) seated; this node's pool holds fresh attestations from %3 of them.")
                   .arg(YellowbackJson::toInt(attest, ARM_HEIGHT))
                   .arg(YellowbackJson::toInt(attest, SEATED_COUNT)).arg(YellowbackJson::toInt(attest, POOL_FRESH));
    return status;
}

QString YellowbackController::describeSelection(const json& selection) {
    using namespace YellowbackRpc::Selection;
    if (!selection.is_object() || selection.empty()) return QString();
    if (!YellowbackJson::toBool(selection, ARMED)) return QString();
    int selected = selection.find(SELECTED) != selection.end() && selection[SELECTED].is_array() ? (int)selection[SELECTED].size() : 0;
    int reachable = (int)YellowbackJson::toInt(selection, REACHABLE);
    int need = (int)YellowbackJson::toInt(selection, M_SELECT);
    QString line = tr("%1 of %2 selected attestors reachable").arg(reachable).arg(selected);
    if (reachable < need)
        line += tr(" (a mint needs %1: waiting for the subscriber to fill the pool)").arg(need);
    return line;
}

QString YellowbackController::describeDivergence(const json& estimate, qint64 divergeBpsAttest) {
    using namespace YellowbackRpc::Estimate;
    if (YellowbackJson::isNull(estimate, DIVERGENCE_BPS) || divergeBpsAttest <= 0) return QString();
    qint64 bps = YellowbackJson::toInt(estimate, DIVERGENCE_BPS);
    if (bps <= divergeBpsAttest) return QString();
    return tr("the pools' fast median and the attestors disagree by %1 %; minting paused until they agree (they usually do within a fast window)").arg(QString::number(bps / 100.0, 'f', 2));
}

QString YellowbackController::describeDivergenceError(const QString& errorMessage, qint64 divergeBpsAttest) {
    if (!errorMessage.startsWith(YellowbackRpc::Errors::MINT10_DIVERGED)) return QString();
    // The message carries no number the contract fixes; the threshold is what the user can act on.
    return divergeBpsAttest > 0
        ? tr("the pools' fast median and the attestors disagree by more than %1 %; minting paused until they agree").arg(QString::number(divergeBpsAttest / 100.0, 'f', 2))
        : tr("the pools' fast median and the attestors disagree; minting paused until they agree");
}

std::optional<qint64> YellowbackController::protocolPriceMicroUsd() const {
    using namespace YellowbackRpc;
    if (!available || statsJson.empty()) return std::nullopt;
    // rpcversion 5: yed_getactivation is the vault upgrade, the same object as yed_getinfo.upgrade
    const json& up = activationJson.empty() ? upgrade() : activationJson;
    if (YellowbackJson::toStr(up, ActivationInfo::STATUS) != Upgrade::STATUS_ACTIVE) return std::nullopt;
    if (YellowbackJson::isNull(statsJson, Stats::P_FAST)) return std::nullopt;
    const qint64 p = YellowbackJson::toInt(statsJson, Stats::P_FAST);
    return p > 0 ? std::optional<qint64>(p) : std::nullopt;
}

void YellowbackController::pushProtocolPrice() {
    if (auto p = protocolPriceMicroUsd()) Settings::getInstance()->setYellowbackPrice((double)p.value() / 1000000.0);
}

void YellowbackController::applyStats(const json& s) {
    statsJson = s.is_object() ? s : json::object();
    pushProtocolPrice();
    // the Positions page's ratio column is judged at the tip's claim price
    positions->setPrices(YellowbackJson::isNull(statsJson, YellowbackRpc::Stats::P_FAST) ? 0 : YellowbackJson::toInt(statsJson, YellowbackRpc::Stats::P_FAST),
                         YellowbackJson::isNull(statsJson, YellowbackRpc::Stats::P_CLAIM) ? 0 : YellowbackJson::toInt(statsJson, YellowbackRpc::Stats::P_CLAIM));
    emit statsUpdated();
}

void YellowbackController::applyActivation(const json& a) {
    activationJson = a.is_object() ? a : json::object();
    pushProtocolPrice();
    emit statsUpdated();
}

void YellowbackController::applyBalance(const json& b) {
    confirmed   = YellowbackJson::toInt(b, YellowbackRpc::Balance::CONFIRMED_CENTS);
    unconfirmed = YellowbackJson::toInt(b, YellowbackRpc::Balance::UNCONFIRMED_CENTS);
    emit balanceUpdated();
}

void YellowbackController::applyPositions(const json& arr) {
    QList<YellowbackPosition> list;
    if (arr.is_array())
        for (auto& it : arr) list.append(YellowbackPosition::fromJson(it));
    positions->setPrices(YellowbackJson::isNull(statsJson, YellowbackRpc::Stats::P_FAST) ? 0 : YellowbackJson::toInt(statsJson, YellowbackRpc::Stats::P_FAST),
                         YellowbackJson::isNull(statsJson, YellowbackRpc::Stats::P_CLAIM) ? 0 : YellowbackJson::toInt(statsJson, YellowbackRpc::Stats::P_CLAIM));
    positions->setNewData(list, indexHeight);
    rebuildPendingClaims();
    emit positionsUpdated();
}

void YellowbackController::applyClaimable(const json& arr) {
    QList<YellowbackClaimable> list;
    if (arr.is_array())
        for (auto& it : arr) list.append(YellowbackClaimable::fromJson(it));
    claimable->setNewData(list, indexHeight);
    emit claimableUpdated();
}

void YellowbackController::applyTransactions(const json& arr) {
    QList<YellowbackTx> list;
    if (arr.is_array())
        for (auto& it : arr) list.append(YellowbackTx::fromJson(it));
    transactions->setNewData(list, indexHeight);
    rebuildPendingClaims();
    emit transactionsUpdated();
}

void YellowbackController::refreshStats() {
    call(YellowbackRpc::GETSTATS, json(nullptr),
        [=, this](const json& s) { applyStats(s); },
        [=, this](const QString& e) { log("yed_getstats: " + e); });
}

void YellowbackController::refreshActivation() {
    call(YellowbackRpc::GETACTIVATION, json(nullptr),
        [=, this](const json& a) { applyActivation(a); },
        [=, this](const QString& e) { log("yed_getactivation: " + e); });
}

void YellowbackController::refreshBalance() {
    call(YellowbackRpc::GETBALANCE, json(nullptr),
        [=, this](const json& b) { applyBalance(b); },
        [=, this](const QString& e) { log("yed_getbalance: " + e); });
}

void YellowbackController::refreshPositions() {
    call(YellowbackRpc::LISTPOSITIONS, json(nullptr),
        [=, this](const json& arr) { applyPositions(arr); },
        [=, this](const QString& e) { log("yed_listpositions: " + e); });
}

void YellowbackController::refreshClaimable() {
    call(YellowbackRpc::LISTCLAIMABLE, json(nullptr),
        [=, this](const json& arr) { applyClaimable(arr); },
        [=, this](const QString& e) { log("yed_listclaimable: " + e); });
}

void YellowbackController::refreshTransactions() {
    call(YellowbackRpc::LISTTRANSACTIONS, json::array({200, 0}),
        [=, this](const json& arr) { applyTransactions(arr); },
        [=, this](const QString& e) { log("yed_listtransactions: " + e); });
}

// ── Status banner ─────────────────────────────────────────────────────────────────────────

YellowbackStatus YellowbackController::status() const {
    return describeStatus(available, reason, infoJson, statsJson, activationJson);
}

// Plan §4.8, banner row, under the vault upgrade (rpcversion 5). Every line is derived from
// contract fields only. The wording follows the upgrade plan's trust statement (§10): the
// Yellowback rules are consensus rules every full node checks; there is no enforcing pool, no
// pause and no abandonment to describe, so the activation, valve, sunset and abandonment lines of
// rpcversion 4 are gone with the fields they read.
YellowbackStatus YellowbackController::describeStatus(bool available, const QString& reason,
                                                      const json& info, const json& stats, const json& activation) {
    using namespace YellowbackRpc;
    YellowbackStatus st;
    st.available = available;
    st.reason    = reason;
    if (!available) {
        st.headline = tr("Yellowback unavailable: %1").arg(reason);
        return st;
    }
    Q_UNUSED(stats);

    const json& up = activation.is_object() && !activation.empty() ? activation : YellowbackJson::obj(info, Info::UPGRADE);
    const QString status = YellowbackJson::toStr(up, Upgrade::STATUS);
    const qint64  at     = YellowbackJson::toInt(up, Upgrade::ACTIVATION_HEIGHT, -1);
    const QString branch = YellowbackJson::toStr(up, Upgrade::BRANCH_ID, "6d5b7a31");
    if (status == Upgrade::STATUS_ACTIVE)
        st.headline = tr("Yellowback rules are Ycash consensus rules since height %1 (the vault upgrade, branch %2): every full node checks them.")
                          .arg(at).arg(branch);
    else if (status == Upgrade::STATUS_PENDING)
        st.headline = tr("The vault upgrade (branch %1) activates at height %2; Yellowback starts there.").arg(branch).arg(at);
    else
        st.headline = tr("Vault upgrade status: %1.").arg(status.isEmpty() ? tr("unknown") : status);

    // H10: the node's own report of the coin locks that keep an ordinary send from burning YED
    if (!YellowbackJson::toBool(info, Info::PROTECTED_BY_INDEX, true))
        st.warnings << tr("This node is not holding your YED outputs locked, so an ordinary YEC send could spend one and burn the YED it carries. "
                          "Check that the node's Yellowback index is healthy.");
    else if (YellowbackJson::toInt(info, Info::LOCKED_OUTPUTS) > 0)
        st.notes << tr("%1 of this wallet's outputs carry YED and are locked by the node, so an ordinary YEC send cannot spend them "
                       "(yed_unlockcoin releases one deliberately).").arg(YellowbackJson::toInt(info, Info::LOCKED_OUTPUTS));

    // The connected node's own quote state, shown only when it can mine (has a payout key)
    const json& miner = YellowbackJson::obj(info, Info::MINER);
    if (YellowbackJson::has(miner, Miner::PAYOUT_ADDRESS)) {
        QString kind = YellowbackJson::toStr(miner, Miner::QUOTE_KIND);
        QString line = tr("This node mines with payout %1: ").arg(YellowbackJson::toStr(miner, Miner::PAYOUT_ADDRESS));
        if (kind == Miner::KIND_QUOTE)
            line += tr("next tag carries a price quote (%1 s old)").arg(YellowbackJson::toInt(miner, Miner::QUOTE_AGE_SECONDS));
        else
            line += tr("next block carries no price quote");
        line += YellowbackJson::toBool(miner, Miner::ELIGIBLE) ? tr("; eligible for enforcement fees") : tr("; not eligible for enforcement fees");
        st.notes << line;
    }
    return st;
}

// ── Addresses ─────────────────────────────────────────────────────────────────────────────

QString YellowbackController::addressPrefix() const {
    if (net == "test")    return "yt";
    if (net == "regtest") return "yr";
    return "ye";
}

bool YellowbackController::looksLikeYellowbackAddress(const QString& addr) const {
    QString a = addr.trimmed();
    if (a.length() < 26 || a.length() > 40) return false;
    if (!a.startsWith(addressPrefix())) return false;
    static const QRegularExpression base58("^[1-9A-HJ-NP-Za-km-z]+$");
    return base58.match(a).hasMatch();
}

bool YellowbackController::isShieldedAddress(const QString& addr) const {
    QString a = addr.trimmed();
    // Ycash: s1.../s3... transparent, ys.../ytestsapling... Sapling, z... Sprout. None can hold YED.
    return a.startsWith("s1") || a.startsWith("s3") || a.startsWith("ys") ||
           a.startsWith("ytestsapling") || a.startsWith("z");
}

double YellowbackController::yecBalance() const {
    if (rpc == nullptr) return 0.0;
    double total = 0.0;
    auto balances = rpc->getModel()->getAllBalances();
    for (auto it = balances.constBegin(); it != balances.constEnd(); ++it)
        if (Settings::isTAddress(it.key())) total += it.value();
    return total;
}

double YellowbackController::yecBalanceAt(const QString& source) const {
    if (source.isEmpty()) return yecBalance();
    if (rpc == nullptr) return 0.0;
    return rpc->getModel()->getAllBalances().value(source, 0.0);
}

QList<QPair<QString, double>> YellowbackController::saplingAddresses() const {
    QList<QPair<QString, double>> out;
    if (rpc == nullptr) return out;
    auto balances = rpc->getModel()->getAllBalances();
    for (const QString& a : rpc->getModel()->getAllZAddresses()) {
        if (!Settings::getInstance()->isSaplingAddress(a)) continue;   // Sprout cannot fund or receive (plan I2)
        out.append(qMakePair(a, balances.value(a, 0.0)));
    }
    std::sort(out.begin(), out.end(), [](const QPair<QString, double>& x, const QPair<QString, double>& y) { return x.second > y.second; });
    return out;
}

QList<QPair<QString, double>> YellowbackController::transparentAddresses() const {
    QList<QPair<QString, double>> out;
    if (rpc == nullptr) return out;
    auto balances = rpc->getModel()->getAllBalances();
    for (const QString& a : rpc->getModel()->getAllTAddresses()) {
        if (!Settings::isTAddress(a)) continue;
        out.append(qMakePair(a, balances.value(a, 0.0)));
    }
    std::sort(out.begin(), out.end(), [](const QPair<QString, double>& x, const QPair<QString, double>& y) { return x.second > y.second; });
    return out;
}

// ── Protocol parameters ───────────────────────────────────────────────────────────────────

// MIN_MINT / MAX_MINT / MIN_OUTPUT are §3.1 constants the v2 contract does not report; the
// compiled-in values stand (the node refuses out-of-range amounts anyway).
qint64 YellowbackController::minMintCents() const   { return YellowbackRpc::MIN_MINT_CENTS; }
// H-12: MAX_MINT is $2,500 on mainnet and testnet for the first parameter lifetime; regtest keeps $10,000.
qint64 YellowbackController::maxMintCents() const   { return net == "regtest" || net.isEmpty() ? YellowbackRpc::MAX_MINT_CENTS : YellowbackRpc::MAX_MINT_CENTS_GUARDED; }
qint64 YellowbackController::minOutputCents() const { return YellowbackRpc::MIN_OUTPUT_CENTS; }
int    YellowbackController::refLag() const         { return (int)YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::REF_LAG, 0); }
int    YellowbackController::refWindow() const      { return (int)YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::REF_WINDOW, 0); }
int    YellowbackController::grace() const          { return (int)YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::GRACE, 0); }

QList<YellowbackController::TermClass> YellowbackController::termClasses() const {
    using namespace YellowbackRpc;
    QList<TermClass> out;
    if (!paramsJson.is_object() || paramsJson.find(Params::CLASSES) == paramsJson.end() || !paramsJson[Params::CLASSES].is_array())
        return out;
    for (auto& c : paramsJson[Params::CLASSES]) {
        TermClass t;
        t.name         = YellowbackJson::toStr(c, ParamClass::CLASS);
        t.minBlocks    = (int)YellowbackJson::toInt(c, ParamClass::MIN_BLOCKS);
        t.maxBlocks    = (int)YellowbackJson::toInt(c, ParamClass::MAX_BLOCKS);
        t.baseRatioBps = YellowbackJson::toInt(c, ParamClass::BASE_RATIO_BPS);
        out.append(t);
    }
    return out;
}

// ── Mint gate ─────────────────────────────────────────────────────────────────────────────

QString YellowbackController::balanceSummary() const {
    if (!available) return QString("-");
    QString line = YellowbackFormat::cents(confirmed) % tr(" YED");
    if (unconfirmed != 0) line += tr(" (%1 unconfirmed)").arg(YellowbackFormat::cents(unconfirmed));
    return line;
}

QString YellowbackController::collateralSummary() const {
    using namespace YellowbackRpc;
    if (!available) return QString("-");
    qint64 zat = 0; int vaults = 0;
    for (int i = 0; ; i++) {
        const YellowbackPosition* p = positions->positionAt(i);
        if (p == nullptr) break;
        if (p->status == Position::STATUS_ACTIVE) { zat += p->collateralZat; vaults++; }
    }
    if (vaults == 0) return tr("none (no active vault)");
    QString line = tr("%1 in %2 active vault(s)").arg(YellowbackFormat::zec(zat)).arg(vaults);   // zec() carries the unit
    // its dollar value at the mint price, when the node has one
    if (!statsJson.empty() && !YellowbackJson::isNull(statsJson, Stats::P_MINT)) {
        const long double usd = (long double)zat / 100000000.0L * (long double)YellowbackJson::toInt(statsJson, Stats::P_MINT) / 1000000.0L;
        line += tr(" (about %1 at the mint price)").arg(YellowbackFormat::cents((qint64)(usd * 100)));
    }
    return line;
}

QStringList YellowbackController::mintableClasses() const {
    using namespace YellowbackRpc;
    if (statsJson.empty()) return QStringList();
    if (YellowbackJson::has(statsJson, Stats::MINTABLE_CLASSES)) return YellowbackJson::strings(statsJson, Stats::MINTABLE_CLASSES);
    // a node from before W16: every class while minting is open, none otherwise
    QStringList all;
    if (YellowbackJson::toBool(statsJson, Stats::MINTING_ALLOWED, false)) for (const auto& c : termClasses()) all << c.name;
    return all;
}

bool YellowbackController::softSupplyCap() const {
    return YellowbackJson::has(infoJson, YellowbackRpc::Info::SUPPLY_CAP_REACHED);
}

bool YellowbackController::supplyCapReached() const {
    return softSupplyCap() && YellowbackJson::toBool(infoJson, YellowbackRpc::Info::SUPPLY_CAP_REACHED, false);
}

// The restrictions a mint can pass when its class reaches the recapitalisation floor: GLOBAL_RATIO
// alone (W16), the supply cap with no halt bit (W20), or both, with at least one class at or above
// the floor. Every other halt still stops every mint; so does the cap on a node from before W20.
static bool recapOnly(const json& stats, const QStringList& mintable, bool capReached) {
    using namespace YellowbackRpc;
    if (mintable.isEmpty()) return false;
    QStringList halts = YellowbackJson::strings(stats, Stats::HALT_MASK);
    if (halts.size() == 1 && halts.first() == Stats::HALT_GLOBAL_RATIO) return true;
    return halts.isEmpty() && capReached;
}

// "YED in circulation ($X) has reached the supply cap ($Y, Z % of issued YEC value)"
static QString capPhrase(const json& stats, const json& params) {
    using namespace YellowbackRpc;
    return QObject::tr("YED in circulation (%1) has reached the supply cap (%2, %3 of issued YEC value)")
        .arg(YellowbackFormat::cents(YellowbackJson::toInt(stats, Stats::SUPPLY_CENTS)))
        .arg(YellowbackJson::isNull(stats, Stats::SUPPLY_CAP_CENTS) ? QObject::tr("undefined") : YellowbackFormat::cents(YellowbackJson::toInt(stats, Stats::SUPPLY_CAP_CENTS)))
        .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(params, Params::SUPPLY_CAP_BPS)));
}

QString YellowbackController::mintLimit() const {
    using namespace YellowbackRpc;
    if (!available || statsJson.empty()) return QString();
    const QStringList mintable = mintableClasses();
    const bool cap = supplyCapReached();
    if (!recapOnly(statsJson, mintable, cap)) return QString();
    const bool ratio = YellowbackJson::strings(statsJson, Stats::HALT_MASK).contains(Stats::HALT_GLOBAL_RATIO);
    const QString globalRatio = YellowbackJson::isNull(statsJson, Stats::GLOBAL_RATIO_BPS) ? tr("undefined") : YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(statsJson, Stats::GLOBAL_RATIO_BPS));
    const QString haltFloor = YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::GLOBAL_RATIO_HALT_BPS, 25000));
    const QString recapFloor = YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::RECAP_RATIO_BPS, 50000));
    if (ratio && cap)
        return tr("Minting is limited: the system-wide collateral ratio is %1, below its %2 floor, and %3. "
                  "Only class %4 can mint until both clear, because its minimum ratio reaches the %5 recapitalisation floor; "
                  "every such mint raises the ratio and is heavily over-collateralised.")
            .arg(globalRatio).arg(haltFloor).arg(capPhrase(statsJson, paramsJson)).arg(mintable.join(tr(" or "))).arg(recapFloor);
    if (cap)
        return tr("Minting is limited: %1. Only class %2 can mint above the cap, because its minimum ratio reaches the %3 floor; "
                  "such mints are heavily over-collateralised.")
            .arg(capPhrase(statsJson, paramsJson)).arg(mintable.join(tr(" or "))).arg(recapFloor);
    return tr("Minting is limited: the system-wide collateral ratio is %1, below its %2 floor. "
              "Only class %3 can mint until it recovers, because its minimum ratio reaches the %4 recapitalisation floor; "
              "every such mint raises the ratio.")
        .arg(globalRatio).arg(haltFloor).arg(mintable.join(tr(" or "))).arg(recapFloor);
}

QString YellowbackController::mintBlocker(qint64 cents, const QString& termClass) const {
    using namespace YellowbackRpc;
    if (!available) return reason;
    if (statsJson.empty()) return tr("Waiting for yed_getstats.");

    // H-1: on a network that requires it, no mint is built while the layer is not ARMED (the node
    // refuses mintpol-unarmed; mintableClasses is empty), whatever else the halt mask says
    if (mintRequiresArmed() && !isArmed())
        return tr("Minting is paused until the attestation layer is ARMED (%1): on this network every mint needs an attested price, "
                  "and a mint without one would be VOID (mint-halted-unarmed).").arg(describeAttest(attest()));

    const QStringList mintable = mintableClasses();
    const bool cap = supplyCapReached();
    const bool limited = recapOnly(statsJson, mintable, cap);
    const qint64 recap = YellowbackJson::toInt(paramsJson, Params::RECAP_RATIO_BPS, 50000);
    QStringList halts = YellowbackJson::strings(statsJson, Stats::HALT_MASK);
    if (!halts.isEmpty() && !limited) {
        QStringList lines;
        for (const QString& h : halts) lines << YellowbackFormat::haltReason(h);
        // When it ends, not only why (the owner found the wait "awkward" without it): the divergence
        // halt clears by itself within the slow window; the global-ratio one when the ratio recovers
        if (halts.contains(Stats::HALT_DIVERGENCE)) {
            const int slow = YellowbackJson::has(paramsJson, Params::WINDOWS) ? (int)YellowbackJson::toInt(paramsJson[Params::WINDOWS], "slow", 64) : 64;
            lines << tr("The divergence clears by itself once the price windows agree again, within %1 of the last big move.")
                         .arg(YellowbackFormat::blocksAndDuration(slow));
        }
        if (halts.contains(Stats::HALT_GLOBAL_RATIO)) {
            QStringList recapClasses;
            for (const auto& c : termClasses()) if (c.baseRatioBps >= recap) recapClasses << c.name;
            lines << tr("The global ratio recovers as the price rises or as vaults are redeemed and claimed; once it is the only halt left, class %1 can mint again and each such mint raises it.")
                         .arg(recapClasses.isEmpty() ? QString("A") : recapClasses.join(tr(" or ")));
        }
        return tr("Minting is paused. ") % lines.join(" ");
    }
    if (limited && !termClass.isEmpty() && !mintable.contains(termClass)) {
        const bool ratio = halts.contains(Stats::HALT_GLOBAL_RATIO);
        const QString globalRatio = YellowbackJson::isNull(statsJson, Stats::GLOBAL_RATIO_BPS) ? tr("undefined") : YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(statsJson, Stats::GLOBAL_RATIO_BPS));
        const QString haltFloor = YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::GLOBAL_RATIO_HALT_BPS, 25000));
        const QString capCents = YellowbackJson::isNull(statsJson, Stats::SUPPLY_CAP_CENTS) ? tr("undefined") : YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS));
        if (ratio && cap)
            return tr("Class %1 cannot mint while the system-wide collateral ratio (%2) is below its %3 floor and YED in circulation (%4) is at the supply cap (%5): "
                      "only class %6 can, because its minimum ratio reaches the %7 recapitalisation floor. Choose that lock length, or wait for both to clear.")
                .arg(termClass).arg(globalRatio).arg(haltFloor)
                .arg(YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS))).arg(capCents)
                .arg(mintable.join(tr(" or "))).arg(YellowbackFormat::bpsAsPercent(recap));
        if (cap)
            return tr("Class %1 cannot mint while YED in circulation (%2) is at the supply cap (%3): above the cap only class %4 can, "
                      "because its minimum ratio reaches the %5 floor. Choose that lock length, or wait for redemptions or a higher YEC price to make room.")
                .arg(termClass).arg(YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS))).arg(capCents)
                .arg(mintable.join(tr(" or "))).arg(YellowbackFormat::bpsAsPercent(recap));
        return tr("Class %1 cannot mint while the system-wide collateral ratio (%2) is below its %3 floor: only class %4 can, "
                  "because its minimum ratio reaches the %5 recapitalisation floor. Choose that lock length, or wait for the ratio to recover.")
            .arg(termClass).arg(globalRatio).arg(haltFloor)
            .arg(mintable.join(tr(" or ")))
            .arg(YellowbackFormat::bpsAsPercent(recap));
    }
    if (!limited && !YellowbackJson::toBool(statsJson, Stats::MINTING_ALLOWED, true)) {
        // No halt bit, yet not allowed: the supply cap has no room (mintpol-cap)
        if (YellowbackJson::has(statsJson, Stats::SUPPLY_CAP_CENTS)) {
            QString line = tr("Minting is paused: the supply cap (%1) is reached with %2 in circulation.")
                    .arg(YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS)))
                    .arg(YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS)));
            // W20: above the cap a class at or over the floor could mint, but none reaches it now
            if (cap) line += tr(" No term class reaches the %1 floor a mint above the cap needs.").arg(YellowbackFormat::bpsAsPercent(recap));
            return line;
        }
        return tr("Minting is paused (yed_getstats reports mintingAllowed = false).");
    }

    if (cents > 0) {
        if (cents < minMintCents() || cents > maxMintCents())
            return tr("A mint must be between %1 and %2.")
                    .arg(YellowbackFormat::cents(minMintCents())).arg(YellowbackFormat::cents(maxMintCents()));
        if (YellowbackJson::has(statsJson, Stats::SUPPLY_CAP_CENTS)) {
            qint64 capC   = YellowbackJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS);
            qint64 supply = YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS);
            if (supply + cents > capC) {
                if (!softSupplyCap())   // a node from before W20: the cap is a ceiling
                    return tr("Minting %1 would exceed the supply cap (%2 of %3 in circulation).")
                            .arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(capC));
                // W20 (MINT-6 amended): above the cap only a class whose minimum ratio after the
                // volatility multiplier reaches the recapitalisation floor is accepted
                const qint64 sigma = YellowbackJson::toInt(statsJson, Stats::SIGMA_MULT_BPS, 10000);
                QStringList above;
                for (const auto& c : termClasses()) if (c.baseRatioBps * sigma / 10000 >= recap) above << c.name;
                const bool ok = termClass.isEmpty() ? !above.isEmpty() : above.contains(termClass);
                if (!ok) {
                    const QString what = termClass.isEmpty() ? tr("Minting %1").arg(YellowbackFormat::cents(cents))
                                                             : tr("Minting %1 in class %2").arg(YellowbackFormat::cents(cents)).arg(termClass);
                    if (above.isEmpty())
                        return tr("%1 would take YED in circulation above the supply cap (%2 of %3), and no term class reaches the %4 floor a mint above the cap needs.")
                                .arg(what).arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(capC)).arg(YellowbackFormat::bpsAsPercent(recap));
                    return tr("%1 would take YED in circulation above the supply cap (%2 of %3): above the cap only class %4 can mint, "
                              "because its minimum ratio reaches the %5 floor. %6")
                            .arg(what).arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(capC))
                            .arg(above.join(tr(" or "))).arg(YellowbackFormat::bpsAsPercent(recap))
                            .arg(capC - supply >= minMintCents()
                                 ? tr("Choose that lock length, or mint at most %1.").arg(YellowbackFormat::cents(capC - supply))
                                 : tr("Choose that lock length."));
                }
            }
        }
    }
    return QString();
}

// ── Typed calls ───────────────────────────────────────────────────────────────────────────

void YellowbackController::getNewAddress(OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETNEWADDRESS, json(nullptr), ok, err);
}

void YellowbackController::validateAddress(const QString& addr, OkFn ok, ErrFn err) {
    call(YellowbackRpc::VALIDATEADDRESS, json::array({addr.toStdString()}), ok, err);
}

void YellowbackController::estimateCollateral(qint64 cents, int lockBlocks, OkFn ok, ErrFn err) {
    call(YellowbackRpc::ESTIMATECOLLATERAL, json::array({cents, lockBlocks}), ok, err);
}

// yed_mint <cents> <lockBlocks> [from] [bundleHex] [wait]: the bundle is always built from the
// pool ("" = the default) and wait is false, so the reply follows the carrier broadcast (W7).
void YellowbackController::mint(qint64 cents, int lockBlocks, const QString& from, qint64 maxCollateralZat, OkFn ok, ErrFn err) {
    // yed_mint cents lockBlocks ( "from" "bundleHex" wait maxCollateralZat )
    call(YellowbackRpc::MINT, json::array({cents, lockBlocks, from.toStdString(), "", false, maxCollateralZat}), ok, err);
}

void YellowbackController::send(const QString& addr, qint64 cents, OkFn ok, ErrFn err) {
    call(YellowbackRpc::SEND, json::array({addr.toStdString(), cents}), ok, err);
}

void YellowbackController::redeem(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err) {
    if (to.isEmpty()) call(YellowbackRpc::REDEEM, json::array({vaultTxid.toStdString()}), ok, err);
    else              call(YellowbackRpc::REDEEM, json::array({vaultTxid.toStdString(), to.toStdString()}), ok, err);
}

// yed_claim <vaultTxid> [to] [bundleHex] [wait]: "" for the default destination, the bundle
// from the pool, wait false (as yed_mint).
void YellowbackController::claim(const QString& vaultTxid, const QString& to, qint64 minOutZat, qint64 maxBurnCents, OkFn ok, ErrFn err) {
    // yed_claim "vaultTxid" ( "to" "bundleHex" wait minOutZat maxBurnCents )
    call(YellowbackRpc::CLAIM, json::array({vaultTxid.toStdString(), to.toStdString(), "", false, minOutZat, maxBurnCents}), ok, err);
}

void YellowbackController::claimNotice(const QString& vaultTxid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::CLAIMNOTICE, json::array({vaultTxid.toStdString(), "", false}), ok, err);
}

void YellowbackController::sweepCarriers(OkFn ok, ErrFn err) {
    call(YellowbackRpc::SWEEPCARRIERS, json(nullptr), ok, err);
}

void YellowbackController::registerAttestor(const QString& bondYec, int lockBlocks, OkFn ok, ErrFn err) {
    call(YellowbackRpc::REGISTERATTESTOR, json::array({bondYec.toStdString(), lockBlocks}), ok, err);
}

void YellowbackController::withdrawBond(int seq, const QString& to, OkFn ok, ErrFn err) {
    if (to.isEmpty()) call(YellowbackRpc::WITHDRAWBOND, json::array({seq}), ok, err);
    else              call(YellowbackRpc::WITHDRAWBOND, json::array({seq, to.toStdString()}), ok, err);
}

void YellowbackController::heartbeat(const QString& memberKey, OkFn ok, ErrFn err) {
    const QString id = attestorSetId();
    if (id.isEmpty()) { if (err) err(tr("the node reports no YED attestor set")); return; }
    json params = json::array({id.toStdString()});
    if (!memberKey.isEmpty()) params.push_back(memberKey.toStdString());
    call(VaultRpc::SET_HEARTBEAT, params, ok, err);
}

void YellowbackController::releaseIntent(const QString& outpoint, const QString& recipientScript, OkFn ok, ErrFn err) {
    json params = json::array({outpoint.toStdString()});
    if (!recipientScript.isEmpty()) params.push_back(recipientScript.toStdString());
    call(VaultRpc::VAULT_RELEASE, params, ok, err);
}

void YellowbackController::buildCancel(const QString& outpoint, OkFn ok, ErrFn err) {
    call(VaultRpc::VAULT_BUILDCANCEL, json::array({outpoint.toStdString()}), ok, err);
}

void YellowbackController::signCancel(const QString& hex, OkFn ok, ErrFn err) {
    call(VaultRpc::SET_SIGNCANCEL, json::array({hex.toStdString()}), ok, err);
}

void YellowbackController::sendVaultTx(const QString& hex, OkFn ok, ErrFn err) {
    call(VaultRpc::VAULT_SEND, json::array({hex.toStdString()}), ok, err);
}

void YellowbackController::reportEquivocation(const QString& hexA, const QString& hexB, OkFn ok, ErrFn err) {
    call(YellowbackRpc::REPORTEQUIVOCATION, json::array({hexA.toStdString(), hexB.toStdString(), false}), ok, err);
}

// ── v3 two-step follow-up (W7) ────────────────────────────────────────────────────────────
// The rows of `type` present now are the baseline; the first row of that type not in it, and
// mined above the reference height (the main transaction spends a carrier that confirmed after
// R, so an older row is never it, even when the Transactions model was stale at the start), is
// the main transaction. The poll is a plain yed_listtransactions on a timer: the block that
// confirms the carrier is also the ChainTip on which the node builds the main transaction, so
// one or two polls after the main transaction is mined usually settle it. The timer is a child
// of this controller and dies with it.
//
// The match is by type and height, not by carrier: the node's yed_listtransactions rows carry
// no carrierTxid (ycash-dd src/rpc/yellowbackwallet.cpp yed_listtransactions), so two actions
// of one type pending at once (two mints, or one from another client of the same wallet) can
// be cross-attributed in the summary dialog. Plan A6 adds carrierTxid/mainTxid to the rows;
// this poll then matches on it (audit F-9).
//
// Two shapes the node's yed_listtransactions does not give as `type` (both node lines,
// ycash-dd and ycash6 src/rpc/yellowbackwallet.cpp yed_listtransactions):
//  - a claim of a vault this wallet owns is listed as "claimed" (the row closed an own vault),
//    so a claim accepts "claim" or "claimed";
//  - a CLAIM_NOTICE moves no YED and closes no vault, so it has no row at all (the contract's
//    "notice" type is not emitted). A notice is followed through yed_getnotice <vault> instead:
//    the record whose refHeight is the pending reply's is the one this action posted.
void YellowbackController::awaitPending(const QString& type, const QString& carrierTxid, int refHeight, OkFn done, ErrFn err,
                                        const QString& vaultTxid) {
    using namespace YellowbackRpc;
    auto known = std::make_shared<QSet<QString>>();
    auto accepts = [type](const QString& t) {
        return t == type || (type == Transaction::TYPE_CLAIM && t == Transaction::TYPE_CLAIMED);
    };
    for (int i = 0; i < transactions->rowCount(QModelIndex()); i++) {
        const YellowbackTx* t = transactions->txAt(i);
        if (t != nullptr && accepts(t->type)) known->insert(t->txid);
    }
    const bool byNotice = type == Transaction::TYPE_NOTICE && !vaultTxid.isEmpty();
    auto finished = std::make_shared<bool>(false);
    QTimer* timer = new QTimer(this);
    timer->setInterval(pendingPollMs);
    auto stop = [=]() {
        *finished = true;
        timer->stop();
        timer->deleteLater();
    };
    auto finish = [=, this](const QString& txid) {
        stop();
        call(GETTXINFO, json::array({txid.toStdString()}),
            [=](const json& info) { if (done) done(info); },
            [=, this](const QString& e) {
                // The transaction exists; the summary just has fewer fields
                json fallback = {{TxInfo::TXID, txid.toStdString()}, {TxInfo::TYPE, type.toStdString()}};
                log("yed_gettxinfo after " + type + ": " + e);
                if (done) done(fallback);
            });
    };
    auto lapsed = [=, this]() {
        if (*finished || refWindow() <= 0 || indexHeight <= refHeight + refWindow()) return false;
        stop();
        if (err) err(tr("the carrier %1 lapsed: the chain passed reference height %2 plus the %3-block window and no %4 transaction appeared. "
                        "Its funds come back with \"Reclaim lapsed carriers\" on the Settings page.")
                         .arg(carrierTxid).arg(refHeight).arg(refWindow()).arg(type));
        return true;
    };
    auto failedPoll = [=, this](const QString& what, const QString& e) {
        if (*finished) return;
        stop();
        if (err) err(tr("%1 failed while waiting for the %2 transaction: %3").arg(what).arg(type).arg(e));
    };
    auto poll = [=, this]() {
        if (*finished) return;
        if (byNotice) {
            call(GETNOTICE, json::array({vaultTxid.toStdString()}),
                [=, this](const json& r) {
                    if (*finished) return;
                    const QString txid = YellowbackJson::toStr(r, NoticeRecord::TXID);
                    if (YellowbackJson::toBool(r, NoticeRecord::FOUND) && !txid.isEmpty() &&
                        YellowbackJson::toInt(r, NoticeRecord::REF_HEIGHT, -1) == refHeight) {
                        finish(txid);
                        return;
                    }
                    lapsed();
                },
                [=](const QString& e) { failedPoll(GETNOTICE, e); });
            return;
        }
        call(LISTTRANSACTIONS, json::array({50, 0}),
            [=, this](const json& arr) {
                if (*finished) return;
                if (arr.is_array()) {
                    for (auto& it : arr) {
                        YellowbackTx t = YellowbackTx::fromJson(it);
                        if (!accepts(t.type) || t.expired || known->contains(t.txid)) continue;
                        if (t.height > 0 && t.height <= refHeight) continue;
                        applyTransactions(arr);
                        finish(t.txid);
                        return;
                    }
                }
                lapsed();
            },
            [=](const QString& e) { failedPoll(LISTTRANSACTIONS, e); });
    };
    QObject::connect(timer, &QTimer::timeout, this, poll);
    timer->start();
}

void YellowbackController::getTxInfo(const QString& txid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETTXINFO, json::array({txid.toStdString()}), ok, err);
}

void YellowbackController::getVault(const QString& txid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETVAULT, json::array({txid.toStdString()}), ok, err);
}

void YellowbackController::getFeePayee(int refHeight, qint64 collateralZat, OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETFEEPAYEE, json::array({refHeight, collateralZat}), ok, err);
}

YellowbackController::TermClass YellowbackController::classForLock(int lockBlocks) const {
    for (const auto& c : enabledClasses())
        if (lockBlocks >= c.minBlocks && lockBlocks <= c.maxBlocks) return c;
    return TermClass();
}

QList<YellowbackController::TermClass> YellowbackController::enabledClasses() const {
    QList<TermClass> out;
    for (const auto& c : termClasses()) if (c.enabled()) out.append(c);
    return out;
}

// ── Hardening H-1 / H-5 ───────────────────────────────────────────────────────────────────

bool YellowbackController::mintRequiresArmed() const {
    return YellowbackJson::toBool(infoJson, YellowbackRpc::Info::MINT_REQUIRES_ARMED, false);
}

bool YellowbackController::isArmed() const {
    return YellowbackJson::toBool(attest(), YellowbackRpc::Attest::ARMED, false);
}

QString YellowbackController::mintClassNote() const {
    QStringList lines;
    const auto all = termClasses();
    const auto on  = enabledClasses();
    if (!all.isEmpty() && on.size() < all.size()) {
        QStringList offered, disabled;
        for (const auto& c : all) {
            if (c.enabled())
                offered << tr("class %1 (lock %2–%3 blocks, about %4–%5 days, base ratio %6)").arg(c.name).arg(c.minBlocks).arg(c.maxBlocks)
                               .arg((qint64)c.minBlocks * YellowbackRpc::SECONDS_PER_BLOCK / 86400)
                               .arg((qint64)c.maxBlocks * YellowbackRpc::SECONDS_PER_BLOCK / 86400)
                               .arg(YellowbackFormat::bpsAsPercent(c.baseRatioBps));
            else
                disabled << c.name;
        }
        lines << (offered.isEmpty()
                  ? tr("No term class is enabled on this network: minting is not offered.")
                  : tr("Only %1 can be minted on this network: class %2 %3 disabled in this release's parameters.")
                        .arg(offered.join(tr(" or "))).arg(disabled.join(tr(" and "))).arg(disabled.size() == 1 ? tr("is") : tr("are")));
    }
    if (mintRequiresArmed())
        lines << (isArmed()
                  ? tr("Every mint on this network needs the attestation layer ARMED (it is): a mint priced without the attestors' bound would be VOID.")
                  : tr("Every mint on this network needs the attestation layer ARMED, and it is not (%1): minting waits for it, because a mint without an attested price would be VOID (mint-halted-unarmed).")
                        .arg(describeAttest(attest())));
    return lines.join(" ");
}

// ── Hardening H-9.3: local recomputation ──────────────────────────────────────────────────

static constexpr qint64 ZAT_PER_YEC = 100000000;

qint64 YellowbackController::feeZatFor(const json& params, qint64 collateralZat) {
    using namespace YellowbackRpc;
    const qint64 feeMin = YellowbackJson::toInt(params, Params::FEE_MIN_ZAT, 0);
    const qint64 feeBps = std::max<qint64>(0, YellowbackJson::toInt(params, Params::FEE_BPS, 0));
    const __int128 f = (__int128)std::max<qint64>(0, collateralZat) * feeBps / 10000;
    return std::max<qint64>(feeMin, (qint64)f);
}

qint64 YellowbackController::attestFeeZatFor(const json& params, qint64 feeZat) {
    using namespace YellowbackRpc;
    const qint64 bps = YellowbackJson::toInt(YellowbackJson::obj(params, Params::ATTEST), ParamsAttest::ATTEST_FEE_BPS, 0);
    if (feeZat <= 0 || bps <= 0) return 0;
    return (qint64)((__int128)feeZat * bps / 10000);
}

qint64 YellowbackController::requiredZatFor(qint64 cents, qint64 minRatioBps, qint64 pMint) {
    if (cents <= 0 || minRatioBps <= 0 || pMint <= 0) return -1;
    const __int128 num = (__int128)cents * minRatioBps * ZAT_PER_YEC;
    __int128 r = (num + pMint - 1) / pMint;
    if (r % 1000 != 0) r += 1000 - r % 1000;
    if (r > (__int128)21000000 * ZAT_PER_YEC) return -1;    // MAX_MONEY (K14): mint-unsatisfiable
    return (qint64)r;
}

qint64 YellowbackController::mintCollateralZatFor(const json& params, qint64 requiredZat) {
    if (requiredZat < 0) return -1;
    qint64 c = std::max(requiredZat, 4 * YellowbackJson::toInt(params, YellowbackRpc::Params::FEE_MIN_ZAT, 0));
    if (c % 1000 != 0) c += 1000 - c % 1000;
    return c;
}

QStringList YellowbackController::checkEstimate(const json& params, int indexHeight, qint64 cents, int lockBlocks, const json& e) {
    using namespace YellowbackRpc;
    QStringList bad;
    auto mismatch = [&](const QString& what, qint64 node, qint64 local) {
        if (node != local) bad << tr("%1 is %2 where the parameters give %3").arg(what).arg(node).arg(local);
    };
    const int refLag = (int)YellowbackJson::toInt(params, Params::REF_LAG, 0);
    const int grace  = (int)YellowbackJson::toInt(params, Params::GRACE, 0);
    const int ref    = (int)YellowbackJson::toInt(e, Estimate::REF_HEIGHT, -1);
    const int refNow = std::max(0, indexHeight - refLag);
    // the node's tip may be one block past the wallet's last yed_getinfo: allow refNow + 1, never more
    if (ref != refNow && ref != refNow + 1)
        bad << tr("refHeight is %1 where the tip %2 less REF_LAG %3 gives %4").arg(ref).arg(indexHeight).arg(refLag).arg(refNow);
    const qint64 lock = YellowbackJson::toInt(e, Estimate::LOCK_HEIGHT, -1);
    mismatch(tr("lockHeight"), lock, (qint64)ref + lockBlocks);
    mismatch(tr("claimHeight"), YellowbackJson::toInt(e, Estimate::CLAIM_HEIGHT, -1), lock + grace);
    // the class and its ratio, from params.classes (enabled rows only, H-5)
    TermClass cls;
    if (params.is_object() && params.find(Params::CLASSES) != params.end() && params[Params::CLASSES].is_array())
        for (auto& c : params[Params::CLASSES]) {
            TermClass t;
            t.name = YellowbackJson::toStr(c, ParamClass::CLASS);
            t.minBlocks = (int)YellowbackJson::toInt(c, ParamClass::MIN_BLOCKS);
            t.maxBlocks = (int)YellowbackJson::toInt(c, ParamClass::MAX_BLOCKS);
            t.baseRatioBps = YellowbackJson::toInt(c, ParamClass::BASE_RATIO_BPS);
            if (t.enabled() && lockBlocks >= t.minBlocks && lockBlocks <= t.maxBlocks) { cls = t; break; }
        }
    if (cls.name.isEmpty()) {
        bad << tr("a lock of %1 blocks falls in no enabled term class").arg(lockBlocks);
        return bad;
    }
    if (YellowbackJson::toStr(e, Estimate::TERM_CLASS) != cls.name)
        bad << tr("termClass is %1 where a lock of %2 blocks is class %3").arg(YellowbackJson::toStr(e, Estimate::TERM_CLASS)).arg(lockBlocks).arg(cls.name);
    mismatch(tr("baseRatioBps"), YellowbackJson::toInt(e, Estimate::BASE_RATIO_BPS, -1), cls.baseRatioBps);
    const qint64 sigma = YellowbackJson::toInt(e, Estimate::SIGMA_MULT_BPS, -1);
    if (sigma < 10000) bad << tr("sigmaMultBps is %1, below its floor of 10000").arg(sigma);
    const qint64 minRatio = cls.baseRatioBps * sigma / 10000;
    mismatch(tr("minRatioBps"), YellowbackJson::toInt(e, Estimate::MIN_RATIO_BPS, -1), minRatio);
    if (!YellowbackJson::isNull(e, Estimate::REQUIRED_ZAT)) {
        const qint64 required = YellowbackJson::toInt(e, Estimate::REQUIRED_ZAT);
        mismatch(tr("requiredZat"), required, requiredZatFor(cents, minRatio, YellowbackJson::toInt(e, Estimate::P_MINT, -1)));
        // AFEE-1 on FEE-1 of the requirement, and only when a bundle was used (armed)
        if (YellowbackJson::has(e, Estimate::ATTEST_FEE_ZAT)) {
            const qint64 af = YellowbackJson::toInt(e, Estimate::ATTEST_FEE_ZAT);
            const bool bundle = !YellowbackJson::strings(e, Estimate::BUNDLE_SEQS).isEmpty() ||
                                (e.is_object() && e.find(Estimate::BUNDLE_SEQS) != e.end() && e[Estimate::BUNDLE_SEQS].is_array() && !e[Estimate::BUNDLE_SEQS].empty());
            mismatch(tr("attestFeeZat"), af, bundle ? attestFeeZatFor(params, feeZatFor(params, required)) : 0);
        }
    }
    return bad;
}

QStringList YellowbackController::checkClaimable(const json& params, int indexHeight, const YellowbackClaimable& c) {
    QStringList bad;
    const qint64 fee = feeZatFor(params, c.collateralZat);
    if (c.feeZat != fee)
        bad << tr("the enforcement fee is %1 where FEE-1 of the collateral gives %2").arg(YellowbackFormat::zec(c.feeZat)).arg(YellowbackFormat::zec(fee));
    const qint64 af = attestFeeZatFor(params, fee);
    if (c.attestFeeZat != 0 && c.attestFeeZat != af)
        bad << tr("the attestation fee is %1 where AFEE-1 gives %2").arg(YellowbackFormat::zec(c.attestFeeZat)).arg(YellowbackFormat::zec(af));
    // rpcversion 5 (U-23, finding 33): the claimant intent carries the collateral less the RED-5
    // residual; the fees come from the claimant's own YEC, not from the collateral
    if (c.residualZat < 0 || c.collateralZat - c.residualZat <= 0)
        bad << tr("the residual %1 leaves nothing of the collateral %2 for the claimant").arg(YellowbackFormat::zec(c.residualZat)).arg(YellowbackFormat::zec(c.collateralZat));
    if (indexHeight < c.claimHeight)
        bad << tr("the claim height %1 is not reached (the chain is at %2)").arg(c.claimHeight).arg(indexHeight);
    return bad;
}

QStringList YellowbackController::checkVaultHeights(const json& params, const YellowbackPosition& p) {
    QStringList bad;
    if (!params.is_object() || !YellowbackJson::has(params, YellowbackRpc::Params::GRACE)) return bad;
    const int grace = (int)YellowbackJson::toInt(params, YellowbackRpc::Params::GRACE);
    if (p.claimHeight != p.lockHeight + grace)
        bad << tr("its claim height %1 is not its lock height %2 plus GRACE %3").arg(p.claimHeight).arg(p.lockHeight).arg(grace);
    if (p.lockHeight <= p.refHeight)
        bad << tr("its lock height %1 is not after its reference height %2").arg(p.lockHeight).arg(p.refHeight);
    return bad;
}

// ── Hardening H-9.2: deadlines ────────────────────────────────────────────────────────────

QString YellowbackController::deadlineWarning(const YellowbackPosition& p, int height) {
    using namespace YellowbackRpc::Position;
    if (height > 0 && p.status == STATUS_CLAIMING) {
        // rpcversion 5: an own vault under a claim intent; it is the attestors' to stop, not the owner's
        const YellowbackIntent* c = p.claimantIntent();
        if (c != nullptr && height + 1 < c->releaseHeight)
            return tr("Vault %1 is being claimed (claim at height %2): its collateral goes to the claimant at height %3 unless a member of the attestor set cancels the claim as made at a wrong price.")
                .arg(p.txid.left(12) % "…").arg(c->height).arg(c->releaseHeight);
        return QString();
    }
    if (height <= 0 || (p.status != STATUS_ACTIVE && p.status != STATUS_VOID)) return QString();
    if (height < p.claimHeight - BLOCKS_PER_DAY) return QString();
    const QString vault = p.txid.left(12) % "…";
    const QString when = YellowbackFormat::estimateDate(p.claimHeight, height).toString("yyyy-MM-dd HH:mm");
    if (p.status == STATUS_VOID) {
        if (height >= p.claimHeight)
            return tr("VOID vault %1 is past its claim height %2: anyone may take its collateral by the claim path. Release it now.").arg(vault).arg(p.claimHeight);
        return tr("VOID vault %1 reaches its claim height %2 (~%3, in %4): release its collateral before then.")
            .arg(vault).arg(p.claimHeight).arg(when).arg(YellowbackFormat::blocksAndDuration(p.claimHeight - height));
    }
    if (height >= p.claimHeight)
        return tr("Vault %1 is past its claim height %2: anyone may claim its collateral by burning its debt once it is underwater. Redeem or renew it now.")
            .arg(vault).arg(p.claimHeight);
    return tr("Vault %1 reaches its claim height %2 (~%3, in %4): redeem or renew it before then. After it, anyone may claim the collateral by burning its debt once it is underwater.")
        .arg(vault).arg(p.claimHeight).arg(when).arg(YellowbackFormat::blocksAndDuration(p.claimHeight - height));
}

QStringList YellowbackController::deadlineWarnings() const {
    QStringList out;
    for (int i = 0; ; i++) {
        const YellowbackPosition* p = positions->positionAt(i);
        if (p == nullptr) break;
        QString w = deadlineWarning(*p, indexHeight);
        if (!w.isEmpty()) out << w;
    }
    return out;
}

int YellowbackController::renewLockBlocks(const YellowbackPosition& p) const {
    const int own = p.lockHeight - p.refHeight;
    if (own > 0 && !classForLock(own).name.isEmpty()) return own;
    const auto on = enabledClasses();
    return on.isEmpty() ? 0 : on.first().minBlocks;
}

// ── Error identifiers (§4.5) ──────────────────────────────────────────────────────────────

QString YellowbackController::explainError(const QString& e) {
    using namespace YellowbackRpc;
    auto is = [&](const char* id) { return e.startsWith(id, Qt::CaseInsensitive); };
    if (isMethodNotFound(e))
        return tr("The connected node does not offer this command. It needs a ycashd release with the vault upgrade and this Yellowback RPC, on a network whose YED attestor set is configured.");
    if (is(Errors::INDEX_UNHEALTHY))
        return tr("The node's Yellowback index is unhealthy; restart ycashd with -reindex-yellowback.");
    if (is(Errors::CHANGE_FLOOR))
        return tr("The YED change left over would be below the minimum output; the node's message names the amounts that work.");
    if (is(Errors::NOT_YELLOWBACK_ADDRESS))
        return tr("The recipient is not a Yellowback address of this network.");
    if (is(Errors::INSUFFICIENT_YED))
        return tr("Your confirmed YED is below what this transaction needs to burn or send.");
    if (is(Errors::VAULT_LOCKED))
        return tr("The vault's lock height has not been reached; the collateral cannot leave it before then (a rule every Ycash node enforces).");
    if (is(Errors::VAULT_NOT_ACTIVE))
        return tr("The vault is already closed or claimed; there is nothing left to release.");
    if (is(Errors::VAULT_NOT_OWNED))
        return tr("This wallet does not hold the owner key of that vault.");
    if (is(Errors::VAULT_NOT_FOUND))
        return tr("The node's index knows no vault by that txid.");
    if (is(Errors::CLAIM_NOT_YET))
        return tr("The vault's claim height has not been reached.");
    if (is(Errors::CLAIM_NOT_UNDERWATER))
        return tr("At the claim price the vault is not underwater, so the claim would be an invalid transaction under the Yellowback consensus rules.");
    if (is(Errors::MINTPOL_NOT_ACTIVE))
        return tr("The vault upgrade is not active at the reference height: minting waits for it.");
    if (is(Errors::MINTPOL_NO_PRICE))
        return tr("No mint price is defined at the reference height: too few recent blocks carry a price quote.");
    if (is(Errors::MINTPOL_GLOBAL_RATIO))
        return tr("Minting is paused: the network's overall collateral ratio is too low.");
    if (is(Errors::MINTPOL_DIVERGENCE))
        return tr("Minting is paused: the fast and slow price medians diverge too much.");
    if (is(Errors::MINTPOL_CAP)) {
        // W20: a node with the soft cap names the classes that can still mint above it
        if (e.contains("above the cap only"))
            return tr("This mint would take YED in circulation above the supply cap, and its class does not reach the floor a mint above the cap needs. "
                      "Choose the lock length of a class the node names, or a smaller amount.");
        return tr("Minting is paused: the supply cap is reached.");
    }
    if (is(Errors::MINT_UNSATISFIABLE))
        return tr("The collateral this mint needs exceeds the maximum amount of YEC.");
    if (is(Errors::MINTPOL_UNARMED))
        return tr("Minting is paused: this network requires the attestation layer to be ARMED for every mint, and it is not at the reference height. Nothing was sent.");
    if (is(Errors::MINT_BAD_LOCK))
        return tr("The lock length falls in no enabled term class (on mainnet and testnet only class A is enabled; B and C are disabled).");
    if (is(Errors::CLAIM_BURN_ABOVE_MAX))
        return tr("The claim would burn more YED than the vault's debt you confirmed, so nothing was sent. Re-check the claimable list and confirm again.");
    if (is(Errors::COLLATERAL_ABOVE_MAX))
        return tr("The price moved: at the node's reference height the mint would lock more YEC than the amount you confirmed, so nothing was sent. Re-estimate and confirm again.");
    if (is(Errors::CLAIM_OUT_BELOW_MIN))
        return tr("The price moved: at the node's reference height the claim would pay out less YEC than the amount you confirmed, so nothing was sent. Re-check the claimable list and confirm again.");
    if (is(Errors::MEMPOOL_CHECK_FAILED))
        return tr("The node's own check of the Yellowback consensus rules refused the transaction (it would be invalid), so nothing was signed or sent.");
    if (is(Errors::BAD_ADDRESS))
        return tr("A claim pays a claim intent, whose recipient must be a transparent address; claim to a transparent address and shield the YEC afterwards.");
    if (is(RpcErrors::REGISTER_NEEDS_ADMISSION))
        return tr("The attestor set is not open: the join was signed by the new member key only and needs admission signatures from current members. "
                  "The node's message carries the partly signed transaction for set_signact and set_sendact.");
    if (is(RpcErrors::NO_ATTESTOR_SET))
        return tr("This network has no YED attestor set configured.");
    // v3
    if (is(Errors::BUNDLE_INSUFFICIENT))
        return tr("Too few of the selected attestors have a fresh attestation in this node's pool. The subscriber fills the pool (Settings); nothing was sent.");
    if (is(Errors::MINT10_DIVERGED))
        return tr("The pools' and the attestors' prices disagree by more than the allowed margin, so minting is paused; nothing was sent.");
    if (is(Errors::BUNDLE_MALFORMED))
        return tr("The bundle given to the node is not a bundle.");
    if (is(Errors::INSUFFICIENT_YEC))
        return tr("The wallet cannot cover the collateral, the carrier and the fees from confirmed, unlocked YEC.");
    if (is(Errors::NOTICE_STANDING))
        return tr("A claim notice already stands against this vault; a second one cannot reset its clock.");
    if (is(Errors::NOTICE_NOT_UNDERWATER))
        return tr("Under this node's attested prices the vault is not below the emergency ratio (or the layer is not armed), so a notice would be meaningless.");
    if (is(Errors::BOND_BELOW_MIN))
        return tr("The bond is below the minimum an attestor must post.");
    if (is(Errors::LOCK_BELOW_MIN))
        return tr("The bond lock is shorter than the minimum (or its locktime is too far out).");
    if (is(Errors::BOND_LOCKED))
        return tr("The bond's locktime has not passed; the bond cannot be withdrawn before it (every Ycash node enforces that lock).");
    if (is(Errors::BOND_SPENT))
        return tr("The bond has already been withdrawn.");
    if (is(Errors::NOT_EQUIVOCATION))
        return tr("The two attestations are not an equivocation: they must be the same attestor, the same cited height, different prices, both validly signed over this chain's block hash.");
    if (is(Errors::ATTEST_KEY_NOT_HELD))
        return tr("This wallet does not hold the key this attestor action needs (the member key: since P4-b it is the hot key and the bond key at once).");
    if (is(Errors::ATTEST_UNKNOWN_SEQ))
        return tr("The node knows no attestor with that sequence number (a join counts once its transaction confirms).");
    if (is(Errors::ATTEST_MALFORMED))
        return tr("An attestation is 74 bytes (148 characters of hex).");
    if (is(Errors::ATTEST_RANGE))
        return tr("The price is outside the range an attestation may carry.");
    if (is(Errors::EQUIVOCATION_GUARD))
        return tr("This node already signed a different price for that height; signing another would be an equivocation, so it refused.");
    if (e.contains(RpcErrors::WALLET_LOCKED))
        return tr("Unlock the wallet first (walletpassphrase in the console tab).");
    return QString();
}

bool YellowbackController::parseBundleInsufficient(const QString& e, int* count, int* selected, QList<int>* missing) {
    if (!e.startsWith(YellowbackRpc::Errors::BUNDLE_INSUFFICIENT, Qt::CaseInsensitive)) return false;
    static const QRegularExpression re("(\\d+) of (\\d+) selected attestors");
    auto m = re.match(e);
    if (count)    *count    = m.hasMatch() ? m.captured(1).toInt() : 0;
    if (selected) *selected = m.hasMatch() ? m.captured(2).toInt() : 0;
    if (missing) {
        missing->clear();
        static const QRegularExpression seqs("missing seq ([0-9, ]+)");
        auto ms = seqs.match(e);
        if (ms.hasMatch())
            for (const QString& n : ms.captured(1).split(',', Qt::SkipEmptyParts))
                if (!n.trimmed().isEmpty()) missing->append(n.trimmed().toInt());
    }
    return true;
}

bool YellowbackController::parseChangeFloor(const QString& e, qint64* allCents, qint64* atMost) {
    // "change-floor: ... send 12345 cents (all selected inputs) or at most 12245 cents"; the
    // prototype's C20 wording carried the same two figures in the same order.
    static const QRegularExpression re("send (\\d+) cents.*?or at most (\\d+) cents");
    auto m = re.match(e);
    if (!m.hasMatch()) return false;
    if (allCents) *allCents = m.captured(1).toLongLong();
    if (atMost)   *atMost   = m.captured(2).toLongLong();
    return true;
}
