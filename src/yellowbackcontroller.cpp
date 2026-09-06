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
    transactions = new YellowbackTxModel(this);

    reason = tr("Not connected to ycashd yet.");
}

YellowbackController::~YellowbackController() {
    // positions / transactions are children of this QObject
}

Connection* YellowbackController::connection() {
    return rpc ? rpc->getConnection() : nullptr;
}

// ── Generic call ──────────────────────────────────────────────────────────────────────────

void YellowbackController::call(const char* method, const json& params, OkFn ok, ErrFn err) {
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
                    if (code == YellowbackRpc::Errors::METHOD_NOT_FOUND_CODE && !msg.contains(YellowbackRpc::Errors::METHOD_NOT_FOUND))
                        msg = QString(YellowbackRpc::Errors::METHOD_NOT_FOUND) % " (" % msg % ")";
                }
            } else if (reply != nullptr) {
                msg = reply->errorString();
            } else {
                msg = tr("Unknown error");
            }
            // Every command but yed_getinfo answers this while the index is unhealthy; take
            // the tab down at once instead of waiting for the next yed_getinfo.
            if (isIndexUnhealthy(msg)) {
                healthy = false;
                setAvailability(false, tr("Yellowback unavailable: %1").arg(msg));
            }
            if (err) err(msg);
        });
}

bool YellowbackController::isMethodNotFound(const QString& m) {
    return m.contains(YellowbackRpc::Errors::METHOD_NOT_FOUND, Qt::CaseInsensitive);
}

bool YellowbackController::isIndexUnhealthy(const QString& m) {
    return m.contains(YellowbackRpc::Errors::INDEX_UNHEALTHY, Qt::CaseInsensitive);
}

