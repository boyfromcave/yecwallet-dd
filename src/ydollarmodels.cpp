#include "ydollarmodels.h"
#include "ydollarrpc.h"
#include "settings.h"

// ── JSON readers ──────────────────────────────────────────────────────────────────────────

qint64 YDollarJson::toInt(const json& j, const char* key, qint64 def) {
    if (!j.is_object() || !j.contains(key) || j[key].is_null()) return def;
    const json& v = j[key];
    if (v.is_number_integer())  return v.get<json::number_integer_t>();
    if (v.is_number_unsigned()) return (qint64)v.get<json::number_unsigned_t>();
    if (v.is_number_float())    return (qint64)std::llround(v.get<double>());
    if (v.is_string())          return QString::fromStdString(v.get<json::string_t>()).toLongLong();
    if (v.is_boolean())         return v.get<bool>() ? 1 : 0;
    return def;
}

QString YDollarJson::toStr(const json& j, const char* key, const QString& def) {
    if (!j.is_object() || !j.contains(key) || j[key].is_null()) return def;
    const json& v = j[key];
    if (v.is_string()) return QString::fromStdString(v.get<json::string_t>());
    return QString::fromStdString(v.dump());
}

bool YDollarJson::toBool(const json& j, const char* key, bool def) {
    if (!j.is_object() || !j.contains(key) || j[key].is_null()) return def;
    const json& v = j[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number())  return toInt(j, key) != 0;
    return def;
}

bool YDollarJson::isNull(const json& j, const char* key) {
    return !j.is_object() || !j.contains(key) || j[key].is_null();
}

// ── Records ───────────────────────────────────────────────────────────────────────────────

YDollarPosition YDollarPosition::fromJson(const json& j) {
    using namespace YDollarRpc::Position;
    YDollarPosition p;
    p.vaultTxid         = YDollarJson::toStr (j, VAULT_TXID);
    p.status            = YDollarJson::toStr (j, STATUS);
    p.mintedCents       = YDollarJson::toInt (j, MINTED_CENTS);
    p.collateralZat     = YDollarJson::toInt (j, COLLATERAL_ZAT);
    p.lockHeight        = (int)YDollarJson::toInt(j, LOCK_HEIGHT);
    p.tier              = (int)YDollarJson::toInt(j, TIER);
    p.rosterIndex       = (int)YDollarJson::toInt(j, ROSTER_INDEX);
    p.canRedeem         = YDollarJson::toBool(j, CAN_REDEEM);
    p.requiredBurnCents = YDollarJson::toInt (j, REQUIRED_BURN_CENTS);
    p.unlockHeight      = (int)YDollarJson::toInt(j, UNLOCK_HEIGHT);
    p.ownerKeyId        = YDollarJson::toStr (j, OWNER_KEY_ID);
    return p;
}

YDollarTx YDollarTx::fromJson(const json& j) {
    using namespace YDollarRpc::Transaction;
    YDollarTx t;
    t.txid          = YDollarJson::toStr (j, TXID);
    t.height        = (int)YDollarJson::toInt(j, HEIGHT);
    t.confirmations = (int)YDollarJson::toInt(j, CONFIRMATIONS);
    t.type          = YDollarJson::toStr (j, TYPE);
    t.verdict       = YDollarJson::toStr (j, VERDICT);
    t.ydIn          = YDollarJson::toInt (j, YD_IN);
    t.ydOut         = YDollarJson::toInt (j, YD_OUT);
    t.burned        = YDollarJson::toInt (j, BURNED);
    t.amountCents   = YDollarJson::toInt (j, AMOUNT_CENTS);
    t.expired       = YDollarJson::toBool(j, EXPIRED);
    return t;
}

// ── Formatting ────────────────────────────────────────────────────────────────────────────

QString YDollarFormat::cents(qint64 c, bool showCentsUnit) {
    if (showCentsUnit || Settings::getInstance()->getYDollarUnitCents())
        return QString::number(c) % " ¢";

    QString sign = c < 0 ? "-" : "";
    qint64 a = c < 0 ? -c : c;
    QString whole = QLocale().toString((qlonglong)(a / 100));
    return sign % "$" % whole % "." % QString("%1").arg(a % 100, 2, 10, QChar('0'));
}

QString YDollarFormat::zec(qint64 zat) {
    return Settings::getZECDisplayFormat((double)zat / 100000000.0);
}

