// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

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
// canned replies). Wording follows plan §4.8 (banner row) and the upgrade plan's trust statement
// (§10): under the vault upgrade the Yellowback rules are consensus, checked by every full node.
struct YellowbackStatus {
    bool        available = false;   // node answers, enabled, rpcversion ok, synced, healthy
    QString     reason;              // why not, when !available
    QString     headline;            // one line: the vault upgrade's status
    QStringList warnings;            // unprotected YED outputs, own vaults being claimed
    QStringList notes;               // information lines (locked outputs, own-pool quote state)
};

// rpcversion 5: one member of the YED attestor set as set_getinfo reports it (vault-rpc.md).
struct YellowbackSetMember {
    QString key;
    QString status;              // active | removed | ejected | withdrawn
    bool    current    = false;  // ACTIVE and past the set's maturity
    bool    live       = false;  // lastAct within the set's liveness window
    int     joinHeight = 0;
    int     lastAct    = 0;
    qint64  bondValue  = 0;      // zat
    int     bondLocktime = 0;
    bool    bondFrozen = false;
    bool    wallet     = false;  // this wallet holds the key
    static YellowbackSetMember fromJson(const nlohmann::json& j);
    /** "current, live" | "current, DORMANT (no act since h)" | "joined, maturing" | "removed" ... */
    QString describe(int height, int livenessWindow) const;
};

// rpcversion 5: what became of a claim this wallet made (derived from yed_gettxinfo and
// yed_getvault; the node lists no row for a release or a cancel to the claimant).
struct YellowbackClaimOutcome {
    QString claimTxid;
    QString vaultTxid;           // the vault the claim moved into intents
    qint64  burnedCents = 0;
    int     height      = 0;
    QString outcome;             // "pending" | "released" | "cancelled" | "" (unknown)
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
    // ── rpcversion 5: the vault upgrade and the YED attestor set ─────────────────────────
    const json& upgrade() const;                                // yed_getinfo.upgrade (empty until answered)
    const json& vaultInfo() const { return vaultInfoJson; }    // vault_getinfo
    bool    upgradeActive() const;                              // upgrade.status == active
    QString attestorSetId() const;                              // params.attestorSetId, else upgrade.attestorSetId
    int     claimDelay() const;                                 // params.claimDelay (CLAIM_DELAY)
    const json& attestorSet() const { return setJson; }         // set_getinfo <attestorSetId>
    QList<YellowbackSetMember> setMembers() const;              // every member record
    QList<YellowbackSetMember> walletMembers() const;           // the members whose key this wallet holds
    /** This node's wallet holds the key of a current, unfrozen member of the YED attestor set:
     *  it may sign a cancel of a wrong-price claim (and heartbeat). */
    bool    isAttestor() const;
    int     setLivenessWindow() const;
    int     setCancelThreshold() const;
    YellowbackPendingClaimsModel* pendingClaimsModel() { return pendingClaims; }
    QList<YellowbackClaimOutcome> claimOutcomes() const { return outcomes.values(); }
    /** The P2PKH script (hex) of a key id as yed_getvault prints it (CKeyID::GetHex, byte-reversed):
     *  the residual intent's recipient, which vault_release needs when this wallet is not the owner. */
    static QString p2pkhScriptForKeyId(const QString& keyIdHex);
    static bool isOwnClaim(const YellowbackTx& t);   // a claim this wallet made (its YED was burned in it)
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
    // rpcversion 5: the CLAIMING vaults (yed_listvaults "CLAIMING"), and set_getinfo of the
    // attestor set, through the same offline feed.
    void feedUpgrade(const json& claimingVaults, const json& attestorSet, const json& vaultInfo = json(nullptr));

    // ── Protocol parameters: yed_getinfo.params when present, else the compiled-in defaults ─
    qint64  minMintCents() const;
    qint64  maxMintCents() const;
    qint64  minOutputCents() const;
    int     refLag() const;
    int     refWindow() const;                    // v3: the carrier and its main transaction expire together after it
    int     grace() const;
    // Term classes from yed_getinfo.params.classes, in class order (empty until the node answered)
    // H-5: a row with minBlocks > maxBlocks is a disabled class (B and C on mainnet and testnet).
    struct TermClass { QString name; int minBlocks = 0; int maxBlocks = 0; qint64 baseRatioBps = 0;
                       bool enabled() const { return !name.isEmpty() && minBlocks <= maxBlocks; } };
    QList<TermClass> termClasses() const;          // every row, enabled or not
    QList<TermClass> enabledClasses() const;       // the rows a mint can use (H-5)
    // ── Hardening H-1 / H-5 (the Mint page's standing note) ──────────────────────────────
    bool    mintRequiresArmed() const;             // yed_getinfo.mintRequiresArmed; false on a node before H-1
    bool    isArmed() const;                       // yed_getinfo.attest.armed
    /** The Mint page's standing note: which classes this network offers (class A only under H-5)
     *  and, under H-1, that every mint needs the attestation layer ARMED. Empty when neither applies. */
    QString mintClassNote() const;

