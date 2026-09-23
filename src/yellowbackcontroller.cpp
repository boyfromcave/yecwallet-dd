#include "yellowbackcontroller.h"

#include <algorithm>
#include "yellowbackrpc.h"
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
                setAvailability(false, tr("the connected ycashd has no Yellowback RPCs (%1). Add 'experimentalfeatures=1' and 'yellowback=1' to its ycash.conf and restart it.").arg(e));
                if (!confRepairOffered) {
                    confRepairOffered = true;
                    auto conn = connection();
                    if (conn != nullptr && conn->offerYellowbackConfRepair()) {
                        QMessageBox::information(main, tr("ycash.conf updated"),
                            tr("The two lines were added. Restart ycashd (or YecWallet, if it runs the embedded node) to enable Yellowback."));
                    }
                }
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
        setAvailability(false, tr("Yellowback is not enabled on this ycashd. Add 'experimentalfeatures=1' and 'yellowback=1' to ycash.conf and restart it."));
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
    if (!available || statsJson.empty() || activationJson.empty()) return std::nullopt;
    if (YellowbackJson::toStr(activationJson, Activation::STATUS) != Activation::STATUS_ACTIVE) return std::nullopt;
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

bool YellowbackController::isAbandoned() const { return YellowbackJson::toBool(infoJson, YellowbackRpc::Info::ABANDONED); }
bool YellowbackController::isEnforcing() const { return YellowbackJson::toBool(infoJson, YellowbackRpc::Info::ENFORCING); }

YellowbackStatus YellowbackController::status() const {
    return describeStatus(available, reason, infoJson, statsJson, activationJson);
}

