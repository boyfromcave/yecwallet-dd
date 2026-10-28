// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

#ifndef YELLOWBACKMODELS_H
#define YELLOWBACKMODELS_H

#include "precompiled.h"

using json = nlohmann::json;

// rpcversion 5: one claim intent of a CLAIMING vault (yed_getvault.intents).
struct YellowbackIntent {
    QString txid;               // the claim transaction
    int     vout            = 0;
    QString role;               // claimant | residual
    int     height          = 0;   // the claim's height
    int     releaseHeight   = 0;   // height + CLAIM_DELAY: vault_release from here on; an attestor cancel before it
    QString outpoint() const { return txid % ":" % QString::number(vout); }
    static YellowbackIntent fromJson(const json& j);
};

// One row of yed_listpositions (= yed_getvault + canRedeem/canClaim/canNotice; plan §4.5).
// Amounts are integer cents / zatoshi as the node reports them; rendering happens in the
// model's data(). A nullable or optional height is -1 when absent.
struct YellowbackPosition {
    QString txid;
    int     vout            = 0;
    QString status;             // ACTIVE | CLAIMING | CLOSED | CLAIMED (| VOID, indexed before the upgrade)
    int     ownerScheme     = 0;       // rpcversion 7: YellowbackRpc::PqScheme (1 = SLH-DSA); 0 when absent
    QString ownerHash;                 // rpcversion 7: SHA256(scheme || pk), 64 hex in byte order
    QString ownerAddress;              // rpcversion 7: the 53-character PQ owner address (a vault identifier, not a YED address)
    QString termClass;          // A | B | C
    int     lockHeight      = 0;
    int     claimHeight     = 0;
    qint64  collateralZat   = 0;
    qint64  mintedCents     = 0;
    int     mintHeight      = 0;
    int     refHeight       = 0;
    qint64  feePaidZat      = 0;
    qint64  pMint           = 0;       // rpcversion 8: the stored mint price (µUSD/YEC) every fee of the vault is struck at; 0 when absent
    int     closeHeight     = -1;      // null until CLOSED/CLAIMED
    QString closingTxid;               // "" until CLOSED/CLAIMED
    qint64  burnedCents     = 0;
    bool    unbacked        = false;   // closed without its burn (no rule produces one since the upgrade)
    bool    claimable       = false;
    qint64  underwaterAt    = -1;      // µUSD; null (-1) for VOID
    QString voidReason;                // "" unless VOID
    QString scriptPubKey;              // rpcversion 5: the vault's V template (hex)
    QList<YellowbackIntent> intents;   // rpcversion 5: present while CLAIMING
    int     cancelCount     = 0;       // CAN-2: the position's cancels counted by the escalation schedule
    int     cancelsRequired = 1;       // CAN-2: co-signatures of distinct current members a cancel confirming next block needs (1 when absent)
    int     cancelQuietUntil = -1;     // CAN-2: null (-1) when never cancelled
    QString refundHolder;              // CAN-1, while CLAIMING: the script (hex) a cancel re-issues the burn to; "" when absent
    bool    canRedeem       = false;   // ACTIVE or VOID at or past lockHeight (VOID: the Release, L14)
    bool    canClaim        = false;
    bool    noticed         = false;   // v3: a claim notice stands against this vault (NOT-1)
    int     noticeHeight    = -1;      // v3: null (-1) unless noticed
    int     emergencyOpenAt = -1;      // v3: the first reference height an emergency claim may cite; null (-1) unless noticed
    bool    canNotice       = false;   // v3, listpositions only: this node could post a notice now
    qint64  earlyRedeemFeeZat = 0;     // rpcversion 6 (IT-9), listpositions only: a redeem now pays it on top of the protocol fee (rpcversion 8: of the debt at pMint)
    bool    spendPending    = false;   // a transaction spending the vault waits in the mempool (a newer node; false when absent)
    QString spendTxid;                 // that transaction; "" when absent

