// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

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

YellowbackIntent YellowbackIntent::fromJson(const json& j) {
    using namespace YellowbackRpc::PositionIntent;
    YellowbackIntent i;
    i.txid          = YellowbackJson::toStr (j, TXID);
    i.vout          = (int)YellowbackJson::toInt(j, VOUT);
    i.role          = YellowbackJson::toStr (j, ROLE);
    i.height        = (int)YellowbackJson::toInt(j, HEIGHT);
    i.releaseHeight = (int)YellowbackJson::toInt(j, RELEASE_HEIGHT);
    return i;
}

const YellowbackIntent* YellowbackPosition::claimantIntent() const {
    for (const auto& i : intents) if (i.role == YellowbackRpc::PositionIntent::ROLE_CLAIMANT) return &i;
    return nullptr;
}

const YellowbackIntent* YellowbackPosition::residualIntent() const {
    for (const auto& i : intents) if (i.role == YellowbackRpc::PositionIntent::ROLE_RESIDUAL) return &i;
    return nullptr;
}

YellowbackPosition YellowbackPosition::fromJson(const json& j) {
    using namespace YellowbackRpc::Position;
    YellowbackPosition p;
    p.txid          = YellowbackJson::toStr (j, TXID);
    p.vout          = (int)YellowbackJson::toInt(j, VOUT);
    p.status        = YellowbackJson::toStr (j, STATUS);
    p.ownerPubKey   = YellowbackJson::toStr (j, OWNER_PUBKEY);
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
    p.scriptPubKey  = YellowbackJson::toStr (j, SCRIPT_PUBKEY);
    if (j.is_object() && j.find(INTENTS) != j.end() && j[INTENTS].is_array())
        for (auto& it : j[INTENTS]) p.intents.append(YellowbackIntent::fromJson(it));
    p.canRedeem     = YellowbackJson::toBool(j, CAN_REDEEM);
    p.canClaim      = YellowbackJson::toBool(j, CAN_CLAIM);
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
    a.lastAct         = (int)YellowbackJson::toInt(j, LAST_ACT, -1);
    a.bondFrozen      = YellowbackJson::toBool(j, BOND_FROZEN);
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
    if (type == TYPE_CLAIM_RELEASE) return QObject::tr("Claim released");
    if (type == TYPE_CLAIM_CANCEL)  return QObject::tr("Claim cancelled");
    if (type == TYPE_SET_ACT) return QObject::tr("Attestor set act");
    if (type == TYPE_NOTICE)  return QObject::tr("Claim notice");
    if (type == TYPE_NOTICED) return QObject::tr("Vault noticed");
    if (type == TYPE_REGISTER)return QObject::tr("Attestor registration");
    if (type == TYPE_EQUIVOCATION) return QObject::tr("Equivocation report");
    if (type == TYPE_REVIVE)  return QObject::tr("Attestor revival");
    return type;
}

QString YellowbackFormat::rowLabel(const QString& type, const QString& path, qint64 burned) {
    using namespace YellowbackRpc::Transaction;
    if (type == TYPE_REDEEM && burned == 0 && path.isEmpty()) return QObject::tr("Claim released");
    return typeLabel(type);
}

// One sentence per haltMask name (§3.6, MINTPOL-1). The name itself stays in the text so a
// user can match it to the node's mintpol-* error identifiers.
QString YellowbackFormat::haltReason(const QString& name) {
    using namespace YellowbackRpc::Stats;
    if (name == HALT_NOT_ACTIVE)
        return QObject::tr("NOT_ACTIVE: the vault upgrade has not activated at the reference height yet.");
    if (name == HALT_NO_PRICE)
        return QObject::tr("NO_PRICE: the mint price is undefined because too few recent blocks carry a price quote.");
    if (name == HALT_GLOBAL_RATIO)
        return QObject::tr("GLOBAL_RATIO: the system-wide collateral ratio is below its floor; only a term class whose minimum ratio reaches the recapitalisation floor can mint until it recovers.");
    if (name == HALT_DIVERGENCE)
        return QObject::tr("DIVERGENCE: the fast and slow price medians disagree by more than the allowed band.");
    return name;
}

