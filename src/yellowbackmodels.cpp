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

bool YellowbackJson::has(const json& j, const char* key) {
    return !isNull(j, key);
}

const json& YellowbackJson::obj(const json& j, const char* key) {
    static const json empty = json::object();
    if (!j.is_object() || j.find(key) == j.end() || !j[key].is_object()) return empty;
    return j[key];
}

QStringList YellowbackJson::strings(const json& j, const char* key) {
    QStringList out;
    if (!j.is_object() || j.find(key) == j.end() || !j[key].is_array()) return out;
    for (auto& it : j[key])
        if (it.is_string()) out << QString::fromStdString(it.get<json::string_t>());
    return out;
}

// ── Records ───────────────────────────────────────────────────────────────────────────────

YellowbackPosition YellowbackPosition::fromJson(const json& j) {
    using namespace YellowbackRpc::Position;
    YellowbackPosition p;
    p.txid          = YellowbackJson::toStr (j, TXID);
    p.vout          = (int)YellowbackJson::toInt(j, VOUT);
    p.status        = YellowbackJson::toStr (j, STATUS);
    p.ownerAddress  = YellowbackJson::toStr (j, OWNER_ADDRESS);
    p.ownerKeyId    = YellowbackJson::toStr (j, OWNER_KEY_ID);
    p.termClass     = YellowbackJson::toStr (j, TERM_CLASS);
    p.lockHeight    = (int)YellowbackJson::toInt(j, LOCK_HEIGHT);
    p.claimHeight   = (int)YellowbackJson::toInt(j, CLAIM_HEIGHT);
    p.collateralZat = YellowbackJson::toInt (j, COLLATERAL_ZAT);
    p.mintedCents   = YellowbackJson::toInt (j, MINTED_CENTS);
    p.mintHeight    = (int)YellowbackJson::toInt(j, MINT_HEIGHT);
    p.refHeight     = (int)YellowbackJson::toInt(j, REF_HEIGHT);
    p.feePaidZat    = YellowbackJson::toInt (j, FEE_PAID_ZAT);
    p.closeHeight   = (int)YellowbackJson::toInt(j, CLOSE_HEIGHT, -1);
    p.closingTxid   = YellowbackJson::toStr (j, CLOSING_TXID);
    p.burnedCents   = YellowbackJson::toInt (j, BURNED_CENTS);
    p.unbacked      = YellowbackJson::toBool(j, UNBACKED);
    p.claimable     = YellowbackJson::toBool(j, CLAIMABLE);
    p.underwaterAt  = YellowbackJson::toInt (j, UNDERWATER_AT, -1);
    p.voidReason    = YellowbackJson::toStr (j, VOID_REASON);
    p.sweepBefore   = (int)YellowbackJson::toInt(j, SWEEP_BEFORE, -1);
    p.canRedeem     = YellowbackJson::toBool(j, CAN_REDEEM);
    p.canClaim      = YellowbackJson::toBool(j, CAN_CLAIM);
    p.canSweep      = YellowbackJson::toBool(j, CAN_SWEEP);
    p.noticed       = YellowbackJson::toBool(j, NOTICED);
    p.noticeHeight  = (int)YellowbackJson::toInt(j, NOTICE_HEIGHT, -1);
    p.emergencyOpenAt = (int)YellowbackJson::toInt(j, EMERGENCY_OPEN_AT, -1);
    p.canNotice     = YellowbackJson::toBool(j, CAN_NOTICE);
    return p;
}