    static YellowbackPosition fromJson(const json& j);
    /** "SLH-DSA-SHA2-128s" | "FN-DSA-512" | "scheme <n>": the owner's post-quantum signature scheme. */
    QString ownerSchemeName() const;
    QString vaultName() const { return txid % ":" % QString::number(vout); }
    /** The claimant intent while CLAIMING; nullptr otherwise. */
    const YellowbackIntent* claimantIntent() const;
    const YellowbackIntent* residualIntent() const;
};

// One row of yed_listclaimable (plan §4.5).
struct YellowbackClaimable {
    QString vault;              // "txid:0"
    QString ownerAddress;
    qint64  collateralZat   = 0;
    qint64  mintedCents     = 0;   // the burn a claim must carry
    qint64  pMint           = 0;   // rpcversion 8: the vault's stored mint price (µUSD/YEC)
    qint64  feeZat          = 0;   // rpcversion 8: FEE-1' of the debt at pMint
    int     claimHeight     = 0;
    qint64  underwaterAt    = 0;
    qint64  pClaim          = 0;
    QString claimPath;             // v3: "a" | "b" | "" (no bundle could be built: yed_claim refuses until the pool refills)
    qint64  residualZat     = 0;   // v3: RED-5, what the claim must return to the owner
    qint64  attestFeeZat    = 0;   // v3: AFEE-1
    bool    noticed         = false;
    int     noticeHeight    = -1;
    int     emergencyOpenAt = -1;
    // rpcversion 6 (IT-7): in-term rows are listed too; claimable false = above the threshold (claim refused)
    bool    claimable       = true;    // a row from before rpcversion 6 was listed only when claimable
    int     lockHeight      = -1;      // tip < lockHeight: a claim in term

    static YellowbackClaimable fromJson(const json& j);
    QString txid() const { return vault.section(':', 0, 0); }
};

// One row of yed_listattestors (v3 plan §4.8, the Attestors view). `weight` stays the decimal
// string the node reports; a nullable height is -1 when absent.
struct YellowbackAttestor {
    int     seq             = 0;
    QString status;             // PENDING | ELIGIBLE | DORMANT | EJECTED | WITHDRAWN
    int     statusHeight    = 0;
    QString attestorPubKey;
    QString bondAddress;
    QString bondKeyAddress;
    qint64  bondZat         = 0;
    int     bondLocktime    = 0;
    int     bondSpentHeight = -1;      // null until the bond is spent
    int     registerHeight  = 0;
    QString weight;                    // decimal string, "0" before ageOrigin
    bool    seated          = false;
    int     seatedSince     = -1;      // null while unseated
    bool    pinned          = false;
    bool    founding        = false;
    int     tier            = 0;       // flags.tier: 0 exchange APIs, 1 mixed, 2 aggregator
    bool    pool            = false;   // flags.pool: operates a mining pool
    int     lastBundleHeight = -1;     // null when none
    bool    poolFresh       = false;   // this node's pool holds a fresh attestation of this seq
    int     lastAct         = -1;      // P4-b: the set's lastAct for the member; -1 when absent
    bool    bondFrozen      = false;   // P4-b: the set froze the bond
    QString seatWeight;                // decimal string, Q16: bondZat x the tenure factor (the seat ranking)
    bool    evicted         = false;   // WITHDRAWN by a larger bond's join into the full set

    static YellowbackAttestor fromJson(const json& j);
};

// One row of yed_listtransactions (plan §4.5).
struct YellowbackTx {
    QString txid;
    int     height          = 0;
    int     confirmations   = 0;
    QString type;               // mint | send | receive | burn | redeem | claim | claimed
    QString verdict;
    QString path;               // owner | claim | ""
    qint64  yedIn           = 0;
    qint64  yedOut          = 0;
    qint64  burned          = 0;
    qint64  amountCents     = 0;
    qint64  feeZat          = 0;
    QString payee;              // "" when null
    bool    unbacked        = false;
    bool    expired         = false;
    qint64  time            = 0;       // the block time (Unix seconds, UTC); 0 while unconfirmed or on a node without `time`
    QString carrierTxid;               // plan A6: the carrier of a two-step action; "" when absent
    QString mainTxid;                  // plan A6: the main transaction the carrier led to; "" when absent
    QString carrierOutcome;            // pending | completed | lapsed | swept; "" when absent