QDateTime YDollarFormat::estimateDate(int height, int currentHeight) {
    qint64 delta = (qint64)(height - currentHeight) * YDollarRpc::SECONDS_PER_BLOCK;
    return QDateTime::currentDateTime().addSecs(delta);
}

QString YDollarFormat::heightWithEstimate(int height, int currentHeight) {
    if (height <= 0) return "-";
    if (currentHeight <= 0) return QString::number(height);
    auto dt = estimateDate(height, currentHeight);
    return QString::number(height) % "  (~" % dt.toString("yyyy-MM-dd HH:mm") % ")";
}

QString YDollarFormat::tierName(int tier) {
    switch (tier) {
        case 0: return QObject::tr("1 hour");
        case 1: return QObject::tr("30 days");
        case 2: return QObject::tr("90 days");
        case 3: return QObject::tr("180 days");
        case 4: return QObject::tr("1 year");
    }
    return QObject::tr("tier %1").arg(tier);
}

QString YDollarFormat::tierRatio(int tier) {
    switch (tier) {
        case 0: return "1000 %";
        case 1: return "500 %";
        case 2: return "400 %";
        case 3: return "350 %";
        case 4: return "300 %";
    }
    return "?";
}

QString YDollarFormat::typeLabel(const QString& type) {
    using namespace YDollarRpc::Transaction;
    if (type == TYPE_MINT)    return QObject::tr("Mint");
    if (type == TYPE_SEND)    return QObject::tr("Sent");
    if (type == TYPE_RECEIVE) return QObject::tr("Received");
    if (type == TYPE_BURN)    return QObject::tr("Burn");
    if (type == TYPE_REDEEM)  return QObject::tr("Redeem");
    return type;
}

// ── Positions model ───────────────────────────────────────────────────────────────────────

YDollarPositionsModel::YDollarPositionsModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Status") << tr("Minted") << tr("Collateral") << tr("Unlock height (est. date)")
            << tr("Required burn now") << tr("Redeemable") << tr("Vault");
    modeldata = new QList<YDollarPosition>();
}

YDollarPositionsModel::~YDollarPositionsModel() {
    delete modeldata;
}

void YDollarPositionsModel::setNewData(const QList<YDollarPosition>& positions, int height) {
    loading = false;
    beginResetModel();
    *modeldata = positions;
    currentHeight = height;
    endResetModel();
}

const YDollarPosition* YDollarPositionsModel::positionAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

QList<YDollarPosition> YDollarPositionsModel::redeemable() const {
    QList<YDollarPosition> out;
    for (auto& p : *modeldata)
        if (p.canRedeem) out.append(p);
    return out;
}

int YDollarPositionsModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YDollarPositionsModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YDollarPositionsModel::data(const QModelIndex& index, int role) const {
    if (loading) {
        if (role == Qt::DisplayRole && index.column() == 0) return tr("Loading...");
        return QVariant();
    }
    if (index.row() >= modeldata->size()) return QVariant();
    const auto& p = modeldata->at(index.row());

    if (role == Qt::TextAlignmentRole &&
        (index.column() == Minted || index.column() == Collateral || index.column() == RequiredBurn))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::ForegroundRole) {
        QBrush b;
        if (p.status == YDollarRpc::Position::STATUS_VOID)   b.setColor(Qt::red);
        else if (p.status == YDollarRpc::Position::STATUS_CLOSED) b.setColor(Qt::gray);
        else return QVariant();
        return b;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Status:       return p.status;
            case Minted:       return YDollarFormat::cents(p.mintedCents);
            case Collateral:   return YDollarFormat::zec(p.collateralZat);
            case Unlock:       return YDollarFormat::heightWithEstimate(p.unlockHeight, currentHeight);
            case RequiredBurn: {
                if (p.status != YDollarRpc::Position::STATUS_ACTIVE) return tr("none");
                QString s = YDollarFormat::cents(p.requiredBurnCents);
                if (p.requiredBurnCents > p.mintedCents) s += tr(" (above minted)");
                return s;
            }
            case Redeemable:   return p.canRedeem ? tr("yes") : tr("no");
            case Vault:        return p.vaultTxid;
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case Status:
                if (p.status == YDollarRpc::Position::STATUS_VOID)
                    return tr("This mint was recorded as VOID by the YDollar index: it created no YDollar. "
                              "The collateral returns to you at the unlock height with no burn required.");
                if (p.status == YDollarRpc::Position::STATUS_CLOSED)
                    return tr("Redeemed: the collateral has been released.");
                return tr("Active: %1 of YDollar are backed by this vault.").arg(YDollarFormat::cents(p.mintedCents));
            case RequiredBurn:
                return tr("The YDollar that must be destroyed to release the collateral, at the current "
                          "system health. During an emergency redemption ratio (health below 100 %) it can "
                          "exceed the amount minted. See the health figure on the Overview page.");
            case Unlock:
                return tr("Lock tier %1 (%2, ratio %3). Dates are estimates at 75 seconds per block.")
                        .arg(p.tier).arg(YDollarFormat::tierName(p.tier)).arg(YDollarFormat::tierRatio(p.tier));
            case Vault:
                return p.vaultTxid % "\n" % tr("Owner key: ") % p.ownerKeyId % "\n" %
                       tr("Roster: ") % QString::number(p.rosterIndex);
        }
    }

    return QVariant();
}