YellowbackAttestor YellowbackAttestor::fromJson(const json& j) {
    using namespace YellowbackRpc::Attestor;
    YellowbackAttestor a;
    a.seq             = (int)YellowbackJson::toInt(j, SEQ);
    a.status          = YellowbackJson::toStr (j, STATUS);
    a.statusHeight    = (int)YellowbackJson::toInt(j, STATUS_HEIGHT);
    a.attestorPubKey  = YellowbackJson::toStr (j, ATTESTOR_PUBKEY);
    a.bondAddress     = YellowbackJson::toStr (j, BOND_ADDRESS);
    a.bondKeyAddress  = YellowbackJson::toStr (j, BOND_KEY_ADDRESS);
    a.bondZat         = YellowbackJson::toInt (j, BOND_ZAT);
    a.bondLocktime    = (int)YellowbackJson::toInt(j, BOND_LOCKTIME);
    a.bondSpentHeight = (int)YellowbackJson::toInt(j, BOND_SPENT_HEIGHT, -1);
    a.registerHeight  = (int)YellowbackJson::toInt(j, REGISTER_HEIGHT);
    a.weight          = YellowbackJson::toStr (j, WEIGHT, "0");
    a.seated          = YellowbackJson::toBool(j, SEATED);
    a.seatedSince     = (int)YellowbackJson::toInt(j, SEATED_SINCE, -1);
    a.pinned          = YellowbackJson::toBool(j, PINNED);
    a.founding        = YellowbackJson::toBool(j, FOUNDING);
    const json& flags = YellowbackJson::obj(j, FLAGS);
    a.tier            = (int)YellowbackJson::toInt(flags, YellowbackRpc::AttestorFlags::TIER);
    a.pool            = YellowbackJson::toBool(flags, YellowbackRpc::AttestorFlags::POOL);
    a.lastBundleHeight = (int)YellowbackJson::toInt(j, LAST_BUNDLE_HEIGHT, -1);
    a.poolFresh       = YellowbackJson::toBool(j, POOL_FRESH);
    return a;
}

YellowbackClaimable YellowbackClaimable::fromJson(const json& j) {
    using namespace YellowbackRpc::Claimable;
    YellowbackClaimable c;
    c.vault         = YellowbackJson::toStr (j, VAULT);
    c.ownerAddress  = YellowbackJson::toStr (j, OWNER_ADDRESS);
    c.collateralZat = YellowbackJson::toInt (j, COLLATERAL_ZAT);
    c.mintedCents   = YellowbackJson::toInt (j, MINTED_CENTS);
    c.feeZat        = YellowbackJson::toInt (j, FEE_ZAT);
    c.claimHeight   = (int)YellowbackJson::toInt(j, CLAIM_HEIGHT);
    c.underwaterAt  = YellowbackJson::toInt (j, UNDERWATER_AT);
    c.pClaim        = YellowbackJson::toInt (j, P_CLAIM);
    c.claimPath     = YellowbackJson::toStr (j, CLAIM_PATH);
    c.residualZat   = YellowbackJson::toInt (j, RESIDUAL_ZAT);
    c.attestFeeZat  = YellowbackJson::toInt (j, ATTEST_FEE_ZAT);
    c.noticed       = YellowbackJson::toBool(j, NOTICED);
    c.noticeHeight  = (int)YellowbackJson::toInt(j, NOTICE_HEIGHT, -1);
    c.emergencyOpenAt = (int)YellowbackJson::toInt(j, EMERGENCY_OPEN_AT, -1);
    return c;
}

