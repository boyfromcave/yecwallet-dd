#ifndef YELLOWBACKCONTROLLER_H
#define YELLOWBACKCONTROLLER_H

#include "precompiled.h"
#include "yellowbackmodels.h"

using json = nlohmann::json;

class MainWindow;
class Controller;
class Connection;

// Issues every yed_* call through the existing Connection (plan §4.7, mapping.md §12).
//
// Lifecycle, all driven by the stock Controller:
//   - Controller::setConnection      -> onConnected()  : yed_getinfo, rpcversion check, conf repair offer
//   - Controller::getInfoThenRefresh -> refresh()      : on the "block changed" branch
//   - Controller::watchTxStatus      -> watchPending() : pending redemptions on the quick txTimer
//
// It never holds key material and never talks to anything but the local node; the redemption
// wizard (yellowbackredeemwizard.cpp) is the one place that reaches operator endpoints.
class YellowbackController : public QObject {
    Q_OBJECT

public:
    YellowbackController(MainWindow* main, Controller* rpc);
    ~YellowbackController();

    // ── Lifecycle hooks ───────────────────────────────────────────────────────────────────
    void onConnected();
    void refresh(bool force = false);
    bool watchPending();                          // true while a redemption is pending

    // ── Availability (status banner) ──────────────────────────────────────────────────────
    // "available" means: node answers yed_getinfo, enabled, rpcversion matches, synced and healthy.
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
    QString addressPrefix() const;                // ye | yt | yr
    bool    looksLikeYellowbackAddress(const QString& addr) const;
    bool    isShieldedAddress(const QString& addr) const;   // s1..., ys..., z... (refused)
    const json& stats() const { return statsJson; }
    const json& protection() const { return protectionJson; }   // yed_getprotectionstatus
    const json& params() const { return paramsJson; }           // yed_getinfo.params
    qint64  confirmedCents() const { return confirmed; }
    qint64  unconfirmedCents() const { return unconfirmed; }
    double  yecBalance() const;                   // from the stock DataModel: every transparent address
    // Plan I2: funding sources and collateral destinations. `source` is "" for the transparent
    // total, else one address (s1... or ys1...) whose balance the DataModel knows.
    double  yecBalanceAt(const QString& source) const;
    QList<QPair<QString, double>> saplingAddresses() const;      // ys1... addresses of this wallet with balances
    QList<QPair<QString, double>> transparentAddresses() const;  // s1... addresses of this wallet with balances

    YellowbackPositionsModel* positionsModel() { return positions; }
    YellowbackTxModel*        transactionsModel() { return transactions; }

    // ── Protocol parameters: yed_getinfo.params when present, else the compiled-in defaults ─
    qint64  minMintCents() const;
    qint64  maxMintCents() const;
    qint64  minOutputCents() const;
    int     mintEvalLag() const;
    int     mintWindow() const;

    // ── Mint gate: empty string when a mint of `cents` is allowed, else the reason ─────────
    // Prefers yed_getprotectionstatus (mintingAllowed and why) and falls back to yed_getstats.
    QString mintBlocker(qint64 cents) const;

    // ── RPC calls. `ok` receives the "result"; `err` the node's error message verbatim ─────
    typedef std::function<void(const json&)>    OkFn;
    typedef std::function<void(const QString&)> ErrFn;

    void call(const char* method, const json& params, OkFn ok, ErrFn err);

    void getNewAddress(OkFn ok, ErrFn err);
    void validateAddress(const QString& addr, OkFn ok, ErrFn err);
    void estimateCollateral(qint64 cents, int tier, OkFn ok, ErrFn err);
    void mint(qint64 cents, int tier, OkFn ok, ErrFn err);
    void mint(qint64 cents, int tier, const QString& from, OkFn ok, ErrFn err);   // from: "" | s1... | ys1... (I2)
    void send(const QString& addr, qint64 cents, OkFn ok, ErrFn err);
    void redeem(const QString& vaultTxid, OkFn ok, ErrFn err);
    void redeem(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err); // to: "" | s1... | ys1... (I2)
    void submitRedeem(const QString& hex, OkFn ok, ErrFn err);
    void abortRedeem(const QString& vaultTxid, OkFn ok, ErrFn err);
    void getRoster(OkFn ok, ErrFn err);
    void getTxInfo(const QString& txid, OkFn ok, ErrFn err);
    void getVault(const QString& txid, OkFn ok, ErrFn err);

    // ── Pending redemptions (yed_redeem issued, yed_submitredeem not yet) ───────────────────
    // Keyed by vault txid; the value is yed_redeem.deadlineHeight, the last height at which
    // yed_submitredeem is accepted.
    void addPendingRedemption(const QString& vaultTxid, int deadlineHeight);
    void removePendingRedemption(const QString& vaultTxid);
    bool hasPendingRedemption(const QString& vaultTxid) const { return pending.contains(vaultTxid); }
    const QMap<QString, int>& pendingRedemptions() const { return pending; }
    int  deadlineHeight(int expiryHeight) const;  // fallback when the node did not say: expiry - EXPIRING_SOON - 1

    // Static helpers
    static bool isMethodNotFound(const QString& errorMessage);
    static bool isIndexUnhealthy(const QString& errorMessage);
    static bool isTransientRefusal(const QString& errorMessage);   // "... (transient)"

    MainWindow* mainWindow() { return main; }
    Connection* connection();

signals:
    void availabilityChanged(bool available, const QString& reason);
    void infoUpdated();
    void statsUpdated();          // after yed_getstats and after yed_getprotectionstatus
    void balanceUpdated();
    void positionsUpdated();
    void transactionsUpdated();
    void pendingChanged();
    void pendingExpired(const QString& vaultTxid);

private:
    void applyInfo(const json& info);
    void refreshStats();
    void refreshProtection();
    void refreshBalance();
    void refreshPositions();
    void refreshTransactions();
    void setAvailability(bool avail, const QString& why);

    MainWindow*   main;
    Controller*   rpc;

    YellowbackPositionsModel* positions    = nullptr;
    YellowbackTxModel*        transactions = nullptr;

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
    json    statsJson      = json::object();
    json    protectionJson = json::object();
    json    paramsJson     = json::object();
    qint64  confirmed   = 0;
    qint64  unconfirmed = 0;

    QMap<QString, int> pending;                   // vaultTxid -> deadlineHeight
};

#endif // YELLOWBACKCONTROLLER_H
