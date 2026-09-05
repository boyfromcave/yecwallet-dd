#include "ydollarcontroller.h"
#include "ydollarrpc.h"
#include "controller.h"
#include "connection.h"
#include "mainwindow.h"
#include "settings.h"

using json = nlohmann::json;

static json rpcPayload(const char* method, const json& params) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "ydollar"},
        {"method", method}
    };
    if (!params.is_null())
        payload["params"] = params;
    return payload;
}

YDollarController::YDollarController(MainWindow* main, Controller* rpc) : QObject(nullptr) {
    this->main = main;
    this->rpc  = rpc;

    positions    = new YDollarPositionsModel(this);
    transactions = new YDollarTxModel(this);

    reason = tr("Not connected to ycashd yet.");
}

YDollarController::~YDollarController() {
    // positions / transactions are children of this QObject
}

Connection* YDollarController::connection() {
    return rpc ? rpc->getConnection() : nullptr;
}

// ── Generic call ──────────────────────────────────────────────────────────────────────────

void YDollarController::call(const char* method, const json& params, OkFn ok, ErrFn err) {
    auto conn = connection();
    if (conn == nullptr) {
        if (err) err(tr("Not connected to ycashd."));
        return;
    }

    conn->doRPCSafe(rpcPayload(method, params),
        [=](const json& result) {
            if (ok) ok(result);
        },
        [=](QNetworkReply* reply, const json& parsed) {
            QString msg;
            if (!parsed.is_discarded() && parsed.is_object() &&
                parsed.contains("error") && parsed["error"].is_object() &&
                parsed["error"].contains("message") && parsed["error"]["message"].is_string()) {
                msg = QString::fromStdString(parsed["error"]["message"].get<json::string_t>());
                if (parsed["error"].contains("code") && parsed["error"]["code"].is_number_integer()) {
                    // Keep the identifier: the code is what makes "Method not found" unambiguous
                    int code = parsed["error"]["code"].get<json::number_integer_t>();
                    if (code == YDollarRpc::Errors::METHOD_NOT_FOUND_CODE && !msg.contains(YDollarRpc::Errors::METHOD_NOT_FOUND))
                        msg = QString(YDollarRpc::Errors::METHOD_NOT_FOUND) % " (" % msg % ")";
                }
            } else if (reply != nullptr) {
                msg = reply->errorString();
            } else {
                msg = tr("Unknown error");
            }
            if (err) err(msg);
        });
}

bool YDollarController::isMethodNotFound(const QString& m) {
    return m.contains(YDollarRpc::Errors::METHOD_NOT_FOUND, Qt::CaseInsensitive);
}

