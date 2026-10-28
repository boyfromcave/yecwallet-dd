#ifndef STRINGSTABLEMODEL_H
#define STRINGSTABLEMODEL_H

#include "precompiled.h"
#include "yellowbackmodels.h"

struct TransactionItem;

class TxTableModel: public QAbstractTableModel
{
public:
    TxTableModel(QObject* parent);    
    ~TxTableModel();

    enum Column
    {
        Type = 0,
        Address = 1,
        Time = 2,
        Confirmations = 3,
        Amount = 4,
        Yed = 5          // Yellowback: the YED a Yellowback transaction moved for this wallet (DigiByte v9.26.7 wave 5)
    };

    // Yellowback (DigiByte v9.26.7 46a7c0de, wave 5 issues 13-16): stock listtransactions rows that are Yellowback
    // transactions, joined by txid to yed_listtransactions, are labelled for what they are (a mint locks collateral,
    // it does not send YEC away; a YED receipt is not a 0.0001 YEC payment) and carry their YED in the Yed column.
    void setYellowbackAnnotations(const QHash<QString, YellowbackTxAnnotation>& annotations);

    void addTData    (const QList<TransactionItem>& data);
    void addZSentData(const QList<TransactionItem>& data);
    void addZRecvData(const QList<TransactionItem>& data);     

    QString  getTxId(int row) const;
    QString  getMemo(int row) const;
    QString  getAddr(int row) const;
    qint64   getDate(int row) const;
    QString  getType(int row) const;
    qint64   getConfirmations(int row) const;
    QString  getAmt (int row) const;

    bool     exportToCsv(QString fileName) const;

    int      rowCount(const QModelIndex &parent) const;
    int      columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

private:
    void updateAllData();

    QList<TransactionItem>*  tTrans      = nullptr;
    QList<TransactionItem>*  zrTrans     = nullptr;     // Z received
    QList<TransactionItem>*  zsTrans     = nullptr;     // Z sent

    QList<TransactionItem>* modeldata    = nullptr;

    QList<QString>           headers;
    QHash<QString, YellowbackTxAnnotation> ybAnnotations;
};


#endif // STRINGSTABLEMODEL_H
