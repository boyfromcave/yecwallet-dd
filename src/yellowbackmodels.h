#ifndef YELLOWBACKMODELS_H
#define YELLOWBACKMODELS_H

#include "precompiled.h"

using json = nlohmann::json;

// One row of yed_listpositions (plan §4.4). Amounts are integer cents / zatoshi as the node
// reports them; rendering happens in the model's data().
struct YellowbackPosition {
    QString vaultTxid;
    QString status;             // ACTIVE | VOID | CLOSED
    qint64  mintedCents     = 0;
    qint64  collateralZat   = 0;
    int     lockHeight      = 0;
    int     tier            = 0;
    int     rosterIndex     = 0;
    bool    canRedeem       = false;
    qint64  requiredBurnCents = 0;
    int     unlockHeight    = 0;
    QString ownerKeyId;

    static YellowbackPosition fromJson(const json& j);
};

// One row of yed_listtransactions (plan §4.4, E1/F6/D18).
struct YellowbackTx {
    QString txid;
    int     height          = 0;
    int     confirmations   = 0;
    QString type;               // mint | send | receive | burn | redeem
    QString verdict;
    qint64  yedIn            = 0;
    qint64  yedOut           = 0;
    qint64  burned          = 0;
    qint64  amountCents     = 0;
    bool    expired         = false;

    static YellowbackTx fromJson(const json& j);
};

// Tolerant readers for RPC replies: a missing or null field yields the default instead of
// throwing, so a node one contract revision ahead or behind degrades to blanks, not a crash.
namespace YellowbackJson {
    qint64  toInt (const json& j, const char* key, qint64 def = 0);
    QString toStr (const json& j, const char* key, const QString& def = QString());
    bool    toBool(const json& j, const char* key, bool def = false);
    bool    isNull(const json& j, const char* key);
}

// Rendering helpers shared by the models and the tab. Pure functions of their arguments; the
// current height is passed in so the models never reach into the controller.
namespace YellowbackFormat {
    QString cents(qint64 cents, bool showCentsUnit = false);
    QString zec(qint64 zat);
    QString heightWithEstimate(int height, int currentHeight);
    QDateTime estimateDate(int height, int currentHeight);
    QString tierName(int tier);
    QString tierRatio(int tier);
    QString typeLabel(const QString& type);
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
        Unlock,
        RequiredBurn,
        Redeemable,
        Vault
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