YellowbackTx YellowbackTx::fromJson(const json& j) {
    using namespace YellowbackRpc::Transaction;
    YellowbackTx t;
    t.txid          = YellowbackJson::toStr (j, TXID);
    t.height        = (int)YellowbackJson::toInt(j, HEIGHT);
    t.confirmations = (int)YellowbackJson::toInt(j, CONFIRMATIONS);
    t.type          = YellowbackJson::toStr (j, TYPE);
    t.verdict       = YellowbackJson::toStr (j, VERDICT);
    t.path          = YellowbackJson::toStr (j, PATH);
    t.yedIn         = YellowbackJson::toInt (j, YED_IN);
    t.yedOut        = YellowbackJson::toInt (j, YED_OUT);
    t.burned        = YellowbackJson::toInt (j, BURNED);
    t.amountCents   = YellowbackJson::toInt (j, AMOUNT_CENTS);
    t.feeZat        = YellowbackJson::toInt (j, FEE_ZAT);
    t.payee         = YellowbackJson::toStr (j, PAYEE);
    t.unbacked      = YellowbackJson::toBool(j, UNBACKED);
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

QString YellowbackFormat::price(qint64 microUsd) {
    return "$" % QString::number((double)microUsd / 1000000.0, 'f', 4);
}

QString YellowbackFormat::priceOrUndefined(const json& j, const char* key) {
    if (YellowbackJson::isNull(j, key)) return QObject::tr("undefined");
    return price(YellowbackJson::toInt(j, key));
}

QString YellowbackFormat::bpsAsMultiplier(qint64 bps) {
    return QString::number((double)bps / 10000.0, 'f', 2) % "x";
}

QString YellowbackFormat::bpsAsPercent(qint64 bps) {
    return QString::number((double)bps / 100.0, 'f', 2) % " %";
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

QString YellowbackFormat::typeLabel(const QString& type) {
    using namespace YellowbackRpc::Transaction;
    if (type == TYPE_MINT)    return QObject::tr("Mint");
    if (type == TYPE_SEND)    return QObject::tr("Sent");
    if (type == TYPE_RECEIVE) return QObject::tr("Received");
    if (type == TYPE_BURN)    return QObject::tr("Burn");
    if (type == TYPE_REDEEM)  return QObject::tr("Redeem");
    if (type == TYPE_CLAIM)   return QObject::tr("Claim");
    if (type == TYPE_CLAIMED) return QObject::tr("Vault claimed");
    if (type == TYPE_SWEEP)   return QObject::tr("Sweep");
    if (type == TYPE_NOTICE)  return QObject::tr("Claim notice");
    if (type == TYPE_NOTICED) return QObject::tr("Vault noticed");
    if (type == TYPE_REGISTER)return QObject::tr("Attestor registration");
    if (type == TYPE_EQUIVOCATION) return QObject::tr("Equivocation report");
    if (type == TYPE_REVIVE)  return QObject::tr("Attestor revival");
    return type;
}

// One sentence per haltMask name (§3.6, MINTPOL-1). The name itself stays in the text so a
// user can match it to the node's mintpol-* error identifiers.
QString YellowbackFormat::haltReason(const QString& name) {
    using namespace YellowbackRpc::Stats;
    if (name == HALT_NOT_ACTIVE)
        return QObject::tr("NOT_ACTIVE: Yellowback has not activated on this chain yet (pools are still signalling).");
    if (name == HALT_NO_PRICE)
        return QObject::tr("NO_PRICE: the mint price is undefined because too few recent blocks carry a price quote.");
    if (name == HALT_PARTICIPATION)
        return QObject::tr("PARTICIPATION: fewer than 60 % of recent blocks signal enforcement; minting pauses until 75 % do.");
    if (name == HALT_GLOBAL_RATIO)
        return QObject::tr("GLOBAL_RATIO: the system-wide collateral ratio is below its floor.");
    if (name == HALT_DIVERGENCE)
        return QObject::tr("DIVERGENCE: the fast and slow price medians disagree by more than the allowed band.");
    if (name == HALT_ENFORCEMENT)
        return QObject::tr("ENFORCEMENT: fewer than half of recent blocks signal, so block rejection is suspended.");
    return name;
}

QString YellowbackFormat::voidReason(const QString& verdict) {
    if (verdict.isEmpty()) return QString();
    return QObject::tr("The mint failed the rule \"%1\" and created no YED. The collateral stays yours: "
                       "release it at the lock height (Release, no burn, no fee).").arg(verdict);
}

QString YellowbackFormat::sourceTier(int tier) {
    switch (tier) {
        case 0:  return QObject::tr("exchange APIs");
        case 1:  return QObject::tr("mixed");
        case 2:  return QObject::tr("aggregator");
        default: return QObject::tr("tier %1").arg(tier);
    }
}

// ── Positions model ───────────────────────────────────────────────────────────────────────

YellowbackPositionsModel::YellowbackPositionsModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Status") << tr("Minted") << tr("Collateral") << tr("Class")
            << tr("Lock height (est. date)") << tr("Claim height (est. date)")
            << tr("Claimable") << tr("Unbacked") << tr("Sweep before") << tr("Notice") << tr("Vault");
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
    using namespace YellowbackRpc::Position;
    if (loading) {
        if (role == Qt::DisplayRole && index.column() == 0) return tr("Loading...");
        return QVariant();
    }
    if (index.row() >= modeldata->size()) return QVariant();
    const auto& p = modeldata->at(index.row());

    if (role == Qt::TextAlignmentRole &&
        (index.column() == Minted || index.column() == Collateral))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::ForegroundRole) {
        QBrush b;
        if (p.status == STATUS_VOID)                              b.setColor(Qt::red);
        else if (p.sweepBefore > 0 && p.status == STATUS_ACTIVE)  b.setColor(QColor(200, 100, 0));
        else if (p.status == STATUS_CLOSED || p.status == STATUS_CLAIMED) b.setColor(Qt::gray);
        else return QVariant();
        return b;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Status: {
                QString s = p.status;
                if (p.status == STATUS_VOID && !p.voidReason.isEmpty()) s += " (" % p.voidReason % ")";
                return s;
            }
            case Minted:       return YellowbackFormat::cents(p.mintedCents);
            case Collateral:   return YellowbackFormat::zec(p.collateralZat);
            case TermClass:    return p.termClass;
            case LockHeight:   return YellowbackFormat::heightWithEstimate(p.lockHeight, currentHeight);
            case ClaimHeight:  return YellowbackFormat::heightWithEstimate(p.claimHeight, currentHeight);
            case Claimable:    return p.claimable ? tr("yes") : tr("no");
            case Unbacked:     return p.unbacked ? tr("yes") : tr("no");
            case SweepBefore:  return p.sweepBefore > 0 ? YellowbackFormat::heightWithEstimate(p.sweepBefore, currentHeight) : QString("-");
            case Notice:
                if (p.noticed)   return p.emergencyOpenAt >= 0
                                     ? tr("noticed, emergency claim from %1").arg(p.emergencyOpenAt)
                                     : tr("noticed");
                if (p.canNotice) return tr("notice possible");
                return QString("-");
            case Vault:        return p.vaultName();
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case Status:
                if (p.status == STATUS_VOID)
                    return YellowbackFormat::voidReason(p.voidReason.isEmpty() ? tr("(no reason returned)") : p.voidReason);
                if (p.status == STATUS_CLOSED)
                    return p.unbacked
                        ? tr("Closed at height %1 by %2 without burning its debt: the %3 of YED minted against it are unbacked.")
                              .arg(p.closeHeight).arg(p.closingTxid).arg(YellowbackFormat::cents(p.mintedCents))
                        : tr("Redeemed at height %1 by %2; %3 of YED were burned.")
                              .arg(p.closeHeight).arg(p.closingTxid).arg(YellowbackFormat::cents(p.burnedCents));
                if (p.status == STATUS_CLAIMED)
                    return tr("Claimed at height %1 by %2: the vault was underwater past its claim height and someone burned %3 of YED to take the collateral.")
                              .arg(p.closeHeight).arg(p.closingTxid).arg(YellowbackFormat::cents(p.burnedCents));
                return tr("Active: %1 of YED are backed by this vault. Releasing the collateral burns exactly that debt and pays an enforcement fee to a quoting pool.")
                          .arg(YellowbackFormat::cents(p.mintedCents));
            case Claimable:
                return p.underwaterAt >= 0
                    ? tr("Past the claim height, anyone may burn the vault's debt and take its collateral once the claim price falls below %1 per YEC.")
                          .arg(YellowbackFormat::price(p.underwaterAt))
                    : tr("A VOID vault carries no debt and can never be claimed for one; its claim path is open to anyone after the claim height (see Sweep before).");
            case Unbacked:
                return tr("\"yes\" means this vault was closed without burning its debt (a sweep under abandonment); the YED minted against it are no longer backed.");
            case SweepBefore:
                if (p.status == STATUS_VOID)
                    return tr("After height %1 this vault's claim path can be spent by anyone. Release the collateral before then.").arg(p.sweepBefore);
                if (p.sweepBefore > 0)
                    return tr("Enforcement has been abandoned: after height %1 the claim path is open to anyone and nobody polices the burn. Sweep the collateral before then or lose it to whoever claims first.").arg(p.sweepBefore);
                return tr("Not applicable while enforcement holds.");
            case Notice:
                if (p.noticed)
                    return tr("A claim notice against this vault confirmed at height %1: someone judged it under the emergency ratio by one price source. "
                              "If it stays there, a claim citing reference height %2 or later may take the collateral even though the combined price has not put it underwater.")
                              .arg(p.noticeHeight).arg(p.emergencyOpenAt);
                if (p.canNotice)
                    return tr("Under the attested prices this node holds, this vault is below the emergency ratio: a claim notice could be posted against it.");
                return tr("No claim notice stands against this vault.");
            case LockHeight:
                return tr("Term class %1. Before the lock height the collateral cannot leave the vault; that is enforced by every Ycash node. Dates are estimates at 75 seconds per block.").arg(p.termClass);
            case ClaimHeight:
                return tr("Lock height plus the grace period. After it, the vault's claim path opens: anyone who burns the debt can take the collateral if the vault is underwater.");
            case Vault:
                return QString(p.vaultName() % "\n" % tr("Owner: ") % p.ownerAddress % "\n" % tr("Owner key: ") % p.ownerKeyId %
                               "\n" % tr("Minted at height %1 (reference height %2), enforcement fee paid %3")
                                   .arg(p.mintHeight).arg(p.refHeight).arg(YellowbackFormat::zec(p.feePaidZat)));
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

// ── Claimable model ───────────────────────────────────────────────────────────────────────

YellowbackClaimableModel::YellowbackClaimableModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Vault") << tr("Owner") << tr("Collateral") << tr("YED to burn") << tr("Fee")
            << tr("Claim height") << tr("Underwater below") << tr("Claim price now");
    modeldata = new QList<YellowbackClaimable>();
}

YellowbackClaimableModel::~YellowbackClaimableModel() {
    delete modeldata;
}

void YellowbackClaimableModel::setNewData(const QList<YellowbackClaimable>& rows, int height) {
    loading = false;
    beginResetModel();
    *modeldata = rows;
    currentHeight = height;
    endResetModel();
}

const YellowbackClaimable* YellowbackClaimableModel::rowAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

int YellowbackClaimableModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YellowbackClaimableModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YellowbackClaimableModel::data(const QModelIndex& index, int role) const {
    if (loading) {
        if (role == Qt::DisplayRole && index.column() == 0) return tr("Loading...");
        return QVariant();
    }
    if (index.row() >= modeldata->size()) return QVariant();
    const auto& c = modeldata->at(index.row());

    if (role == Qt::TextAlignmentRole &&
        (index.column() == Collateral || index.column() == Burn || index.column() == Fee))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Vault:        return c.vault;
            case Owner:        return c.ownerAddress;
            case Collateral:   return YellowbackFormat::zec(c.collateralZat);
            case Burn:         return YellowbackFormat::cents(c.mintedCents);
            case Fee:          return YellowbackFormat::zec(c.feeZat);
            case ClaimHeight:  return QString::number(c.claimHeight);
            case UnderwaterAt: return YellowbackFormat::price(c.underwaterAt);
            case PClaim:       return YellowbackFormat::price(c.pClaim);
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case Burn:
                return tr("A claim must burn exactly the vault's debt (its minted YED) from your own YED.");
            case Fee:
                return tr("The enforcement fee, paid from the collateral to a pool that published a price quote recently; you receive the rest.");
            case UnderwaterAt:
                return tr("The claim price below which this vault is underwater. It is claimable while the current claim price is below it.");
            default:
                return c.vault;
        }
    }
    return QVariant();
}