// Plan §4.8, banner row. Every line is derived from contract fields only; the wording of what
// enforcement means follows §8.1: a majority of hashpower that runs the module makes the rules
// hold — a description of who enforces, never a guarantee.
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

    const json& act = YellowbackJson::obj(info, Info::ACTIVATION);
    QString   status  = YellowbackJson::toStr(act, Activation::STATUS);
    qint64    sigCount = YellowbackJson::toInt(act, Activation::SIGNAL_COUNT);
    qint64    window  = YellowbackJson::toInt(act, Activation::WINDOW);
    bool      enforcing   = YellowbackJson::toBool(info, Info::ENFORCING);
    bool      valve       = YellowbackJson::toBool(info, Info::VALVE_TRIPPED);
    bool      sunset      = YellowbackJson::toBool(info, Info::SUNSET);
    bool      abandoned   = YellowbackJson::toBool(info, Info::ABANDONED);
    qint64    suppressed  = YellowbackJson::toInt(info, Info::SUPPRESSED_BLOCKS);
    qint64    rejected    = YellowbackJson::toInt(info, Info::REJECTED_BLOCKS);
    QStringList halts     = YellowbackJson::strings(stats, Stats::HALT_MASK);
    // yed_getactivation carries the suspension flag and the thresholds; yed_getstats.haltMask
    // carries ENFORCEMENT. Either source is enough: the first to answer wins, both agree.
    bool suspended = YellowbackJson::toBool(activation, ActivationInfo::ENFORCEMENT_SUSPENDED) ||
                     halts.contains(Stats::HALT_ENFORCEMENT);
    bool participationHalt = halts.contains(Stats::HALT_PARTICIPATION) ||
                             (status == Activation::STATUS_ACTIVE && YellowbackJson::toBool(activation, ActivationInfo::MINT_HALTED));
    qint64 threshold = YellowbackJson::toInt(activation, ActivationInfo::THRESHOLD, -1);

    // Headline: activation state
    QString signalText = window > 0 ? tr("%1/%2 of recent blocks signalling").arg(sigCount).arg(window) : QString();
    if (status == Activation::STATUS_SIGNALING) {
        st.headline = tr("Yellowback is signalling: %1%2. Minting opens only after activation.")
            .arg(signalText.isEmpty() ? tr("waiting for pool signals") : signalText)
            .arg(threshold > 0 ? tr(" (needs %1)").arg(threshold) : QString());
    } else if (status == Activation::STATUS_LOCKED_IN) {
        st.headline = tr("Yellowback is locked in; it activates at height %1.")
            .arg(YellowbackJson::toInt(act, Activation::ACTIVATE_HEIGHT));
    } else if (status == Activation::STATUS_ACTIVE) {
        st.headline = tr("Yellowback is active since height %1; %2.")
            .arg(YellowbackJson::toInt(act, Activation::ACTIVATE_HEIGHT))
            .arg(signalText.isEmpty() ? tr("enforced by the pools that run the module") : signalText);
    } else {
        st.headline = tr("Yellowback activation state: %1.").arg(status.isEmpty() ? tr("unknown") : status);
    }

    // Warnings, most severe first
    if (abandoned)
        st.warnings << tr("Enforcement abandoned: fewer than half of blocks have signalled for two full windows. "
                          "Nobody polices vault spends; after its claim height any vault can be emptied by anyone. "
                          "Sweep your collateral before then (see Vaults).");
    if (valve)
        st.warnings << tr("This node's work valve tripped: it rejected a block the rest of the network built on, "
                          "so it stopped enforcing and rejoined the network's chain. Restart the node to re-arm it, "
                          "after checking why the network did not follow (yed_getblockverdict).");
    if (sunset)
        st.warnings << tr("Enforcement sunset: this node's release enforces only until a fixed height, which has passed. "
                          "It keeps accounting but rejects nothing; upgrade the node.");
    if (suspended && !abandoned)
        st.warnings << tr("Enforcement suspended: fewer than half of recent blocks signal, so block rejection is paused "
                          "and vault spends are not policed until 60 % signal again. Minting is paused too.");
    else if (participationHalt)
        st.warnings << tr("Participation halt: fewer than 60 % of recent blocks signal enforcement, so minting is paused "
                          "until 75 % do. Existing YED stays redeemable.");
    if (status == Activation::STATUS_ACTIVE && !enforcing && !valve && !sunset)
        st.warnings << tr("This node is not enforcing (yellowbackenforce=0 or the index is unhealthy). "
                          "It still accounts and quotes; the network's rules depend on the pools that do enforce.");

    // Information lines
    if (suppressed > 0)
        st.notes << tr("%1 rule-breaking block(s) were accepted because the network had already built on them "
                       "(catch-up after an outage); enforcement is still on.").arg(suppressed);
    if (rejected > 0)
        st.notes << tr("This node has rejected %1 block(s) for vault-spend rule violations.").arg(rejected);
    // H10: the node's own report of the coin locks that keep an ordinary send from burning YED
    if (!YellowbackJson::toBool(info, Info::PROTECTED_BY_INDEX, true))
        st.warnings << tr("This node is not holding your YED outputs locked, so an ordinary YEC send could spend one and burn the YED it carries. "
                          "Check that the node runs with yellowback enabled.");
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
        else if (kind == Miner::KIND_SIGNAL)
            line += tr("next tag signals without a quote (no fresh quote from the agent)");
        else
            line += tr("next block carries no tag");
        line += YellowbackJson::toBool(miner, Miner::SIGNAL) ? tr("; signalling") : tr("; not signalling");
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
qint64 YellowbackController::maxMintCents() const   { return YellowbackRpc::MAX_MINT_CENTS; }
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

// The one halt a mint can pass (W16): GLOBAL_RATIO alone, with at least one class at or above
// the recapitalisation floor. Every other halt, and the cap, still stop every mint.
static bool recapOnly(const json& stats, const QStringList& mintable) {
    using namespace YellowbackRpc;
    QStringList halts = YellowbackJson::strings(stats, Stats::HALT_MASK);
    return halts.size() == 1 && halts.first() == Stats::HALT_GLOBAL_RATIO && !mintable.isEmpty();
}