QString YellowbackFormat::voidReason(const QString& verdict) {
    if (verdict.isEmpty()) return QString();
    // hardening H-1: the one VOID reason a wallet following the Mint page should never see
    if (verdict.startsWith("mint-halted-unarmed"))
        return QObject::tr("The mint was confirmed while the attestation layer was not ARMED, on a network that requires it for every mint "
                           "(mint-halted-unarmed), so it created no YED. The collateral stays yours: "
                           "release it at the lock height (Release, no burn, no fee).");
    return QObject::tr("The mint failed the rule \"%1\" and created no YED (it was recorded before the vault upgrade; since then such a mint is an invalid transaction). "
                       "The collateral stays yours: release it at the lock height (Release, no burn, no fee).").arg(verdict);
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
    headers << tr("Status") << tr("Act by") << tr("Minted") << tr("Collateral") << tr("Ratio (market)") << tr("Class")
            << tr("Lock height (est. date)") << tr("Claim height (est. date)")
            << tr("Claimable") << tr("Underwater below") << tr("Unbacked") << tr("Claim in progress") << tr("Notice") << tr("Vault");
    modeldata = new QList<YellowbackPosition>();
}

QString YellowbackFormat::duration(qint64 seconds) {
    if (seconds < 0) seconds = 0;
    const qint64 d = seconds / 86400, h = (seconds % 86400) / 3600, m = (seconds % 3600) / 60;
    if (d > 0) return QObject::tr("%1 d %2 h").arg(d).arg(h);
    if (h > 0) return QObject::tr("%1 h %2 m").arg(h).arg(m);
    return QObject::tr("%1 m").arg(m);
}

QString YellowbackFormat::blocksAndDuration(int blocks) {
    return QObject::tr("%1 block(s), ~%2").arg(blocks).arg(duration((qint64)blocks * YellowbackRpc::SECONDS_PER_BLOCK));
}

QString YellowbackPositionsModel::actBy(const YellowbackPosition& p, int currentHeight) {
    using namespace YellowbackRpc::Position;
    if (currentHeight <= 0) return QString("-");
    if (p.status == STATUS_VOID) {
        if (currentHeight < p.lockHeight) return QObject::tr("locked; release in %1").arg(YellowbackFormat::blocksAndDuration(p.lockHeight - currentHeight));
        if (currentHeight < p.claimHeight) return QObject::tr("RELEASE NOW: open to anyone in %1").arg(YellowbackFormat::blocksAndDuration(p.claimHeight - currentHeight));
        return QObject::tr("open to anyone: release at once");
    }
    if (p.status == STATUS_CLAIMING) {
        const YellowbackIntent* c = p.claimantIntent();
        if (c == nullptr) return QObject::tr("being claimed");
        return currentHeight < c->releaseHeight
            ? QObject::tr("BEING CLAIMED: the claim releases in %1 unless an attestor cancels it").arg(YellowbackFormat::blocksAndDuration(c->releaseHeight - currentHeight))
            : QObject::tr("claimed: the claimant may release the collateral now");
    }
    if (p.status != STATUS_ACTIVE) return QString("-");
    if (currentHeight < p.lockHeight) return QObject::tr("locked; redeemable in %1").arg(YellowbackFormat::blocksAndDuration(p.lockHeight - currentHeight));
    if (currentHeight < p.claimHeight) return QObject::tr("REDEEMABLE: claim path opens in %1").arg(YellowbackFormat::blocksAndDuration(p.claimHeight - currentHeight));
    return p.claimable ? QObject::tr("CLAIM PATH OPEN and underwater: redeem before someone claims it")
                       : QObject::tr("claim path open; safe while the claim price stays above %1").arg(p.underwaterAt >= 0 ? YellowbackFormat::price(p.underwaterAt) : QObject::tr("(undefined)"));
}

bool YellowbackPositionsFilter::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    using namespace YellowbackRpc::Position;
    auto* src = dynamic_cast<YellowbackPositionsModel*>(sourceModel());   // the model has no Q_OBJECT macro
    if (src == nullptr) return true;
    const YellowbackPosition* p = src->positionAt(sourceRow);
    if (p == nullptr) return true;                                  // the "Loading..." row
    if (statusFilter.isEmpty()) return true;
    if (statusFilter == "open") return p->status == STATUS_ACTIVE || p->status == STATUS_VOID || p->status == STATUS_CLAIMING;
    return p->status == statusFilter;
    Q_UNUSED(sourceParent);
}

qint64 YellowbackPositionsModel::ratioBps(const YellowbackPosition& p, qint64 pClaimMicroUsd) {
    // collateralZat * pClaim / (COIN * mintedCents), as the node's globalRatioBps (math.h): the
    // product overflows 64 bits for a large vault, so it is done in floating point for display.
    if (p.mintedCents <= 0 || pClaimMicroUsd <= 0 || p.collateralZat <= 0) return -1;
    const long double r = (long double)p.collateralZat * (long double)pClaimMicroUsd / (100000000.0L * (long double)p.mintedCents);
    return (qint64)r;
}

