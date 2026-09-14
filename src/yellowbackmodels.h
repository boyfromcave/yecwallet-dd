#ifndef YELLOWBACKMODELS_H
#define YELLOWBACKMODELS_H

#include "precompiled.h"

using json = nlohmann::json;

// One row of yed_listpositions (= yed_getvault + canRedeem/canClaim/canSweep; plan §4.5).
// Amounts are integer cents / zatoshi as the node reports them; rendering happens in the
// model's data(). A nullable or optional height is -1 when absent.
struct YellowbackPosition {
    QString txid;
    int     vout            = 0;
    QString status;             // ACTIVE | VOID | CLOSED | CLAIMED
    QString ownerAddress;
    QString ownerKeyId;
    QString termClass;          // A | B | C
    int     lockHeight      = 0;
    int     claimHeight     = 0;
    qint64  collateralZat   = 0;
    qint64  mintedCents     = 0;
    int     mintHeight      = 0;
    int     refHeight       = 0;
    qint64  feePaidZat      = 0;
    int     closeHeight     = -1;      // null until CLOSED/CLAIMED
    QString closingTxid;               // "" until CLOSED/CLAIMED
    qint64  burnedCents     = 0;
    bool    unbacked        = false;   // closed without its burn (a sweep)
    bool    claimable       = false;
    qint64  underwaterAt    = -1;      // µUSD; null (-1) for VOID
    QString voidReason;                // "" unless VOID
    int     sweepBefore     = -1;      // optional: present on VOID, and on ACTIVE under abandonment
    bool    canRedeem       = false;   // ACTIVE or VOID at or past lockHeight (VOID: the Release, L14)
    bool    canClaim        = false;
    bool    canSweep        = false;
    bool    noticed         = false;   // v3: a claim notice stands against this vault (NOT-1)
    int     noticeHeight    = -1;      // v3: null (-1) unless noticed
    int     emergencyOpenAt = -1;      // v3: the first reference height an emergency claim may cite; null (-1) unless noticed
    bool    canNotice       = false;   // v3, listpositions only: this node could post a notice now

    static YellowbackPosition fromJson(const json& j);
    QString vaultName() const { return txid % ":" % QString::number(vout); }
};

// One row of yed_listclaimable (plan §4.5).
struct YellowbackClaimable {
    QString vault;              // "txid:0"
    QString ownerAddress;
    qint64  collateralZat   = 0;
    qint64  mintedCents     = 0;   // the burn a claim must carry
    qint64  feeZat          = 0;
    int     claimHeight     = 0;
    qint64  underwaterAt    = 0;
    qint64  pClaim          = 0;
    QString claimPath;             // v3: "a" | "b" | "" (no bundle could be built: yed_claim refuses until the pool refills)
    qint64  residualZat     = 0;   // v3: RED-5, what the claim must return to the owner
    qint64  attestFeeZat    = 0;   // v3: AFEE-1
    bool    noticed         = false;
    int     noticeHeight    = -1;
    int     emergencyOpenAt = -1;

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

    static YellowbackAttestor fromJson(const json& j);
};

// One row of yed_listtransactions (plan §4.5).
struct YellowbackTx {
    QString txid;
    int     height          = 0;
    int     confirmations   = 0;
    QString type;               // mint | send | receive | burn | redeem | claim | claimed | sweep
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

    static YellowbackTx fromJson(const json& j);
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
    QString heightWithEstimate(int height, int currentHeight);
    QDateTime estimateDate(int height, int currentHeight);
    QString typeLabel(const QString& type);
    QString haltReason(const QString& name);               // haltMask name -> sentence
    QString voidReason(const QString& verdict);            // MINT verdict -> sentence
    QString sourceTier(int tier);                          // v3 flags.tier -> "exchange APIs" | "mixed" | "aggregator"
}

// Positions (vaults) table, pattern of src/balancestablemodel.h.
class YellowbackPositionsModel : public QAbstractTableModel {
public:
    YellowbackPositionsModel(QObject* parent);
    ~YellowbackPositionsModel();

    enum Column {
        Status = 0,
        Minted,
        Collateral,
        TermClass,
        LockHeight,
        ClaimHeight,
        Claimable,
        Unbacked,
        SweepBefore,
        Notice,          // v3: "noticed, emergency claim from <h>" | "notice possible" | "-"
        Vault,
        ColumnCount
    };

    void setNewData(const QList<YellowbackPosition>& positions, int currentHeight);
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
    bool                    loading       = true;
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
        ColumnCount
    };

    void setNewData(const QList<YellowbackClaimable>& rows, int currentHeight);
    const YellowbackClaimable* rowAt(int row) const;

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
        ColumnCount
    };

    void setNewData(const QList<YellowbackAttestor>& rows, int currentHeight);
    const YellowbackAttestor* rowAt(int row) const;

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

// Yellowback transactions table, pattern of src/txtablemodel.h.
class YellowbackTxModel : public QAbstractTableModel {
public:
    YellowbackTxModel(QObject* parent);
    ~YellowbackTxModel();

    enum Column {
        Type = 0,
        Amount,
        Confirmations,
        Height,
        Verdict,
        Txid
    };

    void setNewData(const QList<YellowbackTx>& txs, int currentHeight);
    const YellowbackTx* txAt(int row) const;
    QString getTxId(int row) const;

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