bool YellowbackController::isTransientRefusal(const QString& m) {
    return m.trimmed().endsWith(YellowbackRpc::Errors::TRANSIENT_SUFFIX);
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
            using namespace YellowbackRpc::Info;
            enabled = YellowbackJson::toBool(info, ENABLED, true);
            int version = (int)YellowbackJson::toInt(info, RPCVERSION, -1);

            if (!enabled) {
                setAvailability(false, tr("Yellowback is not enabled on this ycashd. Add 'experimentalfeatures=1' and 'yellowback=1' to ycash.conf and restart it."));
                emit infoUpdated();
                return;
            }
            if (version != Settings::getYellowbackRpcVersion()) {
                setAvailability(false, tr("This YecWallet understands Yellowback RPC version %1 but the node reports version %2. "
                                          "Update YecWallet (or ycashd) so the two match; the Yellowback tab is disabled until then.")
                                          .arg(Settings::getYellowbackRpcVersion()).arg(version));
                emit infoUpdated();
                return;
            }
            versionOk = true;
            applyInfo(info);
            refresh(true);
        },
        [=, this](const QString& e) {
            if (isMethodNotFound(e)) {
                setAvailability(false, tr("The connected ycashd has no Yellowback RPCs (%1). Add 'experimentalfeatures=1' and 'yellowback=1' to its ycash.conf and restart it.").arg(e));
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

void YellowbackController::applyInfo(const json& info) {
    using namespace YellowbackRpc::Info;
    net              = YellowbackJson::toStr (info, NETWORK, net);
    indexHeight      = (int)YellowbackJson::toInt(info, HEIGHT, indexHeight);
    indexStartHeight = (int)YellowbackJson::toInt(info, START_HEIGHT, indexStartHeight);
    synced           = YellowbackJson::toBool(info, SYNCED, false);
    healthy          = YellowbackJson::toBool(info, HEALTHY, false);
    if (info.is_object() && info.find(PARAMS) != info.end() && info[PARAMS].is_object())
        paramsJson = info[PARAMS];

    if (!synced) {
        setAvailability(false, tr("The Yellowback index is syncing (index height %1). Actions are disabled until it reaches the chain tip.").arg(indexHeight));
    } else if (!healthy) {
        QString why = YellowbackJson::toStr(info, UNHEALTHY_REASON, tr("unknown reason"));
        setAvailability(false, tr("Yellowback unavailable: the index reports it is unhealthy (%1). "
                                  "Fix: restart ycashd with -reindex-yellowback.").arg(why));
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
            if (force || indexHeight != lastRefreshHeight || !pending.isEmpty()) {
                lastRefreshHeight = indexHeight;
                refreshStats();
                refreshProtection();
                refreshBalance();
                refreshPositions();
                refreshTransactions();
            }
        },
        [=, this](const QString& e) {
            setAvailability(false, tr("yed_getinfo failed: %1").arg(e));
            emit infoUpdated();
        });
}

void YellowbackController::refreshStats() {
    call(YellowbackRpc::GETSTATS, json(nullptr),
        [=, this](const json& s) {
            statsJson = s.is_object() ? s : json::object();
            emit statsUpdated();
        },
        [=, this](const QString& e) { main->logger->write("yed_getstats: " + e); });
}

void YellowbackController::refreshProtection() {
    call(YellowbackRpc::GETPROTECTIONSTATUS, json(nullptr),
        [=, this](const json& p) {
            protectionJson = p.is_object() ? p : json::object();
            emit statsUpdated();
        },
        [=, this](const QString& e) { main->logger->write("yed_getprotectionstatus: " + e); });
}

void YellowbackController::refreshBalance() {
    call(YellowbackRpc::GETBALANCE, json(nullptr),
        [=, this](const json& b) {
            confirmed   = YellowbackJson::toInt(b, YellowbackRpc::Balance::CONFIRMED_CENTS);
            unconfirmed = YellowbackJson::toInt(b, YellowbackRpc::Balance::UNCONFIRMED_CENTS);
            emit balanceUpdated();
        },
        [=, this](const QString& e) { main->logger->write("yed_getbalance: " + e); });
}

void YellowbackController::refreshPositions() {
    call(YellowbackRpc::LISTPOSITIONS, json(nullptr),
        [=, this](const json& arr) {
            QList<YellowbackPosition> list;
            if (arr.is_array())
                for (auto& it : arr) list.append(YellowbackPosition::fromJson(it));
            positions->setNewData(list, indexHeight);
            emit positionsUpdated();
        },
        [=, this](const QString& e) { main->logger->write("yed_listpositions: " + e); });
}

void YellowbackController::refreshTransactions() {
    call(YellowbackRpc::LISTTRANSACTIONS, json::array({200, 0}),
        [=, this](const json& arr) {
            QList<YellowbackTx> list;
            if (arr.is_array())
                for (auto& it : arr) list.append(YellowbackTx::fromJson(it));
            transactions->setNewData(list, indexHeight);
            emit transactionsUpdated();
        },
        [=, this](const QString& e) { main->logger->write("yed_listtransactions: " + e); });
}

// ── Pending redemptions ───────────────────────────────────────────────────────────────────

int YellowbackController::deadlineHeight(int expiryHeight) const {
    // yed_submitredeem refuses once expiry < chainHeight + 1 + EXPIRING_SOON, so the last
    // accepted height is expiry - EXPIRING_SOON - 1 (what yed_redeem.deadlineHeight reports).
    return expiryHeight - YellowbackRpc::EXPIRING_SOON - 1;
}

void YellowbackController::addPendingRedemption(const QString& vaultTxid, int deadline) {
    pending[vaultTxid] = deadline;
    emit pendingChanged();
}

void YellowbackController::removePendingRedemption(const QString& vaultTxid) {
    if (pending.remove(vaultTxid) > 0)
        emit pendingChanged();
}

bool YellowbackController::watchPending() {
    if (pending.isEmpty() || !versionOk) return false;

    call(YellowbackRpc::GETINFO, json(nullptr),
        [=, this](const json& info) {
            int h = (int)YellowbackJson::toInt(info, YellowbackRpc::Info::HEIGHT, indexHeight);
            indexHeight = h;
            QList<QString> expired;
            for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
                if (h > it.value()) expired.append(it.key());   // deadlineHeight is inclusive
            for (auto& v : expired) {
                pending.remove(v);
                emit pendingChanged();
                emit pendingExpired(v);
            }
        },
        [=, this](const QString&) {});
    return true;
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

qint64 YellowbackController::minMintCents() const   { return YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::MIN_MINT_CENTS,   YellowbackRpc::MIN_MINT_CENTS); }
qint64 YellowbackController::maxMintCents() const   { return YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::MAX_MINT_CENTS,   YellowbackRpc::MAX_MINT_CENTS); }
qint64 YellowbackController::minOutputCents() const { return YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::MIN_OUTPUT_CENTS, YellowbackRpc::MIN_OUTPUT_CENTS); }
int    YellowbackController::mintEvalLag() const    { return (int)YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::MINT_EVAL_LAG, YellowbackRpc::MINT_EVAL_LAG); }
int    YellowbackController::mintWindow() const     { return (int)YellowbackJson::toInt(paramsJson, YellowbackRpc::Params::MINT_WINDOW,   YellowbackRpc::MINT_WINDOW); }

// ── Mint gate ─────────────────────────────────────────────────────────────────────────────

