#include "yellowbackmodels.h"
#include "yellowbackrpc.h"
#include "settings.h"

// ── JSON readers ──────────────────────────────────────────────────────────────────────────

qint64 YellowbackJson::toInt(const json& j, const char* key, qint64 def) {
    if (!j.is_object() || j.find(key) == j.end() || j[key].is_null()) return def;
    const json& v = j[key];
    if (v.is_number_integer())  return v.get<json::number_integer_t>();
    if (v.is_number_unsigned()) return (qint64)v.get<json::number_unsigned_t>();
    if (v.is_number_float())    return (qint64)std::llround(v.get<double>());
    if (v.is_string())          return QString::fromStdString(v.get<json::string_t>()).toLongLong();
    if (v.is_boolean())         return v.get<bool>() ? 1 : 0;
    return def;
}

QString YellowbackJson::toStr(const json& j, const char* key, const QString& def) {
    if (!j.is_object() || j.find(key) == j.end() || j[key].is_null()) return def;
    const json& v = j[key];
    if (v.is_string()) return QString::fromStdString(v.get<json::string_t>());
    return QString::fromStdString(v.dump());
}

bool YellowbackJson::toBool(const json& j, const char* key, bool def) {
    if (!j.is_object() || j.find(key) == j.end() || j[key].is_null()) return def;
    const json& v = j[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number())  return toInt(j, key) != 0;
    return def;
}

bool YellowbackJson::isNull(const json& j, const char* key) {
    return !j.is_object() || j.find(key) == j.end() || j[key].is_null();
}

// ── Records ───────────────────────────────────────────────────────────────────────────────

YellowbackPosition YellowbackPosition::fromJson(const json& j) {
    using namespace YellowbackRpc::Position;
    YellowbackPosition p;
    p.vaultTxid         = YellowbackJson::toStr (j, VAULT_TXID);
    p.status            = YellowbackJson::toStr (j, STATUS);
    p.mintedCents       = YellowbackJson::toInt (j, MINTED_CENTS);
    p.collateralZat     = YellowbackJson::toInt (j, COLLATERAL_ZAT);
    p.lockHeight        = (int)YellowbackJson::toInt(j, LOCK_HEIGHT);
    p.tier              = (int)YellowbackJson::toInt(j, TIER);
    p.rosterIndex       = (int)YellowbackJson::toInt(j, ROSTER_INDEX);
    p.canRedeem         = YellowbackJson::toBool(j, CAN_REDEEM);
    p.requiredBurnCents = YellowbackJson::toInt (j, REQUIRED_BURN_CENTS);
    p.unlockHeight      = (int)YellowbackJson::toInt(j, UNLOCK_HEIGHT, p.lockHeight);
    p.mintHeight        = (int)YellowbackJson::toInt(j, MINT_HEIGHT);
    p.ownerKeyId        = YellowbackJson::toStr (j, OWNER_KEY_ID);
    p.pending           = YellowbackJson::toBool(j, PENDING);
    p.voidReason        = YellowbackJson::toStr (j, VOID_REASON);
    p.closeHeight       = (int)YellowbackJson::toInt(j, CLOSE_HEIGHT);
    p.closingTxid       = YellowbackJson::toStr (j, CLOSING_TXID);
    p.burnedCents       = YellowbackJson::toInt (j, BURNED_CENTS);
    return p;
}

YellowbackTx YellowbackTx::fromJson(const json& j) {
    using namespace YellowbackRpc::Transaction;
    YellowbackTx t;
    t.txid          = YellowbackJson::toStr (j, TXID);
    t.height        = (int)YellowbackJson::toInt(j, HEIGHT);
    t.confirmations = (int)YellowbackJson::toInt(j, CONFIRMATIONS);
    t.type          = YellowbackJson::toStr (j, TYPE);
    t.verdict       = YellowbackJson::toStr (j, VERDICT);
    t.yedIn         = YellowbackJson::toInt (j, YED_IN);
    t.yedOut        = YellowbackJson::toInt (j, YED_OUT);
    t.burned        = YellowbackJson::toInt (j, BURNED);
    t.amountCents   = YellowbackJson::toInt (j, AMOUNT_CENTS);
    t.expired       = YellowbackJson::toBool(j, EXPIRED) || t.verdict == VERDICT_EXPIRED;
    return t;
}

// ── Formatting ────────────────────────────────────────────────────────────────────────────