    static YellowbackTx fromJson(const json& j);
    /** The row failed: expired, or its carrier lapsed or was swept without a main transaction. */
    bool failed() const;
    /** The row is waiting: unconfirmed, or its carrier is still pending. */
    bool pending() const;
};

// One row of yed_listaddresses (a newer node), or of the wallet's own fallback list on an older one.
struct YellowbackAddress {
    QString address;
    QString label;
    qint64  confirmedCents = -1;       // -1 when unknown (the fallback list)
    int     txCount        = -1;       // -1 when unknown
    qint64  lastUsed       = -1;       // Unix seconds, or a height on a node that reports one; -1 never / unknown
    static YellowbackAddress fromJson(const json& j);
};

// W3 (DigiByte v9.26.7 wave 5, 46a7c0de): what the ordinary Transactions tab says about a stock
// listtransactions row that is a Yellowback transaction, joined by txid to yed_listtransactions.
struct YellowbackTxAnnotation {
    QString label;                     // "Yellowback mint: collateral locked", "YED sent", ...
    qint64  yedCents  = 0;             // the YED it moved for this wallet, signed (+ received / minted, - sent / burned)
    bool    hasYed    = false;         // the YED column shows a figure (a carrier moves none)
    QString tooltip;
};

// Tolerant readers for RPC replies: a missing or null field yields the default instead of
// throwing, so a node one contract revision ahead or behind degrades to blanks, not a crash.
namespace YellowbackJson {
    qint64  toInt (const json& j, const char* key, qint64 def = 0);
    QString toStr (const json& j, const char* key, const QString& def = QString());
    bool    toBool(const json& j, const char* key, bool def = false);
    bool    isNull(const json& j, const char* key);       // absent or null
    bool    has   (const json& j, const char* key);       // present and not null
    const json& obj(const json& j, const char* key);      // nested object, or a static empty object
    QStringList strings(const json& j, const char* key);  // array of strings, or empty
}

// Rendering helpers shared by the models and the tab. Pure functions of their arguments; the
// current height is passed in so the models never reach into the controller.
namespace YellowbackFormat {
    QString cents(qint64 cents, bool showCentsUnit = false);
    QString zec(qint64 zat);
    QString price(qint64 microUsd);                        // "$1.9900" from µUSD
    QString priceOrUndefined(const json& j, const char* key);   // "undefined" for null (§4.8)
    QString bpsAsMultiplier(qint64 bps);                   // 10000 -> "1.00x"
    QString bpsAsPercent(qint64 bps);                      // 49750 -> "497.50 %"
    QString bpsAsShortPercent(qint64 bps);                 // 12500 -> "125 %", 250 -> "2.5 %" (the promise's own form)
    QString heightWithEstimate(int height, int currentHeight);
    QString duration(qint64 seconds);                 // "2 d 3 h", "4 h 5 m", "12 m"
    QString blocksAndDuration(int blocks);            // "48 block(s), ~1 h 0 m" at 75 s per block
    QDateTime estimateDate(int height, int currentHeight);
    QString typeLabel(const QString& type);
    /** The Transactions view's label for a row: typeLabel, except that a "redeem" row with no YED
     *  burned and the empty path is the release of a claim intent that closed an own vault (the
     *  node's yed_listtransactions types it "redeem"; both node lines, rpcversion 5). */
    QString rowLabel(const QString& type, const QString& path, qint64 burned);
    QString haltReason(const QString& name);               // haltMask name -> sentence
    QString voidReason(const QString& verdict);            // MINT verdict -> sentence
    QString sourceTier(int tier);                          // v3 flags.tier -> "exchange APIs" | "mixed" | "aggregator"
    // W4 (DigiByte v9.26.7 ea78ad18, 3f09a4a9, b00d185e): dates on screen follow the computer's regional
    // settings; an export carries ISO-8601 UTC and plain decimals, never the display forms.
    QString localDateTime(const QDateTime& dt);            // QLocale's short date and time, local time
    QString localDate(const QDateTime& dt);                // QLocale's short date
    QString unixTimeLocal(qint64 unixSeconds);             // localDateTime of a block time; "-" for 0
    QString isoUtc(qint64 unixSeconds);                    // "2026-10-28T14:05:09Z"; "" for 0
    QString plainDollars(qint64 cents);                    // "1234.50", "-40.00": no symbol, no grouping, '.' decimal
    /** W3: the ordinary tab's annotation of a Yellowback row (and of its carrier, `asCarrier`). Pure. */
    YellowbackTxAnnotation stockAnnotation(const YellowbackTx& t, bool asCarrier = false);
    /** W2: "Redemption pending (txid…)" for a vault whose spend waits in the mempool; "" otherwise. */
    QString spendPendingText(const YellowbackPosition& p);
}