QString YellowbackController::mintBlocker(qint64 cents) const {
    using namespace YellowbackRpc;
    if (!available) return reason;
    if (indexHeight < indexStartHeight + mintEvalLag())
        return tr("The Yellowback index is too young to evaluate a mint (height %1, needs %2).")
                .arg(indexHeight).arg(indexStartHeight + mintEvalLag());

    // yed_getprotectionstatus is the authority on whether minting is open and why; fall back
    // to the yed_getstats fields when it has not answered yet.
    const bool haveProt = protectionJson.is_object() && !protectionJson.empty();
    const json& vol = haveProt && protectionJson.find(Protection::VOLATILITY) != protectionJson.end()
                      ? protectionJson[Protection::VOLATILITY] : statsJson;
    const json& err = haveProt && protectionJson.find(Protection::ERR) != protectionJson.end()
                      ? protectionJson[Protection::ERR] : json::object();

    bool frozen = haveProt ? YellowbackJson::toBool(vol, Protection::VOL_MINT_FROZEN)
                           : YellowbackJson::toBool(statsJson, Stats::MINT_FROZEN);
    if (frozen) {
        int until = haveProt ? (int)YellowbackJson::toInt(vol, Protection::VOL_FROZEN_UNTIL, -1)
                             : (int)YellowbackJson::toInt(statsJson, Stats::MINT_FROZEN_UNTIL, -1);
        return until > 0
            ? tr("Minting is paused by the volatility freeze until height %1.")
                .arg(YellowbackFormat::heightWithEstimate(until, indexHeight))
            : tr("Minting is paused by the volatility freeze.");
    }
    if (YellowbackJson::isNull(statsJson, Stats::PRICE_MICRO_USD))
        return tr("Minting is paused: the federation has not published a fresh YEC price.");
    qint64 health = haveProt ? YellowbackJson::toInt(protectionJson, Protection::HEALTH_PCT, 0)
                             : YellowbackJson::toInt(statsJson, Stats::HEALTH_PCT, 0);
    bool errActive = haveProt ? YellowbackJson::toBool(err, Protection::ERR_ACTIVE) : health < 100;
    if (errActive)
        return tr("Minting is paused: system health is %1 % (must be at least 100 %). "
                  "The emergency redemption ratio is in effect.").arg(health);
    if (haveProt && !YellowbackJson::toBool(protectionJson, Protection::MINTING_ALLOWED, true))
        return tr("Minting is paused (yed_getprotectionstatus reports mintingAllowed = false).");

    if (cents > 0) {
        if (cents < minMintCents() || cents > maxMintCents())
            return tr("A mint must be between %1 and %2.")
                    .arg(YellowbackFormat::cents(minMintCents())).arg(YellowbackFormat::cents(maxMintCents()));
        qint64 cap = YellowbackJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS, 0);   // 0 = no cap
        qint64 supply = YellowbackJson::toInt(statsJson, Stats::SUPPLY_CENTS);
        if (cap > 0 && supply + cents > cap)
            return tr("Minting %1 would exceed the supply cap (%2 of %3 in circulation).")
                    .arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(cap));
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

void YellowbackController::estimateCollateral(qint64 cents, int tier, OkFn ok, ErrFn err) {
    call(YellowbackRpc::ESTIMATECOLLATERAL, json::array({cents, tier}), ok, err);
}

void YellowbackController::mint(qint64 cents, int tier, OkFn ok, ErrFn err) {
    call(YellowbackRpc::MINT, json::array({cents, tier}), ok, err);
}

void YellowbackController::mint(qint64 cents, int tier, const QString& from, OkFn ok, ErrFn err) {
    if (from.isEmpty()) { mint(cents, tier, ok, err); return; }
    call(YellowbackRpc::MINT, json::array({cents, tier, from.toStdString()}), ok, err);
}

void YellowbackController::send(const QString& addr, qint64 cents, OkFn ok, ErrFn err) {
    call(YellowbackRpc::SEND, json::array({addr.toStdString(), cents}), ok, err);
}

void YellowbackController::redeem(const QString& vaultTxid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::REDEEM, json::array({vaultTxid.toStdString()}), ok, err);
}

void YellowbackController::redeem(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err) {
    if (to.isEmpty()) { redeem(vaultTxid, ok, err); return; }
    call(YellowbackRpc::REDEEM, json::array({vaultTxid.toStdString(), to.toStdString()}), ok, err);
}

void YellowbackController::submitRedeem(const QString& hex, OkFn ok, ErrFn err) {
    call(YellowbackRpc::SUBMITREDEEM, json::array({hex.toStdString()}), ok, err);
}

void YellowbackController::abortRedeem(const QString& vaultTxid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::ABORTREDEEM, json::array({vaultTxid.toStdString()}), ok, err);
}

void YellowbackController::getRoster(OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETROSTER, json(nullptr), ok, err);
}

void YellowbackController::getTxInfo(const QString& txid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETTXINFO, json::array({txid.toStdString()}), ok, err);
}

void YellowbackController::getVault(const QString& txid, OkFn ok, ErrFn err) {
    call(YellowbackRpc::GETVAULT, json::array({txid.toStdString()}), ok, err);
}