void YellowbackPositionsModel::setPrices(qint64 pFastMicroUsd, qint64 pClaimMicroUsd) {
    if (pFast == pFastMicroUsd && pClaim == pClaimMicroUsd) return;
    pFast = pFastMicroUsd; pClaim = pClaimMicroUsd;
    if (!modeldata->isEmpty()) emit dataChanged(index(0, Ratio), index(modeldata->size() - 1, Ratio));
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
        (index.column() == Minted || index.column() == Collateral || index.column() == Ratio || index.column() == UnderwaterBelow))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);

    if (role == Qt::BackgroundRole) {
        // The rows that need the owner's attention, loud enough to find in a long list: a vault
        // under a claim notice, or past its claim height while active, is exposed (red); one in
        // its grace window is the owner's to redeem now (orange). Closed rows stay plain.
        if (p.status == STATUS_ACTIVE) {
            if (p.noticed || (currentHeight > 0 && currentHeight >= p.claimHeight)) return QBrush(QColor(255, 210, 210));
            if (currentHeight > 0 && currentHeight >= p.lockHeight)              return QBrush(QColor(255, 232, 190));
        }
        if (p.status == STATUS_VOID && currentHeight > 0 && currentHeight >= p.lockHeight) return QBrush(QColor(255, 232, 190));
        if (p.status == STATUS_CLAIMING) return QBrush(QColor(255, 210, 210));
        return QVariant();
    }

    if (role == Qt::ForegroundRole) {
        QBrush b;
        if (index.column() == Ratio && p.status == STATUS_ACTIVE) {
            // the claim threshold is 110 %; below 150 % a further fall of a quarter reaches it
            const qint64 r = ratioBps(p, pFast > 0 ? pFast : pClaim);
            if (r >= 0 && r < 11000)      { b.setColor(Qt::red); return b; }
            if (r >= 0 && r < 15000)      { b.setColor(QColor(200, 100, 0)); return b; }
        }
        if (index.column() == ActBy && p.status == STATUS_ACTIVE && currentHeight > 0 && currentHeight >= p.lockHeight) { b.setColor(Qt::red); return b; }
        if (p.status == STATUS_VOID || p.status == STATUS_CLAIMING) b.setColor(Qt::red);
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
            case ActBy:        return actBy(p, currentHeight);
            case Ratio: {
                if (p.status != STATUS_ACTIVE) return QString("-");
                const qint64 r = ratioBps(p, pFast > 0 ? pFast : pClaim);
                return r < 0 ? tr("(no price)") : YellowbackFormat::bpsAsPercent(r);
            }
            case UnderwaterBelow: return p.status == STATUS_ACTIVE && p.underwaterAt >= 0 ? YellowbackFormat::price(p.underwaterAt) : QString("-");
            case TermClass:    return p.termClass;
            case LockHeight:   return YellowbackFormat::heightWithEstimate(p.lockHeight, currentHeight);
            case ClaimHeight:  return YellowbackFormat::heightWithEstimate(p.claimHeight, currentHeight);
            case Claimable: {
                if (p.status != STATUS_ACTIVE) return QString("-");
                if (p.claimable) return tr("YES");
                if (currentHeight > 0 && currentHeight < p.claimHeight) return tr("not before %1").arg(p.claimHeight);
                if (p.noticed && p.emergencyOpenAt >= 0 && currentHeight > 0 && currentHeight < p.emergencyOpenAt) return tr("notice: opens at %1").arg(p.emergencyOpenAt);
                return p.underwaterAt >= 0 ? tr("no (claim price above %1)").arg(YellowbackFormat::price(p.underwaterAt)) : tr("no");
            }
            case Unbacked:     return p.unbacked ? tr("yes") : tr("no");
            case ClaimState: {
                const YellowbackIntent* c = p.claimantIntent();
                if (p.status != STATUS_CLAIMING || c == nullptr) return QString("-");
                return tr("claimed at %1; releases at %2").arg(c->height).arg(c->releaseHeight);
            }
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
            case ActBy:
                return tr("What you must do with this vault and by when, from its heights alone. Before the lock height nothing can move it, in either direction. "
                          "From the lock height you can redeem it; from the claim height anyone can claim it if it is underwater. Durations assume 75 s per block.");
            case Ratio: {
                const qint64 atClaim = ratioBps(p, pClaim);
                return pFast > 0 || pClaim > 0
                    ? tr("Collateral at the latest price (%1 per YEC) over the %2 of YED this vault backs: the market view, which moves first. "
                         "A claim is judged at the claim price (%3 per YEC), a slower, higher figure by design, at which this vault's ratio is %4. "
                         "Past the claim height the vault can be claimed once its ratio at the claim price is under 110 %%, i.e. once the claim price drops below %5.")
                          .arg(YellowbackFormat::price(pFast > 0 ? pFast : pClaim)).arg(YellowbackFormat::cents(p.mintedCents))
                          .arg(pClaim > 0 ? YellowbackFormat::price(pClaim) : tr("undefined"))
                          .arg(atClaim >= 0 ? YellowbackFormat::bpsAsPercent(atClaim) : tr("undefined"))
                          .arg(p.underwaterAt >= 0 ? YellowbackFormat::price(p.underwaterAt) : tr("(undefined)"))
                    : tr("No price is defined at the tip, so the ratio cannot be judged.");
            }
            case UnderwaterBelow:
                return tr("The claim price below which this vault is underwater (collateral worth less than 110 %% of its debt) and, past its claim height, claimable by anyone who burns the debt.");
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
                if (p.status == STATUS_CLAIMING)
                    return tr("Being claimed: someone burned %1 of YED and moved the collateral into a claim intent. "
                              "It is released to them after the claim delay unless a member of the attestor set cancels it as a wrong-price claim, "
                              "which returns the collateral to this vault (ACTIVE again).").arg(YellowbackFormat::cents(p.mintedCents));
                return tr("Active: %1 of YED are backed by this vault. Releasing the collateral burns exactly that debt and pays an enforcement fee to a quoting pool.")
                          .arg(YellowbackFormat::cents(p.mintedCents));
            case Claimable:
                return p.underwaterAt >= 0
                    ? tr("Past the claim height, anyone may burn the vault's debt and take its collateral once the claim price falls below %1 per YEC.")
                          .arg(YellowbackFormat::price(p.underwaterAt))
                    : tr("A VOID vault (recorded before the vault upgrade) carries no debt and can never be claimed for one. Release its collateral.");
            case Unbacked:
                return tr("\"yes\" means this vault was closed without burning its debt; the YED minted against it are no longer backed. No rule can produce such a close since the vault upgrade.");
            case ClaimState:
                return tr("A claim moves the collateral into a claim intent. After the claim delay the claimant releases it and the vault is CLAIMED; "
                          "before then any member of the YED attestor set may cancel a claim made at a wrong price, and the vault is ACTIVE again.");
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
                return tr("Lock height plus the grace period. After it, the vault's claim path opens: anyone who burns the debt can claim the collateral if the vault is underwater; "
                          "the claim is released to them after the claim delay unless an attestor cancels it.");
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
                return tr("The enforcement fee, paid from your own YEC (not from the collateral) to a pool that published a price quote recently.");
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
            << tr("Seated since") << tr("Founding") << tr("Sources") << tr("Last bundle") << tr("In pool")
            << tr("Last act") << tr("Bond frozen");
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
        if (a.bondFrozen && index.column() == BondFrozen)                { b.setColor(Qt::red); return b; }
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
            case LastAct:     return a.lastAct >= 0 ? QString::number(a.lastAct) : QString("-");
            case BondFrozen:  return a.bondFrozen ? tr("FROZEN") : tr("no");
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
                if (a.status == STATUS_PENDING)   return tr("Joined the attestor set; the bond has not matured yet (since height %1).").arg(a.statusHeight);
                if (a.status == STATUS_ELIGIBLE)  return tr("A current member of the attestor set: may be seated, selected for bundles and cancel wrong-price claims (since height %1).").arg(a.statusHeight);
                if (a.status == STATUS_DORMANT)   return tr("No heartbeat within the set's liveness window (or too many missed bundles): set aside until its next heartbeat (since height %1).").arg(a.statusHeight);
                if (a.status == STATUS_EJECTED)   return tr("Ejected for equivocation at height %1; the bond is frozen and forfeit.").arg(a.statusHeight);
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
            case LastAct:
                return tr("The attestor set's last act for this member: its join plus maturity, or its last heartbeat. Signing prices is not an act; "
                          "a member with no heartbeat within the set's liveness window is dormant.");
            case BondFrozen:
                return a.bondFrozen ? tr("The set froze this bond (a removal with burn, or an equivocation): it can never be withdrawn, and the key never seats again.")
                                    : tr("The bond is not frozen.");
        }
    }

    return QVariant();
}