    // ── Hardening H-9.3: client plausibility (audit G-1/G-2, F-4) ─────────────────────────
    // The wallet recomputes, from yed_getinfo.params, every figure it shows the user before a
    // mint, a redeem or a claim, and refuses to send when the node's reply disagrees. Pure.
    static qint64 feeZatFor(const json& params, qint64 collateralZat);        // FEE-1: max(FEE_MIN, c · FEE_BPS / 10⁴)
    static qint64 attestFeeZatFor(const json& params, qint64 feeZat);         // AFEE-1: fee · ATTEST_FEE_BPS / 10⁴
    static qint64 requiredZatFor(qint64 cents, qint64 minRatioBps, qint64 pMint);   // ceil(cents · ratio · COIN / pMint) up to 1,000 zat; -1 undefined
    static qint64 mintCollateralZatFor(const json& params, qint64 requiredZat);     // MINT-5: max(required, 4 · FEE_MIN) up to 1,000 zat
    /** What is wrong with a yed_estimatecollateral reply for (cents, lockBlocks) at index height
     *  `indexHeight`: the height identities (refHeight = tip − REF_LAG, lockHeight = refHeight +
     *  lockBlocks, claimHeight = lockHeight + GRACE), the class and its ratio, the collateral
     *  and the attestor fee. Empty when every figure checks out. */
    static QStringList checkEstimate(const json& params, int indexHeight, qint64 cents, int lockBlocks, const json& estimate);
    /** The same for a yed_listclaimable row: FEE-1, AFEE-1, the claim height reached. */
    static QStringList checkClaimable(const json& params, int indexHeight, const YellowbackClaimable& c);
    /** The same for a vault's heights: claimHeight = lockHeight + GRACE, lockHeight − refHeight in its class. */
    static QStringList checkVaultHeights(const json& params, const YellowbackPosition& p);

    // ── Hardening H-9.2: vault deadlines ──────────────────────────────────────────────────
    static constexpr int BLOCKS_PER_DAY = 86400 / 75;   // SECONDS_PER_BLOCK: the "claimHeight − 1 day" warning
    /** The persistent warning for one vault: an ACTIVE vault from claimHeight − 1 day on (redeem or
     *  renew before the claim height), a VOID one likewise (release); empty otherwise. Pure. */
    static QString deadlineWarning(const YellowbackPosition& p, int height);
    /** rpcversion 6 (in-term claims, IT-8): the persistent warning for one ACTIVE vault under the
     *  threshold rule: claimable now, or the claim price `pClaim` (µUSD) within
     *  YellowbackPositionsModel::WARN_MARGIN_BPS above its underwaterAt; empty otherwise. Pure. */
    static QString thresholdWarning(const YellowbackPosition& p, qint64 pClaimMicroUsd);
    /** Every owned vault's deadlineWarning at the index height (thresholdWarning under in-term
     *  claims), for the tab's banner. */
    QStringList deadlineWarnings() const;
    /** The lock length a renewal of `p` re-mints with: the vault's own (lockHeight − refHeight)
     *  when an enabled class still takes it, else the shortest enabled one; 0 when none. */
    int renewLockBlocks(const YellowbackPosition& p) const;