QVariant YDollarPositionsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::FontRole && orientation == Qt::Horizontal) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers.size())
        return headers.at(section);
    return QVariant();
}

// ── Transactions model ────────────────────────────────────────────────────────────────────

YDollarTxModel::YDollarTxModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Type") << tr("Amount") << tr("Confirmations") << tr("Height") << tr("Verdict") << tr("Txid");
    modeldata = new QList<YDollarTx>();
}

YDollarTxModel::~YDollarTxModel() {
    delete modeldata;
}

void YDollarTxModel::setNewData(const QList<YDollarTx>& txs, int height) {
    loading = false;
    beginResetModel();
    *modeldata = txs;
    currentHeight = height;
    endResetModel();
}

const YDollarTx* YDollarTxModel::txAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

QString YDollarTxModel::getTxId(int row) const {
    auto t = txAt(row);
    return t ? t->txid : QString();
}

int YDollarTxModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YDollarTxModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YDollarTxModel::data(const QModelIndex& index, int role) const {
    if (loading) {
        if (role == Qt::DisplayRole && index.column() == 0) return tr("Loading...");
        return QVariant();
    }
    if (index.row() >= modeldata->size()) return QVariant();
    const auto& t = modeldata->at(index.row());

    if (role == Qt::TextAlignmentRole &&
        (index.column() == Amount || index.column() == Confirmations || index.column() == Height))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::ForegroundRole) {
        QBrush b;
        if (t.expired || t.confirmations <= 0) { b.setColor(Qt::red); return b; }
        if (t.type == YDollarRpc::Transaction::TYPE_BURN) { b.setColor(QColor(200, 100, 0)); return b; }
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Type: {
                QString s = YDollarFormat::typeLabel(t.type);
                if (t.expired) s += tr(" (expired)");
                return s;
            }
            case Amount: {
                qint64 a = t.amountCents;
                if (t.type == YDollarRpc::Transaction::TYPE_SEND || t.type == YDollarRpc::Transaction::TYPE_BURN)
                    a = -std::abs(a);
                return YDollarFormat::cents(a);
            }
            case Confirmations: return t.expired ? tr("expired") : QString::number(t.confirmations);
            case Height:        return t.height > 0 ? QString::number(t.height) : tr("unconfirmed");
            case Verdict:       return t.verdict;
            case Txid:          return t.txid;
        }
    }

    if (role == Qt::ToolTipRole) {
        if (t.expired)
            return tr("This transaction was never mined and has passed its expiry height. "
                      "It has no effect; any YDollar it would have moved is still yours.");
        if (t.type == YDollarRpc::Transaction::TYPE_BURN)
            return tr("A YDollar output was spent as plain YEC. The YDollar index treats the token as "
                      "destroyed (burned); only its YEC carrier value moved.");
        switch (index.column()) {
            case Amount:
                return tr("YDollar in: %1, out: %2, burned: %3")
                        .arg(YDollarFormat::cents(t.ydIn)).arg(YDollarFormat::cents(t.ydOut))
                        .arg(YDollarFormat::cents(t.burned));
            case Verdict:
                return tr("The YDollar index's verdict on this transaction (a stable identifier).");
            case Height:
                return YDollarFormat::heightWithEstimate(t.height, currentHeight);
            default:
                return t.txid;
        }
    }

    return QVariant();
}

QVariant YDollarTxModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::FontRole && orientation == Qt::Horizontal) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers.size())
        return headers.at(section);
    return QVariant();
}