QVariant YellowbackAttestorsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers.size())
        return headers.at(section);
    return QVariant();
}

// ── Pending claims model (rpcversion 5) ───────────────────────────────────────────────────

YellowbackPendingClaimsModel::YellowbackPendingClaimsModel(QObject* parent) : QAbstractTableModel(parent) {
    headers << tr("Vault") << tr("Intent") << tr("Whose") << tr("Debt burned") << tr("Collateral")
            << tr("Claimed at") << tr("Releases at") << tr("Release");
    modeldata = new QList<YellowbackPendingClaim>();
}

YellowbackPendingClaimsModel::~YellowbackPendingClaimsModel() {
    delete modeldata;
}

void YellowbackPendingClaimsModel::setNewData(const QList<YellowbackPendingClaim>& rows, int height) {
    loading = false;
    beginResetModel();
    *modeldata = rows;
    currentHeight = height;
    endResetModel();
}

const YellowbackPendingClaim* YellowbackPendingClaimsModel::rowAt(int row) const {
    if (row < 0 || row >= modeldata->size()) return nullptr;
    return &modeldata->at(row);
}

QString YellowbackPendingClaimsModel::remaining(const YellowbackIntent& i, int height) {
    // vault_release confirms at releaseHeight at the earliest: the next block is height + 1
    if (height + 1 >= i.releaseHeight) return tr("releasable now");
    return tr("in %1").arg(YellowbackFormat::blocksAndDuration(i.releaseHeight - height - 1));
}