QVariant YellowbackClaimableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::FontRole && orientation == Qt::Horizontal) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers.size())
        return headers.at(section);
    return QVariant();
}

// ── Attestors model (v3) ──────────────────────────────────────────────────────────────────

YellowbackAttestorsModel::YellowbackAttestorsModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Seq") << tr("Status") << tr("Seated") << tr("Bond (lock height)") << tr("Weight")
            << tr("Seated since") << tr("Founding") << tr("Sources") << tr("Last bundle") << tr("In pool");
    modeldata = new QList<YellowbackAttestor>();
}

YellowbackAttestorsModel::~YellowbackAttestorsModel() {
    delete modeldata;
}

void YellowbackAttestorsModel::setNewData(const QList<YellowbackAttestor>& rows, int height) {
    loading = false;
    beginResetModel();
    *modeldata = rows;
    currentHeight = height;
    endResetModel();
}

const YellowbackAttestor* YellowbackAttestorsModel::rowAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

int YellowbackAttestorsModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YellowbackAttestorsModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YellowbackAttestorsModel::data(const QModelIndex& index, int role) const {
    using namespace YellowbackRpc::Attestor;
    if (loading) {
        if (role == Qt::DisplayRole && index.column() == 0) return tr("Loading...");
        return QVariant();
    }
    if (index.row() >= modeldata->size()) return QVariant();
    const auto& a = modeldata->at(index.row());

    if (role == Qt::TextAlignmentRole && (index.column() == Bond || index.column() == Weight))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::ForegroundRole) {
        QBrush b;
        if (a.status == STATUS_EJECTED || a.status == STATUS_WITHDRAWN) b.setColor(Qt::gray);
        else if (a.status == STATUS_DORMANT)                            b.setColor(QColor(200, 100, 0));
        else if (a.pinned)                                              b.setColor(Qt::red);
        else return QVariant();
        return b;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Seq:         return QString::number(a.seq);
            case Status:      return a.status;
            case Seated: {
                QStringList marks;
                if (a.seated) marks << tr("seated");
                if (a.pinned) marks << tr("pinned");
                return marks.isEmpty() ? QString("-") : marks.join(", ");
            }
            case Bond:        return QString(YellowbackFormat::zec(a.bondZat) % " (" % QString::number(a.bondLocktime) % ")");
            case Weight:      return a.weight;
            case SeatedSince: return a.seatedSince >= 0 ? QString::number(a.seatedSince) : QString("-");
            case Founding:    return a.founding ? tr("yes") : tr("no");
            case Sources:     return QString(YellowbackFormat::sourceTier(a.tier) % (a.pool ? tr(", pool") : QString()));
            case LastBundle:  return a.lastBundleHeight >= 0 ? QString::number(a.lastBundleHeight) : QString("-");
            case PoolFresh:   return a.poolFresh ? tr("fresh") : tr("no");
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
            case Seq:
                return QString(tr("Attestor %1, registered at height %2\n").arg(a.seq).arg(a.registerHeight) %
                               tr("Hot key: ") % a.attestorPubKey % "\n" %
                               tr("Bond address: ") % a.bondAddress % "\n" %
                               tr("Fee address: ") % a.bondKeyAddress);
            case Status:
                if (a.status == STATUS_PENDING)   return tr("Registered; the bond has not matured yet (since height %1).").arg(a.statusHeight);
                if (a.status == STATUS_ELIGIBLE)  return tr("May be seated and selected for bundles (since height %1).").arg(a.statusHeight);
                if (a.status == STATUS_DORMANT)   return tr("Missed too many bundles and is set aside until revived (since height %1).").arg(a.statusHeight);
                if (a.status == STATUS_EJECTED)   return tr("Ejected for equivocation at height %1; the bond is forfeit.").arg(a.statusHeight);
                if (a.status == STATUS_WITHDRAWN) return tr("The bond was withdrawn at height %1.").arg(a.statusHeight);
                return a.status;
            case Seated:
                return tr("Seated attestors are the ones a bundle may draw from at this height; a pinned one is excluded while its attestations sit too far from the pool cross-section.");
            case Bond:
                return a.bondSpentHeight >= 0
                    ? tr("Bond of %1, spendable from height %2; spent at height %3.").arg(YellowbackFormat::zec(a.bondZat)).arg(a.bondLocktime).arg(a.bondSpentHeight)
                    : tr("Bond of %1, locked until height %2.").arg(YellowbackFormat::zec(a.bondZat)).arg(a.bondLocktime);
            case Weight:
                return tr("Bond weight at height %1: the bond scaled by its age, which sets the attestor's share of selections.").arg(currentHeight);
            case Founding:
                return tr("A founding attestor's age runs from the trigger height rather than its registration.");
            case Sources:
                return tr("What the registration declares: where the attestor's prices come from, and whether it also operates a mining pool.");
            case LastBundle:
                return tr("The newest block whose price bundle carried this attestor's attestation.");
            case PoolFresh:
                return tr("Whether this node's own attestation pool holds a fresh attestation from this attestor (the subscriber keeps the pool filled).");
        }
    }

    return QVariant();
}