QString YellowbackController::mintLimit() const {
    using namespace YellowbackRpc;
    if (!available || statsJson.empty()) return QString();
    const QStringList mintable = mintableClasses();
    if (!recapOnly(statsJson, mintable)) return QString();
    return tr("Minting is limited: the system-wide collateral ratio is %1, below its %2 floor. "
              "Only class %3 can mint until it recovers, because its minimum ratio reaches the %4 recapitalisation floor; "
              "every such mint raises the ratio.")
        .arg(YellowbackJson::isNull(statsJson, Stats::GLOBAL_RATIO_BPS) ? tr("undefined") : YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(statsJson, Stats::GLOBAL_RATIO_BPS)))
        .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::GLOBAL_RATIO_HALT_BPS, 25000)))
        .arg(mintable.join(tr(" or ")))
        .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::RECAP_RATIO_BPS, 50000)));
}

QString YellowbackController::mintBlocker(qint64 cents, const QString& termClass) const {
    using namespace YellowbackRpc;
    if (!available) return reason;
    if (statsJson.empty()) return tr("Waiting for yed_getstats.");

    const QStringList mintable = mintableClasses();
    const bool limited = recapOnly(statsJson, mintable);
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
            const qint64 recap = YellowbackJson::toInt(paramsJson, Params::RECAP_RATIO_BPS, 50000);
            QStringList recapClasses;
            for (const auto& c : termClasses()) if (c.baseRatioBps >= recap) recapClasses << c.name;
            lines << tr("The global ratio recovers as the price rises or as vaults are redeemed and claimed; once it is the only halt left, class %1 can mint again and each such mint raises it.")
                         .arg(recapClasses.isEmpty() ? QString("A") : recapClasses.join(tr(" or ")));
        }
        return tr("Minting is paused. ") % lines.join(" ");
    }
    if (limited && !termClass.isEmpty() && !mintable.contains(termClass)) {
        return tr("Class %1 cannot mint while the system-wide collateral ratio (%2) is below its %3 floor: only class %4 can, "
                  "because its minimum ratio reaches the %5 recapitalisation floor. Choose that lock length, or wait for the ratio to recover.")
            .arg(termClass)
            .arg(YellowbackJson::isNull(statsJson, Stats::GLOBAL_RATIO_BPS) ? tr("undefined") : YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(statsJson, Stats::GLOBAL_RATIO_BPS)))
            .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::GLOBAL_RATIO_HALT_BPS, 25000)))
            .arg(mintable.join(tr(" or ")))
            .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(paramsJson, Params::RECAP_RATIO_BPS, 50000)));
    }
    if (!limited && !YellowbackJson::toBool(statsJson, Stats::MINTING_ALLOWED, true)) {
        // No halt bit, yet not allowed: the supply cap has no room (mintpol-cap)
        if (YellowbackJson::has(statsJson, Stats::SUPPLY_CAP_CENTS))
            return tr("Minting is paused: the supply cap (%1) is reached with %2 in circulation.")
                    .arg(YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS)))
                    .arg(YellowbackFormat::cents(YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS)));
        return tr("Minting is paused (yed_getstats reports mintingAllowed = false).");
    }

    if (cents > 0) {
        if (cents < minMintCents() || cents > maxMintCents())
            return tr("A mint must be between %1 and %2.")
                    .arg(YellowbackFormat::cents(minMintCents())).arg(YellowbackFormat::cents(maxMintCents()));
        if (YellowbackJson::has(statsJson, Stats::SUPPLY_CAP_CENTS)) {
            qint64 cap    = YellowbackJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS);
            qint64 supply = YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS);
            if (supply + cents > cap)
                return tr("Minting %1 would exceed the supply cap (%2 of %3 in circulation).")
                        .arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(cap));
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
void YellowbackController::mint(qint64 cents, int lockBlocks, const QString& from, OkFn ok, ErrFn err) {
    call(YellowbackRpc::MINT, json::array({cents, lockBlocks, from.toStdString(), "", false}), ok, err);
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
void YellowbackController::claim(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err) {
    call(YellowbackRpc::CLAIM, json::array({vaultTxid.toStdString(), to.toStdString(), "", false}), ok, err);
}

void YellowbackController::claimNotice(const QString& vaultTxid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::CLAIMNOTICE, json::array({vaultTxid.toStdString(), "", false}), ok, err);
}

