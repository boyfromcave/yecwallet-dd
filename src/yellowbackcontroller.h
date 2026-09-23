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
    // v3: the same for yed_getprice, yed_listattestors and yed_getselection.
    void feedAttest(const json& price, const json& attestors, const json& selection);

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
    const json& attest() const;                                 // v3: yed_getinfo.attest (empty object until answered)
    const json& price() const { return priceJson; }             // v3: yed_getprice at the tip
    const json& selection() const { return selectionJson; }     // v3: yed_getselection at refHeightNow()
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
    YellowbackAttestorsModel* attestorsModel() { return attestors; }   // v3

    // ── Protocol parameters: yed_getinfo.params when present, else the compiled-in defaults ─
    qint64  minMintCents() const;
    qint64  maxMintCents() const;
    qint64  minOutputCents() const;
    int     refLag() const;
    int     refWindow() const;                    // v3: the carrier and its main transaction expire together after it
    int     grace() const;
    // Term classes from yed_getinfo.params.classes, in class order (empty until the node answered)
    struct TermClass { QString name; int minBlocks = 0; int maxBlocks = 0; qint64 baseRatioBps = 0; };
    QList<TermClass> termClasses() const;

    // ── v3 attestation layer (plan §4.8), pure functions of the replies so the QTest reads them ──
    // The arming banner: "UNARMED" / "TRIGGERED at h, arms at h'" / "ARMED", or the disabled
    // sentence when `required` is false. Empty until the node has answered.
    static QString describeAttest(const json& attest);
    // "n of m selected attestors reachable" from yed_getselection; empty while not armed.
    static QString describeSelection(const json& selection);
    // The mint10-diverged sentence when an estimate's divergenceBps exceeds params.attest
    // .divergeBpsAttest (or the node already refused with mint10-diverged); empty otherwise.
    static QString describeDivergence(const json& estimate, qint64 divergeBpsAttest);
    static QString describeDivergenceError(const QString& errorMessage, qint64 divergeBpsAttest);
    qint64  divergeBpsAttest() const;             // params.attest.divergeBpsAttest, 0 when unknown

    // ── Mint gate: empty string when a mint of `cents` is allowed, else the reason ─────────
    // MINTPOL-1 as yed_getstats reports it: mintingAllowed, and every haltMask name by name.
    /** Why a mint of `cents` in `termClass` ("" = any) cannot be built now; empty when it can. */
    QString mintBlocker(qint64 cents, const QString& termClass = QString()) const;
    /** W16: the classes yed_getstats says can mint now (every class while minting is open). */
    QStringList mintableClasses() const;
    /** W16: non-blocking notice when the global-ratio halt limits minting to the recapitalising classes; empty otherwise. */
    QString mintLimit() const;
    /** The Balance tab's two Yellowback lines (owner's request, regtest plan F-22): the YED
     *  balance, and the YEC locked as collateral in this wallet's active vaults. "-" until known. */
    QString balanceSummary() const;
    QString collateralSummary() const;
    /** The pools' fast median (µUSD) when the node is enabled, activated and has one; else nullopt.
     *  Pushed into Settings as the wallet's YEC/USD rate (F-23). */
    std::optional<qint64> protocolPriceMicroUsd() const;
private:
    void pushProtocolPrice();