QString YellowbackFormat::cents(qint64 c, bool showCentsUnit) {
    if (showCentsUnit || Settings::getInstance()->getYellowbackUnitCents())
        return QString::number(c) % " ¢";

    QString sign = c < 0 ? "-" : "";
    qint64 a = c < 0 ? -c : c;
    QString whole = QLocale().toString((qlonglong)(a / 100));
    return sign % "$" % whole % "." % QString("%1").arg(a % 100, 2, 10, QChar('0'));
}

QString YellowbackFormat::zec(qint64 zat) {
    return Settings::getZECDisplayFormat((double)zat / 100000000.0);
}

QDateTime YellowbackFormat::estimateDate(int height, int currentHeight) {
    qint64 delta = (qint64)(height - currentHeight) * YellowbackRpc::SECONDS_PER_BLOCK;
    return QDateTime::currentDateTime().addSecs(delta);
}

QString YellowbackFormat::heightWithEstimate(int height, int currentHeight) {
    if (height <= 0) return "-";
    if (currentHeight <= 0) return QString::number(height);
    auto dt = estimateDate(height, currentHeight);
    return QString::number(height) % "  (~" % dt.toString("yyyy-MM-dd HH:mm") % ")";
}

QString YellowbackFormat::tierName(int tier) {
    switch (tier) {
        case 0: return QObject::tr("1 hour");
        case 1: return QObject::tr("30 days");
        case 2: return QObject::tr("90 days");
        case 3: return QObject::tr("180 days");
        case 4: return QObject::tr("1 year");
    }
    return QObject::tr("tier %1").arg(tier);
}

QString YellowbackFormat::tierRatio(int tier) {
    switch (tier) {
        case 0: return "1000 %";
        case 1: return "500 %";
        case 2: return "400 %";
        case 3: return "350 %";
        case 4: return "300 %";
    }
    return "?";
}

QString YellowbackFormat::typeLabel(const QString& type) {
    using namespace YellowbackRpc::Transaction;
    if (type == TYPE_MINT)    return QObject::tr("Mint");
    if (type == TYPE_SEND)    return QObject::tr("Sent");
    if (type == TYPE_RECEIVE) return QObject::tr("Received");
    if (type == TYPE_BURN)    return QObject::tr("Burn");
    if (type == TYPE_REDEEM)  return QObject::tr("Redeem");
    if (type == TYPE_TRANSFER) return QObject::tr("Sent");   // payload type name on expired rows
    return type;
}

// ── Positions model ───────────────────────────────────────────────────────────────────────

YellowbackPositionsModel::YellowbackPositionsModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Status") << tr("Minted") << tr("Collateral") << tr("Unlock height (est. date)")
            << tr("Required burn now") << tr("Redeemable") << tr("Vault");
    modeldata = new QList<YellowbackPosition>();
}

YellowbackPositionsModel::~YellowbackPositionsModel() {
    delete modeldata;
}

void YellowbackPositionsModel::setNewData(const QList<YellowbackPosition>& positions, int height) {
    loading = false;
    beginResetModel();
    *modeldata = positions;
    currentHeight = height;
    endResetModel();
}

const YellowbackPosition* YellowbackPositionsModel::positionAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

QList<YellowbackPosition> YellowbackPositionsModel::redeemable() const {
    QList<YellowbackPosition> out;
    for (auto& p : *modeldata)
        if (p.canRedeem) out.append(p);
    return out;
}

int YellowbackPositionsModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YellowbackPositionsModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YellowbackPositionsModel::data(const QModelIndex& index, int role) const {
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
        if (p.status == YellowbackRpc::Position::STATUS_VOID)   b.setColor(Qt::red);
        else if (p.status == YellowbackRpc::Position::STATUS_CLOSED) b.setColor(Qt::gray);
        else return QVariant();
        return b;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Status:       return p.status;
            case Minted:       return YellowbackFormat::cents(p.mintedCents);
            case Collateral:   return YellowbackFormat::zec(p.collateralZat);
            case Unlock:       return YellowbackFormat::heightWithEstimate(p.unlockHeight, currentHeight);
            case RequiredBurn: {
                if (p.status != YellowbackRpc::Position::STATUS_ACTIVE) return tr("none");
                QString s = YellowbackFormat::cents(p.requiredBurnCents);
                if (p.requiredBurnCents > p.mintedCents) s += tr(" (above minted)");
                return s;
            }
            case Redeemable:   return p.pending ? tr("redemption pending") : p.canRedeem ? tr("yes") : tr("no");
            case Vault:        return p.vaultTxid;
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case Status:
                if (p.status == YellowbackRpc::Position::STATUS_VOID)
                    return QString(tr("This mint was recorded as VOID by the Yellowback index: it created no YED. "
                                      "The collateral returns to you at the unlock height with no burn required.") %
                                   (p.voidReason.isEmpty() ? QString() : QString("\n" % tr("Reason: ") % p.voidReason)));
                if (p.status == YellowbackRpc::Position::STATUS_CLOSED)
                    return tr("Redeemed at height %1 by %2; %3 of YED were burned.")
                            .arg(p.closeHeight).arg(p.closingTxid).arg(YellowbackFormat::cents(p.burnedCents));
                return tr("Active: %1 of YED are backed by this vault.").arg(YellowbackFormat::cents(p.mintedCents));
            case RequiredBurn:
                return tr("The YED that must be destroyed to release the collateral, at the current "
                          "system health. During an emergency redemption ratio (health below 100 %) it can "
                          "exceed the amount minted. See the health figure on the Overview page.");
            case Unlock:
                return tr("Lock tier %1 (%2, ratio %3). Dates are estimates at 75 seconds per block.")
                        .arg(p.tier).arg(YellowbackFormat::tierName(p.tier)).arg(YellowbackFormat::tierRatio(p.tier));
            case Vault:
                return QString(p.vaultTxid % "\n" % tr("Owner key: ") % p.ownerKeyId % "\n" %
                               tr("Roster: ") % QString::number(p.rosterIndex));
        }
    }

    return QVariant();
}

QVariant YellowbackPositionsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::FontRole && orientation == Qt::Horizontal) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers.size())
        return headers.at(section);
    return QVariant();
}

// ── Transactions model ────────────────────────────────────────────────────────────────────

YellowbackTxModel::YellowbackTxModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Type") << tr("Amount") << tr("Confirmations") << tr("Height") << tr("Verdict") << tr("Txid");
    modeldata = new QList<YellowbackTx>();
}

YellowbackTxModel::~YellowbackTxModel() {
    delete modeldata;
}

void YellowbackTxModel::setNewData(const QList<YellowbackTx>& txs, int height) {
    loading = false;
    beginResetModel();
    *modeldata = txs;
    currentHeight = height;
    endResetModel();
}

const YellowbackTx* YellowbackTxModel::txAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

QString YellowbackTxModel::getTxId(int row) const {
    auto t = txAt(row);
    return t ? t->txid : QString();
}

int YellowbackTxModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YellowbackTxModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YellowbackTxModel::data(const QModelIndex& index, int role) const {
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
        if (t.type == YellowbackRpc::Transaction::TYPE_BURN) { b.setColor(QColor(200, 100, 0)); return b; }
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Type: {
                QString s = YellowbackFormat::typeLabel(t.type);
                if (t.expired) s += tr(" (expired)");
                return s;
            }
            case Amount: {
                qint64 a = t.amountCents;
                if (t.type == YellowbackRpc::Transaction::TYPE_SEND || t.type == YellowbackRpc::Transaction::TYPE_BURN)
                    a = -std::abs(a);
                return YellowbackFormat::cents(a);
            }
            case Confirmations: return t.expired ? tr("expired") : QString::number(t.confirmations);
            case Height:        return t.expired ? tr("expired") : t.height > 0 ? QString::number(t.height) : tr("unconfirmed");
            case Verdict:       return t.verdict;
            case Txid:          return t.txid;
        }
    }

    if (role == Qt::ToolTipRole) {
        if (t.expired)
            return tr("This transaction was never mined and has passed its expiry height. "
                      "It has no effect; any Yellowback it would have moved is still yours.");
        if (t.type == YellowbackRpc::Transaction::TYPE_BURN)
            return tr("A Yellowback output was spent as plain YEC. The Yellowback index treats the token as "
                      "destroyed (burned); only its YEC carrier value moved.");
        switch (index.column()) {
            case Amount:
                return tr("YED in: %1, out: %2, burned: %3")
                        .arg(YellowbackFormat::cents(t.yedIn)).arg(YellowbackFormat::cents(t.yedOut))
                        .arg(YellowbackFormat::cents(t.burned));
            case Verdict:
                return tr("The Yellowback index's verdict on this transaction (a stable identifier).");
            case Height:
                return YellowbackFormat::heightWithEstimate(t.height, currentHeight);
            default:
                return t.txid;
        }
    }

    return QVariant();
}

QVariant YellowbackTxModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::FontRole && orientation == Qt::Horizontal) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers.size())
        return headers.at(section);
    return QVariant();
}
