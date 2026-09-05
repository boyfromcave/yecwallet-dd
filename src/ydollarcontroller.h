#ifndef YDOLLARCONTROLLER_H
#define YDOLLARCONTROLLER_H

#include "precompiled.h"
#include "ydollarmodels.h"

using json = nlohmann::json;

class MainWindow;
class Controller;
class Connection;

// Issues every yd_* call through the existing Connection (plan §4.7, mapping.md §12).
//
// Lifecycle, all driven by the stock Controller:
//   - Controller::setConnection      -> onConnected()  : yd_getinfo, rpcversion check, conf repair offer
//   - Controller::getInfoThenRefresh -> refresh()      : on the "block changed" branch
//   - Controller::watchTxStatus      -> watchPending() : pending redemptions on the quick txTimer
//
// It never holds key material and never talks to anything but the local node; the redemption
// wizard (ydollarredeemwizard.cpp) is the one place that reaches operator endpoints.
class YDollarController : public QObject {
    Q_OBJECT

public:
    YDollarController(MainWindow* main, Controller* rpc);
    ~YDollarController();

    // ── Lifecycle hooks ───────────────────────────────────────────────────────────────────
    void onConnected();
    void refresh(bool force = false);
    bool watchPending();                          // true while a redemption is pending

    // ── Availability (status banner) ──────────────────────────────────────────────────────
    // "available" means: node answers yd_getinfo, enabled, rpcversion matches, synced and healthy.
    bool    isAvailable() const { return available; }
    QString unavailableReason() const { return reason; }
    bool    isEnabled() const { return enabled; }
    bool    isVersionOk() const { return versionOk; }
    bool    isSynced() const { return synced; }
    bool    isHealthy() const { return healthy; }

    // ── Cached state ──────────────────────────────────────────────────────────────────────
    int     height() const { return indexHeight; }
    int     startHeight() const { return indexStartHeight; }
    QString network() const { return net; }       // "main" | "test" | "regtest" (never getinfo.testnet)
    QString addressPrefix() const;                // yd | yt | yr
    bool    looksLikeYDollarAddress(const QString& addr) const;
    bool    isShieldedAddress(const QString& addr) const;   // s1..., ys..., z... (refused)
    const json& stats() const { return statsJson; }
    qint64  confirmedCents() const { return confirmed; }
    qint64  unconfirmedCents() const { return unconfirmed; }
    double  yecBalance() const;                   // from the stock DataModel

    YDollarPositionsModel* positionsModel() { return positions; }
    YDollarTxModel*        transactionsModel() { return transactions; }

    // ── Mint gate: empty string when a mint of `cents` is allowed, else the reason ─────────
    QString mintBlocker(qint64 cents) const;

    // ── RPC calls. `ok` receives the "result"; `err` the node's error message verbatim ─────
    typedef std::function<void(const json&)>    OkFn;
    typedef std::function<void(const QString&)> ErrFn;

    void call(const char* method, const json& params, OkFn ok, ErrFn err);

    void getNewAddress(OkFn ok, ErrFn err);
    void validateAddress(const QString& addr, OkFn ok, ErrFn err);
    void estimateCollateral(qint64 cents, int tier, OkFn ok, ErrFn err);
    void mint(qint64 cents, int tier, OkFn ok, ErrFn err);
    void send(const QString& addr, qint64 cents, OkFn ok, ErrFn err);
    void redeem(const QString& vaultTxid, OkFn ok, ErrFn err);
    void submitRedeem(const QString& hex, OkFn ok, ErrFn err);
    void abortRedeem(const QString& vaultTxid, OkFn ok, ErrFn err);
    void getRoster(OkFn ok, ErrFn err);
    void getTxInfo(const QString& txid, OkFn ok, ErrFn err);
    void getVault(const QString& txid, OkFn ok, ErrFn err);

    // ── Pending redemptions (yd_redeem issued, yd_submitredeem not yet) ───────────────────
    void addPendingRedemption(const QString& vaultTxid, int expiryHeight);
    void removePendingRedemption(const QString& vaultTxid);
    bool hasPendingRedemption(const QString& vaultTxid) const { return pending.contains(vaultTxid); }
    const QMap<QString, int>& pendingRedemptions() const { return pending; }
    int  deadlineHeight(int expiryHeight) const;  // last height a submit is accepted

    // Static helpers
    static bool isMethodNotFound(const QString& errorMessage);
    static bool isTransientRefusal(const QString& errorMessage);

    MainWindow* mainWindow() { return main; }
    Connection* connection();

signals:
    void availabilityChanged(bool available, const QString& reason);
    void infoUpdated();
    void statsUpdated();
    void balanceUpdated();
    void positionsUpdated();
    void transactionsUpdated();
    void pendingChanged();
    void pendingExpired(const QString& vaultTxid);

private:
    void applyInfo(const json& info);
    void refreshStats();
    void refreshBalance();
    void refreshPositions();
    void refreshTransactions();
    void setAvailability(bool avail, const QString& why);

    MainWindow*   main;
    Controller*   rpc;

    YDollarPositionsModel* positions    = nullptr;
    YDollarTxModel*        transactions = nullptr;

    bool    available   = false;
    bool    enabled     = false;
    bool    versionOk   = false;
    bool    synced      = false;
    bool    healthy     = false;
    bool    confRepairOffered = false;
    QString reason;
    QString net;
    int     indexHeight      = 0;
    int     indexStartHeight = 0;
    int     lastRefreshHeight = -1;
    json    statsJson   = json::object();
    qint64  confirmed   = 0;
    qint64  unconfirmed = 0;

    QMap<QString, int> pending;                   // vaultTxid -> expiryHeight
};

#endif // YDOLLARCONTROLLER_H