// W5 (DigiByte v9.26.7 943bf9e9, 5e0f27ec, ba84ed96): the Yellowback tab's colours follow the theme.
// YecWallet's dark and matrix themes paint light text on a dark window (res/css/dark.css, matrix.css);
// the default theme follows the platform palette. Light colours are the ones the tab always used.
namespace YellowbackTheme {
    /** "dark" and "matrix" are dark; "light" and "blue" are light; any other (the default theme) follows
     *  the palette's window lightness. Pure. */
    bool    isDarkTheme(const QString& themeName, const QPalette& palette);
    void    setDark(bool dark);                            // set by YellowbackTab::applyTheme
    bool    dark();
    QColor  attentionBackground();                         // a vault exposed now (claimable, noticed, claiming)
    QColor  warningBackground();                           // a vault the owner should act on soon
    QColor  pendingBackground();                           // a vault whose spend waits in the mempool
    QColor  failedForeground();                            // expired, lapsed, claimable: red
    QColor  pendingForeground();                           // unconfirmed or waiting: a neutral blue, never the failure red
    QColor  cautionForeground();                           // a burn, an unbacked row, near the threshold: amber
    QColor  mutedForeground();                             // closed rows
    /** Label stylesheets: "muted" (secondary text, was #444), "hint" (an inline refusal, was #a33), and the
     *  boxed banners "warning", "ok", "error", "info", "note"; "" for an unknown kind. */
    QString labelCss(const QString& kind);
}

// Positions (vaults) table, pattern of src/balancestablemodel.h.
class YellowbackPositionsModel : public QAbstractTableModel {
public:
    YellowbackPositionsModel(QObject* parent);
    ~YellowbackPositionsModel();

    enum Column {
        Status = 0,
        ActBy,           // what the owner must do and by when: locked / redeem before the claim height / claim path open
        Minted,
        Collateral,
        Ratio,           // collateral at the latest (fast) price over the debt: the market view; the claim-price ratio is the tooltip
        TermClass,
        LockHeight,
        ClaimHeight,
        Claimable,       // yes, or why not and when: before the claim height / notice standing / needs a lower claim price
        UnderwaterBelow, // the claim price below which the vault can be claimed (yed_getvault.underwaterAt)
        Unbacked,
        ClaimState,      // rpcversion 5: "claimed, releases at h" while CLAIMING; "-" otherwise
        Notice,          // v3: "noticed, emergency claim from <h>" | "notice possible" | "-"
        MintPrice,       // rpcversion 8: the vault's stored pMint, the price its protocol and early fees are charged at
        Vault,
        ColumnCount
    };