bool YDollarController::isTransientRefusal(const QString& m) {
    return m.contains(YDollarRpc::Errors::RED_0) || m.contains(YDollarRpc::Errors::RED_2);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────────────────

void YDollarController::setAvailability(bool avail, const QString& why) {
    bool changed = (avail != available) || (why != reason);
    available = avail;
    reason    = why;
    if (changed)
        emit availabilityChanged(available, reason);
}

void YDollarController::onConnected() {
    versionOk = false;
    enabled   = false;
    lastRefreshHeight = -1;

    call(YDollarRpc::GETINFO, json(nullptr),
        [=](const json& info) {
            using namespace YDollarRpc::Info;
            enabled = YDollarJson::toBool(info, ENABLED, true);
            int version = (int)YDollarJson::toInt(info, RPCVERSION, -1);

            if (!enabled) {
                setAvailability(false, tr("YDollar is not enabled on this ycashd. Add 'experimentalfeatures=1' and 'ydollar=1' to ycash.conf and restart it."));
                emit infoUpdated();
                return;
            }
            if (version != Settings::getYDollarRpcVersion()) {
                setAvailability(false, tr("This YecWallet understands YDollar RPC version %1 but the node reports version %2. "
                                          "Update YecWallet (or ycashd) so the two match; the YDollar tab is disabled until then.")
                                          .arg(Settings::getYDollarRpcVersion()).arg(version));
                emit infoUpdated();
                return;
            }
            versionOk = true;
            applyInfo(info);
            refresh(true);
        },
        [=](const QString& e) {
            if (isMethodNotFound(e)) {
                setAvailability(false, tr("The connected ycashd has no YDollar RPCs (%1). Add 'experimentalfeatures=1' and 'ydollar=1' to its ycash.conf and restart it.").arg(e));
                if (!confRepairOffered) {
                    confRepairOffered = true;
                    auto conn = connection();
                    if (conn != nullptr && conn->offerYDollarConfRepair()) {
                        QMessageBox::information(main, tr("ycash.conf updated"),
                            tr("The two lines were added. Restart ycashd (or YecWallet, if it runs the embedded node) to enable YDollar."));
                    }
                }
            } else {
                setAvailability(false, tr("yd_getinfo failed: %1").arg(e));
            }
            emit infoUpdated();
        });
}

void YDollarController::applyInfo(const json& info) {
    using namespace YDollarRpc::Info;
    net              = YDollarJson::toStr (info, NETWORK, net);
    indexHeight      = (int)YDollarJson::toInt(info, HEIGHT, indexHeight);
    indexStartHeight = (int)YDollarJson::toInt(info, START_HEIGHT, indexStartHeight);
    synced           = YDollarJson::toBool(info, SYNCED, false);
    healthy          = YDollarJson::toBool(info, HEALTHY, false);

    if (!synced) {
        setAvailability(false, tr("The YDollar index is syncing (index height %1). Actions are disabled until it reaches the chain tip.").arg(indexHeight));
    } else if (!healthy) {
        QString why = YDollarJson::toStr(info, UNHEALTHY_REASON, tr("unknown reason"));
        setAvailability(false, tr("YDollar unavailable: the index reports it is unhealthy (%1). "
                                  "Fix: restart ycashd with -reindex-ydollar.").arg(why));
    } else {
        setAvailability(true, QString());
    }
    emit infoUpdated();
}

void YDollarController::refresh(bool force) {
    if (!versionOk || !enabled) return;

    call(YDollarRpc::GETINFO, json(nullptr),
        [=](const json& info) {
            applyInfo(info);
            if (force || indexHeight != lastRefreshHeight || !pending.isEmpty()) {
                lastRefreshHeight = indexHeight;
                refreshStats();
                refreshBalance();
                refreshPositions();
                refreshTransactions();
            }
        },
        [=](const QString& e) {
            setAvailability(false, tr("yd_getinfo failed: %1").arg(e));
            emit infoUpdated();
        });
}

void YDollarController::refreshStats() {
    call(YDollarRpc::GETSTATS, json(nullptr),
        [=](const json& s) {
            statsJson = s.is_object() ? s : json::object();
            emit statsUpdated();
        },
        [=](const QString& e) { main->logger->write("yd_getstats: " + e); });
}

void YDollarController::refreshBalance() {
    call(YDollarRpc::GETBALANCE, json(nullptr),
        [=](const json& b) {
            confirmed   = YDollarJson::toInt(b, YDollarRpc::Balance::CONFIRMED_CENTS);
            unconfirmed = YDollarJson::toInt(b, YDollarRpc::Balance::UNCONFIRMED_CENTS);
            emit balanceUpdated();
        },
        [=](const QString& e) { main->logger->write("yd_getbalance: " + e); });
}

void YDollarController::refreshPositions() {
    call(YDollarRpc::LISTPOSITIONS, json(nullptr),
        [=](const json& arr) {
            QList<YDollarPosition> list;
            if (arr.is_array())
                for (auto& it : arr) list.append(YDollarPosition::fromJson(it));
            positions->setNewData(list, indexHeight);
            emit positionsUpdated();
        },
        [=](const QString& e) { main->logger->write("yd_listpositions: " + e); });
}

void YDollarController::refreshTransactions() {
    call(YDollarRpc::LISTTRANSACTIONS, json::array({200, 0}),
        [=](const json& arr) {
            QList<YDollarTx> list;
            if (arr.is_array())
                for (auto& it : arr) list.append(YDollarTx::fromJson(it));
            transactions->setNewData(list, indexHeight);
            emit transactionsUpdated();
        },
        [=](const QString& e) { main->logger->write("yd_listtransactions: " + e); });
}

// ── Pending redemptions ───────────────────────────────────────────────────────────────────

int YDollarController::deadlineHeight(int expiryHeight) const {
    return expiryHeight - YDollarRpc::EXPIRING_SOON;
}

void YDollarController::addPendingRedemption(const QString& vaultTxid, int expiryHeight) {
    pending[vaultTxid] = expiryHeight;
    emit pendingChanged();
}

void YDollarController::removePendingRedemption(const QString& vaultTxid) {
    if (pending.remove(vaultTxid) > 0)
        emit pendingChanged();
}

bool YDollarController::watchPending() {
    if (pending.isEmpty() || !versionOk) return false;

    call(YDollarRpc::GETINFO, json(nullptr),
        [=](const json& info) {
            int h = (int)YDollarJson::toInt(info, YDollarRpc::Info::HEIGHT, indexHeight);
            indexHeight = h;
            QList<QString> expired;
            for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
                if (h >= deadlineHeight(it.value())) expired.append(it.key());
            for (auto& v : expired) {
                pending.remove(v);
                emit pendingChanged();
                emit pendingExpired(v);
            }
        },
        [=](const QString&) {});
    return true;
}

// ── Addresses ─────────────────────────────────────────────────────────────────────────────

QString YDollarController::addressPrefix() const {
    if (net == "test")    return "yt";
    if (net == "regtest") return "yr";
    return "yd";
}

bool YDollarController::looksLikeYDollarAddress(const QString& addr) const {
    QString a = addr.trimmed();
    if (a.length() < 26 || a.length() > 40) return false;
    if (!a.startsWith(addressPrefix())) return false;
    static const QRegularExpression base58("^[1-9A-HJ-NP-Za-km-z]+$");
    return base58.match(a).hasMatch();
}

bool YDollarController::isShieldedAddress(const QString& addr) const {
    QString a = addr.trimmed();
    // Ycash: s1.../s3... transparent, ys.../ytestsapling... Sapling, z... Sprout. None can hold YDollar.
    return a.startsWith("s1") || a.startsWith("s3") || a.startsWith("ys") ||
           a.startsWith("ytestsapling") || a.startsWith("z");
}

double YDollarController::yecBalance() const {
    if (rpc == nullptr) return 0.0;
    double total = 0.0;
    auto balances = rpc->getModel()->getAllBalances();
    for (auto it = balances.constBegin(); it != balances.constEnd(); ++it)
        if (Settings::isTAddress(it.key())) total += it.value();
    return total;
}

// ── Mint gate ─────────────────────────────────────────────────────────────────────────────

QString YDollarController::mintBlocker(qint64 cents) const {
    using namespace YDollarRpc;
    if (!available) return reason;
    if (indexHeight < indexStartHeight + MINT_EVAL_LAG)
        return tr("The YDollar index is too young to evaluate a mint (height %1, needs %2).")
                .arg(indexHeight).arg(indexStartHeight + MINT_EVAL_LAG);
    if (YDollarJson::toBool(statsJson, Stats::MINT_FROZEN)) {
        int until = (int)YDollarJson::toInt(statsJson, Stats::MINT_FROZEN_UNTIL);
        return tr("Minting is paused by the volatility freeze until height %1.")
                .arg(YDollarFormat::heightWithEstimate(until, indexHeight));
    }
    if (YDollarJson::isNull(statsJson, Stats::PRICE_MICRO_USD))
        return tr("Minting is paused: the federation has not published a fresh YEC price.");
    qint64 health = YDollarJson::toInt(statsJson, Stats::HEALTH_PCT, 0);
    if (health < 100)
        return tr("Minting is paused: system health is %1 % (must be at least 100 %). "
                  "The emergency redemption ratio is in effect.").arg(health);
    if (cents > 0) {
        if (cents < MIN_MINT_CENTS || cents > MAX_MINT_CENTS)
            return tr("A mint must be between %1 and %2.")
                    .arg(YDollarFormat::cents(MIN_MINT_CENTS)).arg(YDollarFormat::cents(MAX_MINT_CENTS));
        if (!YDollarJson::isNull(statsJson, Stats::SUPPLY_CAP_CENTS)) {
            qint64 cap = YDollarJson::toInt(statsJson, Stats::SUPPLY_CAP_CENTS);
            qint64 supply = YDollarJson::toInt(statsJson, Stats::SUPPLY_CENTS);
            if (cap > 0 && supply + cents > cap)
                return tr("Minting %1 would exceed the supply cap (%2 of %3 in circulation).")
                        .arg(YDollarFormat::cents(cents)).arg(YDollarFormat::cents(supply)).arg(YDollarFormat::cents(cap));
        }
    }
    return QString();
}

// ── Typed calls ───────────────────────────────────────────────────────────────────────────

void YDollarController::getNewAddress(OkFn ok, ErrFn err) {
    call(YDollarRpc::GETNEWADDRESS, json(nullptr), ok, err);
}

void YDollarController::validateAddress(const QString& addr, OkFn ok, ErrFn err) {
    call(YDollarRpc::VALIDATEADDRESS, json::array({addr.toStdString()}), ok, err);
}

void YDollarController::estimateCollateral(qint64 cents, int tier, OkFn ok, ErrFn err) {
    call(YDollarRpc::ESTIMATECOLLATERAL, json::array({cents, tier}), ok, err);
}

void YDollarController::mint(qint64 cents, int tier, OkFn ok, ErrFn err) {
    call(YDollarRpc::MINT, json::array({cents, tier}), ok, err);
}

void YDollarController::send(const QString& addr, qint64 cents, OkFn ok, ErrFn err) {
    call(YDollarRpc::SEND, json::array({addr.toStdString(), cents}), ok, err);
}

void YDollarController::redeem(const QString& vaultTxid, OkFn ok, ErrFn err) {
    call(YDollarRpc::REDEEM, json::array({vaultTxid.toStdString()}), ok, err);
}

void YDollarController::submitRedeem(const QString& hex, OkFn ok, ErrFn err) {
    call(YDollarRpc::SUBMITREDEEM, json::array({hex.toStdString()}), ok, err);
}

void YDollarController::abortRedeem(const QString& vaultTxid, OkFn ok, ErrFn err) {
    call(YDollarRpc::ABORTREDEEM, json::array({vaultTxid.toStdString()}), ok, err);
}

void YDollarController::getRoster(OkFn ok, ErrFn err) {
    call(YDollarRpc::GETROSTER, json(nullptr), ok, err);
}

void YDollarController::getTxInfo(const QString& txid, OkFn ok, ErrFn err) {
    call(YDollarRpc::GETTXINFO, json::array({txid.toStdString()}), ok, err);
}

void YDollarController::getVault(const QString& txid, OkFn ok, ErrFn err) {
    call(YDollarRpc::GETVAULT, json::array({txid.toStdString()}), ok, err);
}