int YellowbackPendingClaimsModel::rowCount(const QModelIndex&) const {
    if (loading) return 1;
    return modeldata->size();
}

int YellowbackPendingClaimsModel::columnCount(const QModelIndex&) const {
    return headers.size();
}

QVariant YellowbackPendingClaimsModel::data(const QModelIndex& index, int role) const {
    using namespace YellowbackRpc::PositionIntent;
    if (loading) {
        if (role == Qt::DisplayRole && index.column() == 0) return tr("Loading...");
        return QVariant();
    }
    if (index.row() >= modeldata->size()) return QVariant();
    const auto& c = modeldata->at(index.row());
    if (role == Qt::TextAlignmentRole && (index.column() == Debt || index.column() == Collateral))
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Vault:         return c.vault.vaultName();
            case Role:          return c.intent.role == ROLE_RESIDUAL ? tr("owner's residual") : tr("claimant");
            case Whose:         return c.mineClaim ? tr("your claim") : c.mineVault ? tr("your vault") : tr("someone else's");
            case Debt:          return YellowbackFormat::cents(c.vault.mintedCents);
            case Collateral:    return YellowbackFormat::zec(c.vault.collateralZat);
            case ClaimHeight:   return QString::number(c.intent.height);
            case ReleaseHeight: return YellowbackFormat::heightWithEstimate(c.intent.releaseHeight, currentHeight);
            case Remaining:     return remaining(c.intent, currentHeight);
        }
    }
    if (role == Qt::ToolTipRole) {
        if (c.intent.role == ROLE_RESIDUAL)
            return tr("The owner's residual (RED-5): the collateral above what the claimant receives. Anyone may release it to the owner after the claim delay; it cannot be cancelled.");
        return tr("Intent %1: released to the claimant from height %2 on. Until then any member of the YED attestor set may cancel it as a wrong-price claim; "
                  "the collateral then returns to the vault and the claim's burn is not refunded.").arg(c.intent.outpoint()).arg(c.intent.releaseHeight);
    }
    return QVariant();
}

QVariant YellowbackPendingClaimsModel::headerData(int section, Qt::Orientation orientation, int role) const {
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
                QString s = YellowbackFormat::rowLabel(t.type, t.path, t.burned);
                // A send whose every output came back to this wallet: the balance did not move, and
                // "Sent $0.00" read as a fault on the owner's first walk-through
                if (t.type == TYPE_SEND && t.amountCents == 0 && t.yedOut > 0) s = tr("self-transfer");
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
        if (YellowbackFormat::rowLabel(t.type, t.path, t.burned) != YellowbackFormat::typeLabel(t.type))
            return tr("A claim intent of a vault of yours was released after the claim delay: the vault is CLAIMED.");
        if (t.type == TYPE_BURN)
            return tr("A Yellowback output was spent as plain YEC. The Yellowback index treats the token as "
                      "destroyed (burned); only its YEC carrier value moved.");
        if (t.unbacked)
            return tr("A vault of yours was closed without burning its debt; the YED minted against it are unbacked from now on.");
        if (t.type == TYPE_SEND && t.amountCents == 0 && t.yedOut > 0)
            return tr("YED moved between addresses of this wallet (%1 in, %2 out): the balance is unchanged, which is why the amount is $0.00.")
                    .arg(YellowbackFormat::cents(t.yedIn)).arg(YellowbackFormat::cents(t.yedOut));
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