void YellowbackController::sweepCarriers(OkFn ok, ErrFn err) {
    call(YellowbackRpc::SWEEPCARRIERS, json(nullptr), ok, err);
}

void YellowbackController::registerAttestor(double bondYec, int lockBlocks, int flags, OkFn ok, ErrFn err) {
    call(YellowbackRpc::REGISTERATTESTOR, json::array({bondYec, lockBlocks, flags}), ok, err);
}

void YellowbackController::withdrawBond(int seq, const QString& to, OkFn ok, ErrFn err) {
    if (to.isEmpty()) call(YellowbackRpc::WITHDRAWBOND, json::array({seq}), ok, err);
    else              call(YellowbackRpc::WITHDRAWBOND, json::array({seq, to.toStdString()}), ok, err);
}

void YellowbackController::revive(int seq, qint64 priceMicroUsd, OkFn ok, ErrFn err) {
    call(YellowbackRpc::REVIVE, json::array({seq, priceMicroUsd}), ok, err);
}

void YellowbackController::reportEquivocation(const QString& hexA, const QString& hexB, OkFn ok, ErrFn err) {
    call(YellowbackRpc::REPORTEQUIVOCATION, json::array({hexA.toStdString(), hexB.toStdString(), false}), ok, err);
}

// ── v3 two-step follow-up (W7) ────────────────────────────────────────────────────────────
// The rows of `type` present now are the baseline; the first row of that type not in it is
// the main transaction. The poll is a plain yed_listtransactions on a timer: the block that
// confirms the carrier is also the ChainTip on which the node builds the main transaction, so
// one or two polls after the next block usually settle it. The timer is a child of this
// controller and dies with it.
void YellowbackController::awaitPending(const QString& type, const QString& carrierTxid, int refHeight, OkFn done, ErrFn err) {
    auto known = std::make_shared<QSet<QString>>();
    for (int i = 0; i < transactions->rowCount(QModelIndex()); i++) {
        const YellowbackTx* t = transactions->txAt(i);
        if (t != nullptr && t->type == type) known->insert(t->txid);
    }
    QTimer* timer = new QTimer(this);
    timer->setInterval(pendingPollMs);
    auto poll = [=, this]() {
        call(YellowbackRpc::LISTTRANSACTIONS, json::array({50, 0}),
            [=, this](const json& arr) {
                if (arr.is_array()) {
                    for (auto& it : arr) {
                        YellowbackTx t = YellowbackTx::fromJson(it);
                        if (t.type != type || t.expired || known->contains(t.txid)) continue;
                        timer->stop();
                        timer->deleteLater();
                        applyTransactions(arr);
                        call(YellowbackRpc::GETTXINFO, json::array({t.txid.toStdString()}),
                            [=](const json& info) { if (done) done(info); },
                            [=](const QString& e) {
                                // The row exists; the summary just has fewer fields
                                json fallback = {{YellowbackRpc::TxInfo::TXID, t.txid.toStdString()}, {YellowbackRpc::TxInfo::TYPE, type.toStdString()}};
                                log("yed_gettxinfo after " + type + ": " + e);
                                if (done) done(fallback);
                            });
                        return;
                    }
                }
                if (refWindow() > 0 && indexHeight > refHeight + refWindow()) {
                    timer->stop();
                    timer->deleteLater();
                    if (err) err(tr("the carrier %1 lapsed: the chain passed reference height %2 plus the %3-block window and no %4 transaction appeared. "
                                    "Its funds come back with \"Reclaim lapsed carriers\" on the Settings page.")
                                     .arg(carrierTxid).arg(refHeight).arg(refWindow()).arg(type));
                }
            },
            [=, this](const QString& e) {
                timer->stop();
                timer->deleteLater();
                if (err) err(tr("yed_listtransactions failed while waiting for the %1 transaction: %2").arg(type).arg(e));
            });
    };
    QObject::connect(timer, &QTimer::timeout, this, poll);
    timer->start();
}