    void setNewData(const QList<YellowbackPosition>& positions, int currentHeight);
    /** The tip's prices (µUSD) from yed_getstats: pFast for the market ratio, pClaim for what a
     *  claim is judged by. The Ratio column re-renders when they move. */
    void setPrices(qint64 pFastMicroUsd, qint64 pClaimMicroUsd);
    /** A vault's collateral ratio at `priceMicroUsd`, in bps; -1 when undefined (no debt, no price). */
    static qint64 ratioBps(const YellowbackPosition& p, qint64 priceMicroUsd);
    /** The Act-by cell: what the owner must do and by when. Under in-term claims (rpcversion 6,
     *  `inTerm`) the danger is the threshold, not a height: the cell says "claimable now", warns
     *  while the claim price `pClaim` (µUSD, 0 = unknown) is near the vault's underwaterAt
     *  (nearThreshold), and otherwise names the price; without it, the heights alone. */
    static QString actBy(const YellowbackPosition& p, int currentHeight, bool inTerm = false, qint64 pClaimMicroUsd = 0);
    /** How close the claim price may come to a vault's claimable-at price (underwaterAt) before the
     *  wallet warns: within 25 % above it (a fall of a fifth reaches it). */
    static constexpr qint64 WARN_MARGIN_BPS = 2500;
    /** An ACTIVE, not yet claimable vault whose claim price is within WARN_MARGIN_BPS above its
     *  underwaterAt; false when either price is unknown. Pure. */
    static bool nearThreshold(const YellowbackPosition& p, qint64 pClaimMicroUsd);
    /** rpcversion 6: the network's rule set (yed_getinfo.params.inTermClaims, claimThresholdBps). */
    void setRules(bool inTermClaims, qint64 claimThresholdBps);
    const YellowbackPosition* positionAt(int row) const;
    QList<YellowbackPosition> redeemable() const;

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YellowbackPosition>* modeldata     = nullptr;
    QList<QString>          headers;
    int                     currentHeight = 0;
    qint64                  pFast         = 0;      // µUSD; 0 = unknown
    qint64                  pClaim        = 0;      // µUSD; 0 = unknown
    bool                    inTerm        = false;  // rpcversion 6: params.inTermClaims
    qint64                  thetaBps      = 11000;  // params.claimThresholdBps; 11000 before rpcversion 6
    bool                    loading       = true;
};

/** The Positions table's status filter: "open" (ACTIVE, CLAIMING and VOID, the default -- the rows
 *  an owner can still act on or watch), or one status, or every row. */
class YellowbackPositionsFilter : public QSortFilterProxyModel {
public:
    explicit YellowbackPositionsFilter(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    /** "" = every row; "open" = ACTIVE, CLAIMING or VOID; else exactly that status. */
    void setStatusFilter(const QString& filter) { statusFilter = filter; invalidateFilter(); }
    QString status() const { return statusFilter; }
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
private:
    QString statusFilter = "open";
};

// Claimable vaults table (yed_listclaimable), the source of the Claim page.
class YellowbackClaimableModel : public QAbstractTableModel {
public:
    YellowbackClaimableModel(QObject* parent);
    ~YellowbackClaimableModel();

    enum Column {
        Vault = 0,
        Owner,
        Collateral,
        Burn,
        Fee,
        ClaimHeight,
        UnderwaterAt,
        PClaim,
        State,           // rpcversion 6: "claimable now" | "not claimable: above $x" (claimable: false, IT-7)
        ColumnCount
    };

    void setNewData(const QList<YellowbackClaimable>& rows, int currentHeight);
    const YellowbackClaimable* rowAt(int row) const;
    /** The listed rows a claim would be accepted for (claimable: true). */
    int claimableCount() const;

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YellowbackClaimable>* modeldata     = nullptr;
    QList<QString>           headers;
    int                      currentHeight = 0;
    bool                     loading       = true;
};

// Attestors table (yed_listattestors), the v3 Attestors view. Read-only.
class YellowbackAttestorsModel : public QAbstractTableModel {
public:
    YellowbackAttestorsModel(QObject* parent);
    ~YellowbackAttestorsModel();