QVariant YellowbackAttestorsModel::headerData(int section, Qt::Orientation orientation, int role) const {
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
    using namespace YellowbackRpc::Transaction;
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
        if (t.type == TYPE_BURN || t.unbacked) { b.setColor(QColor(200, 100, 0)); return b; }
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Type: {
                QString s = YellowbackFormat::typeLabel(t.type);
                if (t.unbacked) s += tr(" (unbacked)");
                if (t.expired)  s += tr(" (expired)");
                return s;
            }
            case Amount: {
                qint64 a = t.amountCents;
                if (t.type == TYPE_SEND || t.type == TYPE_BURN)
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
                      "It has no effect; any YED it would have moved is still yours.");
        if (t.type == TYPE_BURN)
            return tr("A Yellowback output was spent as plain YEC. The Yellowback index treats the token as "
                      "destroyed (burned); only its YEC carrier value moved.");
        if (t.unbacked)
            return tr("A vault of yours was closed without burning its debt; the YED minted against it are unbacked from now on.");
        switch (index.column()) {
            case Amount:
                return tr("YED in: %1, out: %2, burned: %3; enforcement fee %4%5")
                        .arg(YellowbackFormat::cents(t.yedIn)).arg(YellowbackFormat::cents(t.yedOut))
                        .arg(YellowbackFormat::cents(t.burned)).arg(YellowbackFormat::zec(t.feeZat))
                        .arg(t.payee.isEmpty() ? QString() : tr(" to %1").arg(t.payee));
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