void YellowbackController::sweep(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err) {
    json params = json::array({vaultTxid.toStdString(), YellowbackRpc::SWEEP_ACKNOWLEDGEMENT});
    if (!to.isEmpty()) params.push_back(to.toStdString());
    call(YellowbackRpc::SWEEP, params, ok, err);
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
    for (const auto& c : termClasses())
        if (lockBlocks >= c.minBlocks && lockBlocks <= c.maxBlocks) return c;
    return TermClass();
}

// ── Error identifiers (§4.5) ──────────────────────────────────────────────────────────────

QString YellowbackController::explainError(const QString& e) {
    using namespace YellowbackRpc;
    auto is = [&](const char* id) { return e.startsWith(id, Qt::CaseInsensitive); };
    if (isMethodNotFound(e))
        return tr("The connected node does not offer this command. It needs a ycashd release with this Yellowback RPC, started with experimentalfeatures=1 and yellowback=1.");
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
    if (is(Errors::SWEEP_NOT_ABANDONED))
        return tr("The chain does not show abandonment: enforcement is on, or has been suspended for fewer than the abandonment window of blocks. Redeem instead.");
    if (is(Errors::SWEEP_ACKNOWLEDGEMENT_MISSING))
        return tr("The node did not receive the exact acknowledgement a sweep requires.");
    if (is(Errors::CLAIM_NOT_YET))
        return tr("The vault's claim height has not been reached.");
    if (is(Errors::CLAIM_NOT_UNDERWATER))
        return tr("At the claim price the vault is not underwater, so a claim would be refused by the enforcing pools.");
    if (is(Errors::MINTPOL_NOT_ACTIVE))
        return tr("Yellowback is not active at the reference height: minting waits for activation.");
    if (is(Errors::MINTPOL_NO_PRICE))
        return tr("No mint price is defined at the reference height: too few recent blocks carry a price quote.");
    if (is(Errors::MINTPOL_PARTICIPATION))
        return tr("Minting is paused: fewer than 60 % of recent blocks signal Yellowback enforcement.");
    if (is(Errors::MINTPOL_GLOBAL_RATIO))
        return tr("Minting is paused: the network's overall collateral ratio is too low.");
    if (is(Errors::MINTPOL_DIVERGENCE))
        return tr("Minting is paused: the fast and slow price medians diverge too much.");
    if (is(Errors::MINTPOL_CAP))
        return tr("Minting is paused: the supply cap is reached.");
    if (is(Errors::MINT_UNSATISFIABLE))
        return tr("The collateral this mint needs exceeds the maximum amount of YEC.");
    if (is(Errors::MINT_BAD_LOCK))
        return tr("The lock length falls in no term class.");
    if (is(Errors::MEMPOOL_CHECK_FAILED))
        return tr("The node's own pre-check of the enforcement rules refused the transaction, so nothing was signed or sent.");
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
    if (is(Errors::NOT_DORMANT))
        return tr("Only a DORMANT attestor can be revived.");
    if (is(Errors::NOT_EQUIVOCATION))
        return tr("The two attestations are not an equivocation: they must be the same attestor, the same cited height, different prices, both validly signed over this chain's block hash.");
    if (is(Errors::ATTEST_KEY_NOT_HELD))
        return tr("This wallet does not hold the key this attestor action needs (the hot key for a revival, the bond key for a withdrawal).");
    if (is(Errors::ATTEST_UNKNOWN_SEQ))
        return tr("The node knows no attestor with that sequence number (a registration counts once its transaction confirms).");
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