    // ── rpcversion 6: in-term claims (docs/plans/yellowback-in-term-claims-plan.md IT-7..IT-9) ─
    bool    inTermClaims() const;                  // params.inTermClaims
    qint64  claimThresholdBps() const;             // params.claimThresholdBps (θ); 11000 when absent
    qint64  claimPriceNow() const;                 // yed_getstats.pClaim, 0 when undefined
    /** IT-9: params.earlyRedeemFeeBps of a term class ("A" | "B" | "C"); 0 when unknown. Pure. */
    static qint64 earlyRedeemFeeBpsFor(const json& params, const QString& termClass);
    /** IT-9: collateral · earlyRedeemFeeBps / 10⁴ (floor, no minimum), the node's EarlyRedeemFeeZat. */
    static qint64 earlyRedeemFeeZatFor(const json& params, const QString& termClass, qint64 collateralZat);
    /** What is wrong with a yed_estimateredeem reply for vault `p` (H-9.3): the vault, the burn,
     *  the collateral, the class's fee rate, and the fee = FEE-1 + the early-redeem fee when early
     *  (both 0 under FEE-0). Empty when every figure checks out. Pure. */
    static QStringList checkEstimateRedeem(const json& params, const YellowbackPosition& p, const json& estimate);
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
    /** W16/W20: non-blocking notice when the global-ratio halt or the supply cap (or both) limit
     *  minting to the classes at or above the recapitalisation floor; empty otherwise. */
    QString mintLimit() const;
    /** W20: the node reports yed_getinfo.supplyCapReached (a node from before W20 does not, and
     *  keeps the hard cap: every mint above it is refused). */
    bool    softSupplyCap() const;
    /** W20: yed_getinfo.supplyCapReached; false on a node from before W20. */
    bool    supplyCapReached() const;
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
    // maxCollateralZat: the most collateral the user agreed to (yed_mint refuses with
    // collateral-above-max beyond it, audit F-1); minOutZat likewise for yed_claim (claim-out-below-min).
    void mint(qint64 cents, int lockBlocks, const QString& from, qint64 maxCollateralZat, OkFn ok, ErrFn err);   // from: "" | ys1... (I2)
    void send(const QString& addr, qint64 cents, OkFn ok, ErrFn err);
    void redeem(const QString& vaultTxid, const QString& to, OkFn ok, ErrFn err);     // to: "" | s1... | ys1... (I2)
    void estimateRedeem(const QString& vaultTxid, OkFn ok, ErrFn err);                // rpcversion 6 (IT-9): yed_estimateredeem
    // maxBurnCents (rpcversion 4, H-9.3): the most YED the claim may burn (claim-burn-above-max beyond it).
    void claim(const QString& vaultTxid, const QString& to, qint64 minOutZat, qint64 maxBurnCents, OkFn ok, ErrFn err);
    void claimNotice(const QString& vaultTxid, OkFn ok, ErrFn err);                   // v3: yed_claimnotice (NOT-1)
    void sweepCarriers(OkFn ok, ErrFn err);                                           // v3: yed_sweepcarriers (W7)
    // P4-b: a SET_JOIN to the attestor set with a fresh wallet key (the flags argument is ignored
    // since P4-b and not sent); bondYec a decimal string (AmountFromValue takes it exactly, audit F-8)
    void registerAttestor(const QString& bondYec, int lockBlocks, OkFn ok, ErrFn err);
    void withdrawBond(int seq, const QString& to, OkFn ok, ErrFn err);                // v3
    void heartbeat(const QString& memberKey, OkFn ok, ErrFn err);                     // P4-b: set_heartbeat <attestorSetId> <key>
    // rpcversion 5: the claim intent spends (the primitive's RPCs, vaultrpc.h)
    void releaseIntent(const QString& outpoint, const QString& recipientScript, OkFn ok, ErrFn err);   // vault_release; script "" = the wallet's own
    void buildCancel(const QString& outpoint, OkFn ok, ErrFn err);                    // vault_buildcancel
    void signCancel(const QString& hex, OkFn ok, ErrFn err);                          // set_signcancel
    void sendVaultTx(const QString& hex, OkFn ok, ErrFn err);                         // vault_send
    /** Follow every claim of this wallet (yed_listtransactions "claim") to its outcome: pending,
     *  released, or cancelled by the attestor set (the burn is not refunded, U-24). */
    void refreshClaimOutcomes();
    void reportEquivocation(const QString& hexA, const QString& hexB, OkFn ok, ErrFn err);   // v3, two-step

    // v3 two-step follow-up (W7). A pending reply names only the carrier; the main
    // transaction is built by the node on the next ChainTip and shows up in
    // yed_listtransactions as a new row of `type`. This polls that list until a row of the type
    // appears that was not there when the action started, then answers `done` with its
    // yed_gettxinfo. `err` fires when the carrier's window lapses (tip past refHeight +
    // refWindow: yed_sweepcarriers reclaims it) or the poll itself fails.
    // A claim also accepts a "claimed" row (a claim of an own vault is listed so); a notice,
    // which has no row, is followed through yed_getnotice `vaultTxid` (pass it for a notice).
    void awaitPending(const QString& type, const QString& carrierTxid, int refHeight, OkFn done, ErrFn err,
                      const QString& vaultTxid = QString());
    void setPendingPollMs(int ms) { pendingPollMs = ms; }   // the QTest shortens it
    void refreshSelection();                      // v3: public so the Mint page's retry can re-query it
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
    void pendingClaimsUpdated();  // rpcversion 5: yed_listvaults CLAIMING
    void attestorSetUpdated();    // rpcversion 5: set_getinfo of the attestor set
    void claimOutcomesUpdated();  // rpcversion 5

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
    void applyClaimingVaults(const json& arr);
    void rebuildPendingClaims();
    void applyAttestorSet(const json& s);
    void refreshStats();
    void refreshActivation();
    void refreshBalance();
    void refreshPositions();
    void refreshClaimable();
    void refreshTransactions();
    void refreshPrice();
    void refreshAttestors();
    void refreshPendingClaims();
    void refreshAttestorSet();
    void refreshVaultInfo();
    void setAvailability(bool avail, const QString& why);
    void log(const QString& line);

    MainWindow*   main;
    Controller*   rpc;
    Transport     transport;

    YellowbackPositionsModel* positions    = nullptr;
    YellowbackClaimableModel* claimable    = nullptr;
    YellowbackTxModel*        transactions = nullptr;
    YellowbackAttestorsModel* attestors    = nullptr;
    YellowbackPendingClaimsModel* pendingClaims = nullptr;

    bool    available   = false;
    bool    enabled     = false;
    bool    versionOk   = false;
    bool    synced      = false;
    bool    healthy     = false;
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
    json    claimingJson   = json::array();
    json    positionsJson  = json::array();   // the last yed_listpositions reply
    json    setJson        = json::object();
    json    vaultInfoJson  = json::object();
    QMap<QString, YellowbackClaimOutcome> outcomes;   // by claim txid
    qint64  confirmed   = 0;
    qint64  unconfirmed = 0;
    int     pendingPollMs = 5000;
};

#endif // YELLOWBACKCONTROLLER_H