public:

    // ── RPC calls. `ok` receives the "result"; `err` the node's error message verbatim ─────
    typedef std::function<void(const json&)>    OkFn;
    typedef std::function<void(const QString&)> ErrFn;

    void call(const char* method, const json& params, OkFn ok, ErrFn err);

    // The fake Connection of the offline QTest (N28): when set, call() hands every request to
    // it instead of the Connection, so a dialog can be driven with canned result JSON or a
    // canned error message. The devnet case sets one that posts to the node directly.
    typedef std::function<void(const QString& method, const json& params, OkFn ok, ErrFn err)> Transport;
    void setTransport(Transport t) { transport = t; }

    void getNewAddress(OkFn ok, ErrFn err);
    void validateAddress(const QString& addr, OkFn ok, ErrFn err);
    void estimateCollateral(qint64 cents, int lockBlocks, OkFn ok, ErrFn err);
    // v3: the two-step commands are issued with wait = false (W7), so the reply comes back
    // right after the carrier broadcast with pending = true; awaitPending() follows the main
    // transaction from there.
    void mint(qint64 cents, int lockBlocks, const QString& from, OkFn ok, ErrFn err);   // from: "" | ys1... (I2)
    void send(const QString& addr, qint64 cents, OkFn ok, ErrFn err);
    void redeem(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);     // to: "" | s1... | ys1... (I2)
    void claim(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);
    void claimNotice(const QString& vaultTxid, OkFn ok, ErrFn err);                   // v3: yed_claimnotice (NOT-1)
    void sweepCarriers(OkFn ok, ErrFn err);                                           // v3: yed_sweepcarriers (W7)
    void registerAttestor(double bondYec, int lockBlocks, int flags, OkFn ok, ErrFn err);   // v3
    void withdrawBond(int seq, const QString& to, OkFn ok, ErrFn err);                // v3
    void revive(int seq, qint64 priceMicroUsd, OkFn ok, ErrFn err);                   // v3
    void reportEquivocation(const QString& hexA, const QString& hexB, OkFn ok, ErrFn err);   // v3, two-step

    // v3 two-step follow-up (W7). A pending reply names only the carrier; the main
    // transaction is built by the node on the next ChainTip and shows up in
    // yed_listtransactions as a new row of `type`. This polls that list until a row of the type
    // appears that was not there when the action started, then answers `done` with its
    // yed_gettxinfo. `err` fires when the carrier's window lapses (tip past refHeight +
    // refWindow: yed_sweepcarriers reclaims it) or the poll itself fails.
    void awaitPending(const QString& type, const QString& carrierTxid, int refHeight, OkFn done, ErrFn err);
    void setPendingPollMs(int ms) { pendingPollMs = ms; }   // the QTest shortens it
    // The flags byte yed_registerattestor takes (proposal §5.2): bits 0-1 the source tier, bit 2 pool operator.
    static int attestorFlags(int tier, bool pool) { return (tier & 3) | (pool ? 4 : 0); }
    void refreshSelection();                      // v3: public so the Mint page's retry can re-query it
    void sweep(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);      // sends the L10 acknowledgement
    void getTxInfo(const QString& txid, OkFn ok, ErrFn err);
    void getVault(const QString& txid, OkFn ok, ErrFn err);
    void getFeePayee(int refHeight, qint64 collateralZat, OkFn ok, ErrFn err);   // FEE-1 / FEE-W for a confirmation

    // The term class a lock length falls in (params.classes), name "" when it is in none (mint-bad-lock).
    TermClass classForLock(int lockBlocks) const;
    // The reference height a transaction built now would use: tip - REF_LAG (§3.5), for yed_getfeepayee.
    int refHeightNow() const { return std::max(0, indexHeight - refLag()); }

    // What the node's refusal means to the user, keyed on the identifier that begins the message
    // (§4.5 error table); empty when the wallet has nothing to add to the verbatim message.
    static QString explainError(const QString& errorMessage);
    // change-floor: the two workable amounts the message names (§4.6), "send exactly `all` or at
    // most `atMost`"; false when the message carries no amounts (then only the text is shown).
    static bool parseChangeFloor(const QString& errorMessage, qint64* allCents, qint64* atMostCents);
    // v3 bundle-insufficient: "<count> of <selected> selected attestors have a fresh attestation;
    // missing seq <a,b,…>" — the two numbers and the missing seqs; false when the message is not
    // that identifier (then only the text is shown).
    static bool parseBundleInsufficient(const QString& errorMessage, int* count, int* selected, QList<int>* missingSeqs);

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
    void priceUpdated();          // v3
    void attestorsUpdated();      // v3
    void selectionUpdated();      // v3

private:
    void applyInfo(const json& info);
    void applyStats(const json& s);
    void applyActivation(const json& a);
    void applyBalance(const json& b);
    void applyPositions(const json& arr);
    void applyClaimable(const json& arr);
    void applyTransactions(const json& arr);
    void applyPrice(const json& p);
    void applyAttestors(const json& arr);
    void applySelection(const json& s);
    void refreshStats();
    void refreshActivation();
    void refreshBalance();
    void refreshPositions();
    void refreshClaimable();
    void refreshTransactions();
    void refreshPrice();
    void refreshAttestors();
    void setAvailability(bool avail, const QString& why);
    void log(const QString& line);

    MainWindow*   main;
    Controller*   rpc;
    Transport     transport;

    YellowbackPositionsModel* positions    = nullptr;
    YellowbackClaimableModel* claimable    = nullptr;
    YellowbackTxModel*        transactions = nullptr;
    YellowbackAttestorsModel* attestors    = nullptr;

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
    json    priceJson      = json::object();
    json    selectionJson  = json::object();
    qint64  confirmed   = 0;
    qint64  unconfirmed = 0;
    int     pendingPollMs = 5000;
};

#endif // YELLOWBACKCONTROLLER_H
