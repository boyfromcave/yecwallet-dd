#ifndef YELLOWBACKCONTROLLER_H
#define YELLOWBACKCONTROLLER_H

#include "precompiled.h"
#include "yellowbackmodels.h"

using json = nlohmann::json;

class MainWindow;
class Controller;
class Connection;

// What the status banner shows, computed from yed_getinfo / yed_getstats / yed_getactivation
// by YellowbackController::describeStatus (a pure function, so the offline QTest can feed it
// canned replies). Wording follows plan §4.8 (banner row) and §8.1.
struct YellowbackStatus {
    bool        available = false;   // node answers, enabled, rpcversion ok, synced, healthy
    QString     reason;              // why not, when !available
    QString     headline;            // one line: activation state and enforcement
    QStringList warnings;            // valve tripped, sunset, suspended, abandoned, participation halt
    QStringList notes;               // information lines (suppressedBlocks, own-pool quote state)
};

// Issues every yed_* call through the existing Connection (plan §4.8, mapping.md §12).
//
// Lifecycle, all driven by the stock Controller:
//   - Controller::setConnection      -> onConnected()  : yed_getinfo, rpcversion check, conf repair offer
//   - Controller::getInfoThenRefresh -> refresh()      : on the "block changed" branch
//
// It never holds key material and never talks to anything but the local node.
class YellowbackController : public QObject {
    Q_OBJECT

public:
    YellowbackController(MainWindow* main, Controller* rpc);
    ~YellowbackController();

    // ── Lifecycle hooks ───────────────────────────────────────────────────────────────────
    void onConnected();
    void refresh(bool force = false);

    // Offline feed (the QTest, N28): apply canned replies exactly as the RPC callbacks would,
    // emitting the same signals. No Connection is touched. Pass json(nullptr) to skip one.
    void feed(const json& info, const json& stats, const json& activation,
              const json& balance, const json& positions, const json& claimable, const json& transactions);

    // ── Availability (status banner) ──────────────────────────────────────────────────────
    // "available" means: node answers yed_getinfo, enabled, rpcversion matches, synced and healthy.
    bool    isAvailable() const { return available; }
    QString unavailableReason() const { return reason; }
    bool    isEnabled() const { return enabled; }
    bool    isVersionOk() const { return versionOk; }
    bool    isSynced() const { return synced; }
    bool    isHealthy() const { return healthy; }
    YellowbackStatus status() const;
    static YellowbackStatus describeStatus(bool available, const QString& reason,
                                           const json& info, const json& stats, const json& activation);

    // ── Cached state ──────────────────────────────────────────────────────────────────────
    int     height() const { return indexHeight; }
    int     chainHeight() const { return tipHeight; }
    int     startHeight() const { return indexStartHeight; }
    QString network() const { return net; }       // "main" | "test" | "regtest" (never getinfo.testnet)
    QString addressPrefix() const;                // ye | yt | yr
    bool    looksLikeYellowbackAddress(const QString& addr) const;
    bool    isShieldedAddress(const QString& addr) const;   // s1..., ys..., z... (refused)
    const json& info() const { return infoJson; }               // yed_getinfo (whole result)
    const json& stats() const { return statsJson; }             // yed_getstats
    const json& activation() const { return activationJson; }   // yed_getactivation
    const json& params() const { return paramsJson; }           // yed_getinfo.params
    bool    isAbandoned() const;                                // yed_getinfo.abandoned (L10)
    bool    isEnforcing() const;                                // yed_getinfo.enforcing
    qint64  confirmedCents() const { return confirmed; }
    qint64  unconfirmedCents() const { return unconfirmed; }
    double  yecBalance() const;                   // from the stock DataModel: every transparent address
    // Plan I2: funding sources and collateral destinations. `source` is "" for the transparent
    // total, else one address (s1... or ys1...) whose balance the DataModel knows.
    double  yecBalanceAt(const QString& source) const;
    QList<QPair<QString, double>> saplingAddresses() const;      // ys1... addresses of this wallet with balances
    QList<QPair<QString, double>> transparentAddresses() const;  // s1... addresses of this wallet with balances

    YellowbackPositionsModel* positionsModel() { return positions; }
    YellowbackClaimableModel* claimableModel() { return claimable; }
    YellowbackTxModel*        transactionsModel() { return transactions; }

    // ── Protocol parameters: yed_getinfo.params when present, else the compiled-in defaults ─
    qint64  minMintCents() const;
    qint64  maxMintCents() const;
    qint64  minOutputCents() const;
    int     refLag() const;
    int     grace() const;
    // Term classes from yed_getinfo.params.classes, in class order (empty until the node answered)
    struct TermClass { QString name; int minBlocks = 0; int maxBlocks = 0; qint64 baseRatioBps = 0; };
    QList<TermClass> termClasses() const;

    // ── Mint gate: empty string when a mint of `cents` is allowed, else the reason ─────────
    // MINTPOL-1 as yed_getstats reports it: mintingAllowed, and every haltMask name by name.
    QString mintBlocker(qint64 cents) const;

    // ── RPC calls. `ok` receives the "result"; `err` the node's error message verbatim ─────
    typedef std::function<void(const json&)>    OkFn;
    typedef std::function<void(const QString&)> ErrFn;

    void call(const char* method, const json& params, OkFn ok, ErrFn err);

    void getNewAddress(OkFn ok, ErrFn err);
    void validateAddress(const QString& addr, OkFn ok, ErrFn err);
    void estimateCollateral(qint64 cents, int lockBlocks, OkFn ok, ErrFn err);
    void mint(qint64 cents, int lockBlocks, const QString& from, OkFn ok, ErrFn err);   // from: "" | ys1... (I2)
    void send(const QString& addr, qint64 cents, OkFn ok, ErrFn err);
    void redeem(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);     // to: "" | s1... | ys1... (I2)
    void claim(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);
    void sweep(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);      // sends the L10 acknowledgement
    void getTxInfo(const QString& txid, OkFn ok, ErrFn err);
    void getVault(const QString& txid, OkFn ok, ErrFn err);

    // Static helpers
    static bool isMethodNotFound(const QString& errorMessage);
    static bool isIndexUnhealthy(const QString& errorMessage);

    MainWindow* mainWindow() { return main; }
    Connection* connection();

signals:
    void availabilityChanged(bool available, const QString& reason);
    void infoUpdated();
    void statsUpdated();          // after yed_getstats and after yed_getactivation
    void balanceUpdated();
    void positionsUpdated();
    void claimableUpdated();
    void transactionsUpdated();

private:
    void applyInfo(const json& info);
    void applyStats(const json& s);
    void applyActivation(const json& a);
    void applyBalance(const json& b);
    void applyPositions(const json& arr);
    void applyClaimable(const json& arr);
    void applyTransactions(const json& arr);
    void refreshStats();
    void refreshActivation();
    void refreshBalance();
    void refreshPositions();
    void refreshClaimable();
    void refreshTransactions();
    void setAvailability(bool avail, const QString& why);
    void log(const QString& line);

    MainWindow*   main;
    Controller*   rpc;

    YellowbackPositionsModel* positions    = nullptr;
    YellowbackClaimableModel* claimable    = nullptr;
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
    int     tipHeight        = 0;
    int     indexStartHeight = 0;
    int     lastRefreshHeight = -1;
    json    infoJson       = json::object();
    json    statsJson      = json::object();
    json    activationJson = json::object();
    json    paramsJson     = json::object();
    qint64  confirmed   = 0;
    qint64  unconfirmed = 0;
};

#endif // YELLOWBACKCONTROLLER_H
