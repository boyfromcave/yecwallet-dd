#ifndef YDOLLARMODELS_H
#define YDOLLARMODELS_H

#include "precompiled.h"

using json = nlohmann::json;

// One row of yd_listpositions (plan §4.4). Amounts are integer cents / zatoshi as the node
// reports them; rendering happens in the model's data().
struct YDollarPosition {
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

    static YDollarPosition fromJson(const json& j);
};

// One row of yd_listtransactions (plan §4.4, E1/F6/D18).
struct YDollarTx {
    QString txid;
    int     height          = 0;
    int     confirmations   = 0;
    QString type;               // mint | send | receive | burn | redeem
    QString verdict;
    qint64  ydIn            = 0;
    qint64  ydOut           = 0;
    qint64  burned          = 0;
    qint64  amountCents     = 0;
    bool    expired         = false;

    static YDollarTx fromJson(const json& j);
};

// Tolerant readers for RPC replies: a missing or null field yields the default instead of
// throwing, so a node one contract revision ahead or behind degrades to blanks, not a crash.
namespace YDollarJson {
    qint64  toInt (const json& j, const char* key, qint64 def = 0);
    QString toStr (const json& j, const char* key, const QString& def = QString());
    bool    toBool(const json& j, const char* key, bool def = false);
    bool    isNull(const json& j, const char* key);
}

// Rendering helpers shared by the models and the tab. Pure functions of their arguments; the
// current height is passed in so the models never reach into the controller.
namespace YDollarFormat {
    QString cents(qint64 cents, bool showCentsUnit = false);
    QString zec(qint64 zat);
    QString heightWithEstimate(int height, int currentHeight);
    QDateTime estimateDate(int height, int currentHeight);
    QString tierName(int tier);
    QString tierRatio(int tier);
    QString typeLabel(const QString& type);
}

// Positions (vaults) table, pattern of src/balancestablemodel.h.
class YDollarPositionsModel : public QAbstractTableModel {
public:
    YDollarPositionsModel(QObject* parent);
    ~YDollarPositionsModel();

    enum Column {
        Status = 0,
        Minted,
        Collateral,
        Unlock,
        RequiredBurn,
        Redeemable,
        Vault
    };

    void setNewData(const QList<YDollarPosition>& positions, int currentHeight);
    const YDollarPosition* positionAt(int row) const;
    QList<YDollarPosition> redeemable() const;

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YDollarPosition>* modeldata     = nullptr;
    QList<QString>          headers;
    int                     currentHeight = 0;
    bool                    loading       = true;
};

// YDollar transactions table, pattern of src/txtablemodel.h.
class YDollarTxModel : public QAbstractTableModel {
public:
    YDollarTxModel(QObject* parent);
    ~YDollarTxModel();

    enum Column {
        Type = 0,
        Amount,
        Confirmations,
        Height,
        Verdict,
        Txid
    };

    void setNewData(const QList<YDollarTx>& txs, int currentHeight);
    const YDollarTx* txAt(int row) const;
    QString getTxId(int row) const;

    int      rowCount(const QModelIndex& parent) const override;
    int      columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<YDollarTx>* modeldata     = nullptr;
    QList<QString>    headers;
    int               currentHeight = 0;
    bool              loading       = true;
};

#endif // YDOLLARMODELS_H