    enum Column {
        Seq = 0,
        Status,
        Seated,          // seated / pinned marks
        Bond,            // bond amount and lock height
        Weight,
        SeatedSince,
        Founding,
        Sources,         // source tier and pool flag
        LastBundle,      // lastBundleHeight
        PoolFresh,
        LastAct,         // P4-b: the set's lastAct (join + maturity, or the last heartbeat)
        BondFrozen,      // P4-b
        SeatWeight,      // seatWeight / 65,536, in YEC
        ColumnCount
    };

    void setNewData(const QList<YellowbackAttestor>& rows, int currentHeight);
    const YellowbackAttestor* rowAt(int row) const;
    /** seatWeight (Q16 zat, decimal) in YEC: floor(q16 / 65,536) zat, formatted. */
    static QString seatWeightYec(const QString& q16);

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YellowbackAttestor>* modeldata     = nullptr;
    QList<QString>          headers;
    int                     currentHeight = 0;
    bool                    loading       = true;
};

// rpcversion 5: the Pending claims page. One row per claim intent of a CLAIMING vault (from
// yed_listvaults "CLAIMING"): the claimant's intent is released after CLAIM_DELAY to the claimant,
// or cancelled before then by the YED attestor set (the vault is ACTIVE again, the burn is
// re-issued to the claimant, CAN-1); the owner's residual intent is released only.
struct YellowbackPendingClaim {
    YellowbackPosition vault;   // the CLAIMING vault (yed_getvault row)
    YellowbackIntent   intent;
    bool               mineClaim = false;   // this wallet made the claim (its claim txid is in yed_listtransactions)
    bool               mineVault = false;   // this wallet owns the vault (it is in yed_listpositions)
};

class YellowbackPendingClaimsModel : public QAbstractTableModel {
public:
    YellowbackPendingClaimsModel(QObject* parent);
    ~YellowbackPendingClaimsModel();

    enum Column {
        Vault = 0,
        Role,            // claimant | owner's residual
        Whose,           // your claim | your vault | someone else's
        Debt,            // the YED the claim burned
        Collateral,
        ClaimHeight,     // the height of the claim
        ReleaseHeight,
        Remaining,       // "releasable now" | "n block(s)"
        ColumnCount
    };

    void setNewData(const QList<YellowbackPendingClaim>& rows, int currentHeight);
    const YellowbackPendingClaim* rowAt(int row) const;
    /** "releasable now" from releaseHeight on, else "in n block(s) (~t)". */
    static QString remaining(const YellowbackIntent& i, int currentHeight);

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YellowbackPendingClaim>* modeldata = nullptr;
    QList<QString>   headers;
    int              currentHeight = 0;
    bool             loading       = true;
};

// Yellowback transactions table, pattern of src/txtablemodel.h.
class YellowbackTxModel : public QAbstractTableModel {
public:
    YellowbackTxModel(QObject* parent);
    ~YellowbackTxModel();

    enum Column {
        Type = 0,
        Date,            // W4: the block time in the computer's regional format (yed_listtransactions.time)
        Amount,
        Confirmations,
        Height,
        Verdict,
        Txid,
        ColumnCount
    };
    /** W4: the role a sorting proxy reads: the raw value (cents, seconds, height), not the display string. */
    static constexpr int SortRole = Qt::UserRole + 1;

    void setNewData(const QList<YellowbackTx>& txs, int currentHeight);
    const YellowbackTx* txAt(int row) const;
    QString getTxId(int row) const;
    /** W4: the rows as CSV: plain-decimal dollars, ISO-8601 UTC dates, the txid and the carrier columns. Pure. */
    static QString toCsv(const QList<YellowbackTx>& txs);
    QString toCsv() const { return toCsv(*modeldata); }
    /** The signed amount a row shows (a send or burn negative). */
    static qint64 signedAmount(const YellowbackTx& t);

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YellowbackTx>* modeldata     = nullptr;
    QList<QString>    headers;
    int               currentHeight = 0;
    bool              loading       = true;
};

#endif // YELLOWBACKMODELS_H
