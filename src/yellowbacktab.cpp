// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

#include "yellowbacktab.h"
#include "yellowbackcontroller.h"
#include "yellowbackmodels.h"
#include "yellowbackrpc.h"
#include "vaultrpc.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "addressbook.h"
#include "settings.h"
#include "connection.h"
#include "nodedatacheck.h"

#include "ui_yellowbacktab.h"
#include "ui_yellowbackoverview.h"
#include "ui_yellowbackreceive.h"
#include "ui_yellowbacksend.h"
#include "ui_yellowbackmint.h"

#include <QSignalBlocker>
#include "ui_yellowbackpositions.h"
#include "ui_yellowbackclaim.h"
#include "ui_yellowbacktransactions.h"
#include "ui_yellowbackredeem.h"
#include "ui_yellowbacksettings.h"
#include "ui_yellowbackattestors.h"

using json = nlohmann::json;

YellowbackTab::YellowbackTab(MainWindow* main, QWidget* parent) : QWidget(parent) {
    this->main = main;
    ui = new Ui::YellowbackTab();
    ui->setupUi(this);

    confirmFn = [this](const QString& title, const QString& text) {
        return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
    };
    noticeFn = [this](const QString& title, const QString& text, bool isError) {
        if (isError) QMessageBox::critical(this, title, text);
        else         QMessageBox::information(this, title, text);
    };
    inputFn = [this](const QString& title, const QString& label, QString* value) {
        bool ok = false;
        QString v = QInputDialog::getText(this, title, label, QLineEdit::Normal, value ? *value : QString(), &ok);
        if (ok && value) *value = v;
        return ok;
    };

    setupPages();

    ui->lblBanner->setText(tr("Yellowback: waiting for the node..."));
    ui->lblBanner->setVisible(true);
    ui->lblStatus->setVisible(false);
    ui->lblNotes->setVisible(false);
    ui->wBackupNag->setVisible(false);
    setActionsEnabled(false);
    updateBackupNag();
}

YellowbackTab::~YellowbackTab() {
    // A subscriber this tab launched dies with the wallet (the node it feeds does too)
    if (subscriber != nullptr && subscriber->parent() == this && subscriber->state() != QProcess::NotRunning) {
        subscriber->terminate();
        if (!subscriber->waitForFinished(3000)) subscriber->kill();
    }
    delete uiOverview;
    delete uiReceive;
    delete uiSend;
    delete uiMint;
    delete uiPositions;
    delete uiClaim;
    delete uiTx;
    delete uiRedeem;
    delete uiSettings;
    delete uiAttestors;
    delete ui;
}

// ── Construction ──────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupPages() {
    for (int i = 0; i < PageCount; i++)
        pages[i] = new QWidget(this);

    uiOverview  = new Ui::YellowbackOverview();     uiOverview->setupUi(pages[Overview]);
    uiReceive   = new Ui::YellowbackReceive();      uiReceive->setupUi(pages[Receive]);
    // The address in the platform's fixed-width font: a stylesheet naming "monospace" made Qt
    // search for a family macOS does not have ("Populating font family aliases took 34 ms")
    uiReceive->lblAddress->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    uiSend      = new Ui::YellowbackSend();         uiSend->setupUi(pages[Send]);
    uiMint      = new Ui::YellowbackMint();         uiMint->setupUi(pages[Mint]);
    // Long, word-wrapped values ("pools $x, attestors $y — …", "n of m attestors reachable",
    // "ARMED — mints and claims use attested prices") were clipped to one line beside their
    // labels on the owner's first walk-through: let such a field take the full width under its
    // label, and let the label grow with its text.
    for (QFormLayout* form : { uiOverview->balanceForm, uiOverview->systemForm, uiMint->mintForm })
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (QLabel* grows : { uiOverview->lblAttestation, uiOverview->lblMintStatus, uiMint->lblSource, uiMint->lblSelection }) {
        grows->setWordWrap(true);
        grows->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        // A word-wrapped label's minimum height is near zero, so when a page is short of room it is
        // the one widget the layout collapses -- to 8 px on the owner's screen. Two lines is the floor.
        grows->setMinimumHeight(grows->fontMetrics().lineSpacing() * 2 + 4);
    }
    uiPositions = new Ui::YellowbackPositions();    uiPositions->setupUi(pages[Vaults]);
    uiClaim     = new Ui::YellowbackClaim();        uiClaim->setupUi(pages[Claim]);
    uiTx        = new Ui::YellowbackTransactions(); uiTx->setupUi(pages[Transactions]);
    uiRedeem    = new Ui::YellowbackRedeem();       uiRedeem->setupUi(pages[Redeem]);
    uiSettings  = new Ui::YellowbackSettings();     uiSettings->setupUi(pages[Settings]);
    uiAttestors = new Ui::YellowbackAttestors();    uiAttestors->setupUi(pages[Attestors]);

    ui->subTabs->addTab(pages[Overview],     tr("Overview"));
    ui->subTabs->addTab(pages[Receive],      tr("Receive"));
    ui->subTabs->addTab(pages[Send],         tr("Send"));
    ui->subTabs->addTab(pages[Mint],         tr("Mint"));
    ui->subTabs->addTab(pages[Vaults],       tr("Vaults"));
    ui->subTabs->addTab(pages[Claim],        tr("Claim"));
    ui->subTabs->addTab(pages[PendingClaims], tr("Pending claims"));
    ui->subTabs->addTab(pages[Transactions], tr("Transactions"));
    ui->subTabs->addTab(pages[Redeem],       tr("Redeem"));
    ui->subTabs->addTab(pages[Attestors],    tr("Attestors"));
    ui->subTabs->addTab(pages[Settings],     tr("Settings"));

    setupOverview();
    setupReceive();
    setupSend();
    setupMint();
    setupPositions();
    setupClaim();
    setupTransactions();
    setupRedeem();
    setupAttestors();
    setupPendingClaims();
    setupSettings();

    // Backup nag
    QObject::connect(ui->btnBackupNow, &QPushButton::clicked, [=, this]() {
        if (main != nullptr && main->ui != nullptr)
            main->ui->actionBackup_wallet_dat->trigger();
    });
    QObject::connect(ui->btnBackupDone, &QPushButton::clicked, [=, this]() {
        if (QMessageBox::question(this, tr("Confirm backup"),
                tr("Do you have a copy of wallet.dat made after your most recent mint, stored somewhere other than this computer?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            Settings::getInstance()->setYellowbackBackupPending(false);
            updateBackupNag();
        }
    });
}

void YellowbackTab::setController(YellowbackController* controller) {
    ctl = controller;
    if (ctl == nullptr) return;

    uiOverview->tblRecent->setModel(ctl->transactionsModel());
    uiTx->tblTransactions->setModel(ctl->transactionsModel());
    positionsFilter = new YellowbackPositionsFilter(this);
    positionsFilter->setSourceModel(ctl->positionsModel());
    uiPositions->tblPositions->setModel(positionsFilter);
    uiClaim->tblClaimable->setModel(ctl->claimableModel());
    uiAttestors->tblAttestors->setModel(ctl->attestorsModel());
    tblPendingClaims->setModel(ctl->pendingClaimsModel());

    QObject::connect(ctl, &YellowbackController::availabilityChanged, this, [=, this](bool, const QString&) {
        updateBanner();
        updateMintGate();
    });
    QObject::connect(ctl, &YellowbackController::infoUpdated,         this, [=, this]() { updateBanner(); updateOverview(); updateMintClasses(); updateVaultButtons(); updateAttestors(); });
    QObject::connect(ctl, &YellowbackController::priceUpdated,        this, [=, this]() { updateOverview(); });
    QObject::connect(ctl, &YellowbackController::attestorsUpdated,    this, [=, this]() { updateAttestors(); });
    QObject::connect(ctl, &YellowbackController::selectionUpdated,    this, [=, this]() { updateMintAttest(); });
    QObject::connect(ctl, &YellowbackController::statsUpdated,        this, [=, this]() { updateBanner(); updateOverview(); updateMintGate(); });
    QObject::connect(ctl, &YellowbackController::balanceUpdated,      this, [=, this]() { updateBalances(); });
    QObject::connect(ctl, &YellowbackController::positionsUpdated,    this, [=, this]() { updatePositions(); updateRedeemPage(); updateBanner(); continueRenew(); });
    QObject::connect(ctl, &YellowbackController::claimableUpdated,    this, [=, this]() { updateClaimPage(); });
    QObject::connect(ctl, &YellowbackController::pendingClaimsUpdated, this, [=, this]() { updatePendingClaims(); });
    QObject::connect(ctl, &YellowbackController::attestorSetUpdated,  this, [=, this]() { updateMembership(); updateAttestorButtons(); updatePendingClaims(); });
    QObject::connect(ctl, &YellowbackController::claimOutcomesUpdated, this, [=, this]() { updatePendingClaims(); });
    QObject::connect(ctl, &YellowbackController::transactionsUpdated, this, [=, this]() {
        uiOverview->tblRecent->resizeColumnsToContents();
        uiTx->tblTransactions->resizeColumnsToContents();
    });

    updateBanner();
    updateOverview();
    updateBalances();
    updateMintClasses();
    updateMintGate();
    updatePositions();
    updateClaimPage();
    updateRedeemPage();
    updateAttestors();
    updateMembership();
    updatePendingClaims();
    updateMintAttest();
    updateSettingsPage();
}

// ── Banner / enabling ─────────────────────────────────────────────────────────────────────

void YellowbackTab::updateBanner() {
    if (ctl == nullptr) {
        ui->lblBanner->setText(tr("Yellowback: not connected."));
        ui->lblBanner->setVisible(true);
        ui->lblStatus->setVisible(false);
        ui->lblNotes->setVisible(false);
        setActionsEnabled(false);
        return;
    }
    YellowbackStatus st = ctl->status();
    if (!st.available) {
        ui->lblBanner->setText(st.headline);
        ui->lblBanner->setVisible(true);
        ui->lblStatus->setVisible(false);
        ui->lblNotes->setVisible(false);
    } else {
        // H-9.2: a vault within a day of its claim height is warned about here, above every page,
        // until it is redeemed, renewed or released
        st.warnings << ctl->deadlineWarnings();
        ui->lblBanner->setText(st.warnings.join("\n"));
        ui->lblBanner->setVisible(!st.warnings.isEmpty());
        ui->lblStatus->setText(st.headline);
        ui->lblStatus->setVisible(true);
        ui->lblNotes->setText(st.notes.join("\n"));
        ui->lblNotes->setVisible(!st.notes.isEmpty());
    }
    setActionsEnabled(st.available);
}

void YellowbackTab::setActionsEnabled(bool enabled) {
    actionsEnabled = enabled;
    uiReceive->btnNewAddress->setEnabled(enabled);
    uiSend->btnSend->setEnabled(enabled);
    uiPositions->btnWhyVoid->setEnabled(enabled);
    if (!enabled) {
        uiMint->btnMint->setEnabled(false);
        uiPositions->btnRelease->setEnabled(false);
        uiPositions->btnRedeem->setEnabled(false);
        uiPositions->btnRenew->setEnabled(false);
        uiPositions->btnNotice->setEnabled(false);
        uiClaim->btnClaim->setEnabled(false);
        uiRedeem->btnStart->setEnabled(false);
        uiAttestors->btnRegister->setEnabled(false);
        uiAttestors->btnReport->setEnabled(false);
        uiAttestors->btnHeartbeat->setEnabled(false);
        uiAttestors->btnWithdraw->setEnabled(false);
        uiSettings->btnSweepCarriers->setEnabled(false);
        if (btnReleaseClaim) btnReleaseClaim->setEnabled(false);
        if (btnCancelClaim)  btnCancelClaim->setEnabled(false);
    } else if (ctl != nullptr) {
        updateMintGate();
        updateVaultButtons();
        updateClaimPage();
        updateRedeemPage();
        updateAttestorButtons();
        updatePendingClaims();
        uiSettings->btnSweepCarriers->setEnabled(true);
    }
}

bool YellowbackTab::confirm(const QString& title, const QString& text) {
    return confirmFn ? confirmFn(title, text) : false;
}

void YellowbackTab::notice(const QString& title, const QString& text, bool isError) {
    if (noticeFn) noticeFn(title, text, isError);
}

// Node error strings are stable identifiers and are always shown verbatim (§4.5); the wallet
// only appends what the identifier means.
void YellowbackTab::failed(const QString& what, const QString& e) {
    QString msg = tr("%1 failed: %2").arg(what).arg(e);
    QString why = YellowbackController::explainError(e);
    if (!why.isEmpty()) msg += "\n\n" % why;
    notice(tr("%1 failed").arg(what), msg, true);
}

// ── v3 two-step (W7) ──────────────────────────────────────────────────────────────────────
// A pending reply names the carrier only. The status label says so ("preparing price proof
// (1 block)") and the controller follows the main transaction; `done` gets its yed_gettxinfo.
// A reply that is not pending (an older node, or a node that ignored wait) is handed to `done`
// as it is, so both shapes end in the same summary.
void YellowbackTab::followPending(const QString& type, const json& r, QLabel* status, std::function<void(const json&)> done,
                                  const QString& vaultTxid) {
    using namespace YellowbackRpc;
    if (!YellowbackJson::toBool(r, MintResult::PENDING)) { done(r); return; }
    const QString carrier = YellowbackJson::toStr(r, MintResult::CARRIER_TXID);
    const int refHeight   = (int)YellowbackJson::toInt(r, MintResult::REF_HEIGHT, ctl->refHeightNow());
    if (status != nullptr)
        status->setText(tr("Preparing price proof (1 block): carrier %1 sent; the %2 transaction follows when it confirms.").arg(carrier).arg(type));
    ctl->awaitPending(type, carrier, refHeight,
        [=, this](const json& info) { done(info); },
        [=, this](const QString& e) {
            if (status != nullptr) status->setText(tr("%1 did not complete: %2").arg(type).arg(e));
            notice(tr("%1 not completed").arg(type), e, true);
        }, vaultTxid);
}

// bundle-insufficient: name the missing seqs and offer to re-query yed_getselection (which
// redraws the "n of m reachable" line) so the user can press the button again when the
// subscriber has filled the pool. Returns false for any other error.
bool YellowbackTab::bundleInsufficientRetry(const QString& what, const QString& e) {
    int count = 0, selected = 0;
    QList<int> missing;
    if (!YellowbackController::parseBundleInsufficient(e, &count, &selected, &missing)) return false;
    QStringList seqs;
    for (int m : missing) seqs << QString::number(m);
    QString text = tr("%1 was not sent: only %2 of the %3 selected attestors have a fresh attestation in this node's pool%4.\n\n"
                      "The subscriber (Settings page) fills the pool as attestors publish; nothing was signed or broadcast. "
                      "Re-check which attestors are reachable now?")
                       .arg(what).arg(count).arg(selected)
                       .arg(seqs.isEmpty() ? QString() : tr(" (missing attestor seq %1)").arg(seqs.join(", ")));
    if (confirm(tr("Price proof incomplete"), text)) {
        ctl->refreshSelection();
        ctl->refresh(true);
    }
    return true;
}

void YellowbackTab::updateBackupNag() {
    ui->wBackupNag->setVisible(Settings::getInstance()->getYellowbackBackupPending());
}

// ── Overview ──────────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupOverview() {
    uiOverview->tblRecent->horizontalHeader()->setStretchLastSection(true);
    uiOverview->tblRecent->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(uiOverview->tblRecent, &QTableView::customContextMenuRequested, [=, this](const QPoint& pos) {
        showTxContextMenu(uiOverview->tblRecent, pos);
    });
}

void YellowbackTab::updateOverview() {
    if (ctl == nullptr) return;
    using namespace YellowbackRpc;
    const json& s    = ctl->stats();
    const json& info = ctl->info();
    const json& up   = ctl->upgrade();

    uiOverview->lblNetwork->setText(ctl->network().isEmpty() ? "-" : ctl->network());
    uiOverview->lblIndexHeight->setText(QString::number(ctl->height()) %
        (ctl->isSynced() ? tr(" (at the chain tip)") : tr(" (chain at %1)").arg(ctl->chainHeight())));
    uiOverview->lblYecBalance->setText(Settings::getZECUSDDisplayFormat(ctl->yecBalance()));

    // rpcversion 5: the vault upgrade (yed_getinfo.upgrade, vault_getinfo) and what it means for the rules
    {
        const QString status = YellowbackJson::toStr(up, Upgrade::STATUS, "-");
        const qint64  at     = YellowbackJson::toInt(up, Upgrade::ACTIVATION_HEIGHT, -1);
        const QString branch = YellowbackJson::toStr(up, Upgrade::BRANCH_ID, "-");
        QString line = status == Upgrade::STATUS_ACTIVE  ? tr("active since %1 (branch %2)").arg(at).arg(branch)
                     : status == Upgrade::STATUS_PENDING ? tr("activates at %1 (branch %2)").arg(at).arg(branch)
                     : status;
        const json& vi = ctl->vaultInfo();
        if (!vi.empty() && YellowbackJson::toStr(vi, VaultRpc::Info::BRANCH_ID) != branch && !YellowbackJson::toStr(vi, VaultRpc::Info::BRANCH_ID).isEmpty())
            line += tr("; vault_getinfo reports branch %1").arg(YellowbackJson::toStr(vi, VaultRpc::Info::BRANCH_ID));
        const QString set = ctl->attestorSetId();
        if (!set.isEmpty()) line += tr("; attestor set %1…, claim delay %2 blocks").arg(set.left(12)).arg(ctl->claimDelay());
        uiOverview->lblActivation->setText(line);
        uiOverview->lblActivation->setToolTip(tr("Yellowback's rules are part of the Ycash vault upgrade (UPGRADE_VAULT, branch ID 6d5b7a31): from its activation height they are consensus rules."));
        uiOverview->lblEnforcement->setText(info.is_object() && !info.empty()
            ? (status == Upgrade::STATUS_ACTIVE ? tr("consensus: every full node checks them") : tr("consensus from the upgrade height"))
            : QString("-"));
    }

    // Prices: P_mid headline, P_mint / P_claim, σ (§4.8 Overview row); null renders "undefined"
    uiOverview->lblYecPrice->setText(YellowbackJson::isNull(s, Stats::P_MID)
        ? tr("undefined (too few quoted blocks)")
        : YellowbackFormat::price(YellowbackJson::toInt(s, Stats::P_MID)) % tr(" per YEC"));
    uiOverview->lblMintClaimPrice->setText(YellowbackFormat::priceOrUndefined(s, Stats::P_MINT) % " / " %
                                           YellowbackFormat::priceOrUndefined(s, Stats::P_CLAIM));
    uiOverview->lblSigma->setText(s.empty() ? "-" : YellowbackFormat::bpsAsMultiplier(YellowbackJson::toInt(s, Stats::SIGMA_MULT_BPS, 10000)));

    uiOverview->lblGlobalRatio->setText(YellowbackJson::isNull(s, Stats::GLOBAL_RATIO_BPS)
        ? tr("undefined (no supply or no mint price)")
        : YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(s, Stats::GLOBAL_RATIO_BPS)));

    if (YellowbackJson::isNull(s, Stats::SUPPLY_CAP_CENTS)) {
        uiOverview->lblCapHeadroom->setText(s.empty() ? "-" : tr("no cap"));
    } else {
        qint64 cap = YellowbackJson::toInt(s, Stats::SUPPLY_CAP_CENTS), supply = YellowbackJson::toInt(s, Stats::SUPPLY_CENTS);
        if (ctl->softSupplyCap() && (ctl->supplyCapReached() || supply >= cap)) {
            // W20: at or above the cap there is no headroom to show; say what is in circulation
            // against the cap and which classes can still mint above it
            const QStringList mintable = ctl->mintableClasses();
            uiOverview->lblCapHeadroom->setText(mintable.isEmpty()
                ? tr("%1 of %2 cap — reached").arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(cap))
                : tr("%1 of %2 cap — class %3 only").arg(YellowbackFormat::cents(supply)).arg(YellowbackFormat::cents(cap)).arg(mintable.join(tr(" or "))));
        } else {
            uiOverview->lblCapHeadroom->setText(tr("%1 of %2 cap").arg(YellowbackFormat::cents(std::max<qint64>(0, cap - supply))).arg(YellowbackFormat::cents(cap)));
        }
    }

    QString blocker = ctl->mintBlocker(0);
    {
        // A short status in the (narrow) field, the whole explanation as the tooltip: the long text
        // was unreadable in that column on the owner's walk-through.
        const QString limit = ctl->mintLimit();
        const QStringList halts = YellowbackJson::strings(ctl->stats(), Stats::HALT_MASK);
        QString shortStatus;
        if (!blocker.isEmpty())      shortStatus = halts.isEmpty() ? tr("paused (supply cap)") : tr("paused: %1").arg(halts.join(", "));
        else if (!limit.isEmpty())   shortStatus = tr("limited to class %1").arg(ctl->mintableClasses().join(tr(" or ")));
        else                         shortStatus = tr("open");
        uiOverview->lblMintStatus->setText(shortStatus);
        uiOverview->lblMintStatus->setToolTip(!blocker.isEmpty() ? blocker : !limit.isEmpty() ? limit : tr("Every term class can mint now."));
    }

    uiOverview->lblSupply->setText(YellowbackFormat::cents(YellowbackJson::toInt(s, Stats::SUPPLY_CENTS)) % " / " %
                                   YellowbackFormat::zec(YellowbackJson::toInt(s, Stats::COLLATERAL_ZAT)));
    uiOverview->lblVaults->setText(QString::number(YellowbackJson::toInt(s, Stats::ACTIVE_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, Stats::VOID_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, Stats::CLOSED_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, Stats::CLAIMED_VAULTS)));
    uiOverview->lblUnbacked->setText(YellowbackFormat::cents(YellowbackJson::toInt(s, Stats::UNBACKED_CENTS)));

    // v3: the two pool cross-section prices and the arming state at the tip, from yed_getprice
    const json& pr = ctl->price();
    if (pr.empty()) {
        uiOverview->lblPoolPrices->setText("-");
        uiOverview->lblAttestation->setText("-");
    } else {
        uiOverview->lblPoolPrices->setText(YellowbackFormat::priceOrUndefined(pr, Price::X_MINT) % " / " %
                                           YellowbackFormat::priceOrUndefined(pr, Price::X_CLAIM));
        QString st = YellowbackJson::toStr(pr, Price::ATTEST_STATUS, "-");
        // The status alone in the (narrow) field; the explanation is the tooltip. The long form
        // was clipped to "ARMED — mints and" on the owner's first walk-through.
        if (YellowbackJson::toBool(pr, Price::ARMED)) {
            uiOverview->lblAttestation->setText(st);
            uiOverview->lblAttestation->setToolTip(tr("%1: mints and claims use attested prices, one both pools and attestors signed off on.").arg(st));
        } else if (st == Attest::STATUS_ARMED) {
            uiOverview->lblAttestation->setText(tr("%1 (disabled)").arg(st));
            uiOverview->lblAttestation->setToolTip(tr("The attestation layer is armed but disabled by the parameter set: prices come from pool quotes alone."));
        } else {
            uiOverview->lblAttestation->setText(st);
            uiOverview->lblAttestation->setToolTip(tr("%1: the attestation layer is not armed; prices come from pool quotes alone.").arg(st));
        }
    }

    refreshFundingSources();
}

void YellowbackTab::updateBalances() {
    if (ctl == nullptr) return;
    uiOverview->lblYedConfirmed->setText(YellowbackFormat::cents(ctl->confirmedCents()));
    uiOverview->lblYedUnconfirmed->setText(YellowbackFormat::cents(ctl->unconfirmedCents()));
    uiSend->lblAvailable->setText(YellowbackFormat::cents(ctl->confirmedCents()) %
        (ctl->unconfirmedCents() > 0 ? tr("  (+ %1 unconfirmed, not spendable yet)").arg(YellowbackFormat::cents(ctl->unconfirmedCents())) : ""));
}

// ── Receive ───────────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupReceive() {
    QObject::connect(uiReceive->btnNewAddress, &QPushButton::clicked, [=, this]() { newReceiveAddress(); });
    QObject::connect(uiReceive->btnCopy, &QPushButton::clicked, [=, this]() {
        if (receiveAddress.isEmpty()) return;
        QGuiApplication::clipboard()->setText(receiveAddress);
        uiReceive->lblReceiveStatus->setText(tr("Address copied to clipboard."));
    });
    QObject::connect(uiReceive->btnSaveLabel, &QPushButton::clicked, [=, this]() {
        if (receiveAddress.isEmpty()) return;
        AddressBook::getInstance()->addAddressLabel(uiReceive->txtLabel->text().trimmed(), receiveAddress);
        uiReceive->lblReceiveStatus->setText(tr("Label saved."));
    });
    QRegularExpressionValidator* v = new QRegularExpressionValidator(QRegularExpression(Settings::labelRegExp), uiReceive->txtLabel);
    uiReceive->txtLabel->setValidator(v);
}

void YellowbackTab::newReceiveAddress() {
    if (ctl == nullptr) return;
    uiReceive->lblReceiveStatus->setText(tr("Creating address..."));
    ctl->getNewAddress(
        [=, this](const json& r) {
            QString addr = r.is_string() ? QString::fromStdString(r.get<json::string_t>())
                                         : YellowbackJson::toStr(r, YellowbackRpc::ValidateAddress::ADDRESS);
            receiveAddress = addr;
            uiReceive->lblAddress->setText(addr);
            uiReceive->lblQr->setQrcodeString(addr);
            uiReceive->lblReceiveStatus->setText(tr("This is a %1 Yellowback address (prefix %2). Send only YED here.")
                .arg(ctl->network()).arg(ctl->addressPrefix()));
        },
        [=, this](const QString& e) {
            uiReceive->lblReceiveStatus->setText(tr("yed_getnewaddress failed: ") % e);
        });
}

// ── Send ──────────────────────────────────────────────────────────────────────────────────

// Dollars and cents from what the user typed, in either separator convention (audit F-3: a
// comma-decimal entry such as "12,50" used to parse as $1,250). When both '.' and ',' occur
// the last one is the decimal mark and the other groups thousands ("1,234.56", "1.234,56");
// a lone ',' or '.' followed by one or two digits is the decimal mark ("12,50", "1,5", "12.5");
// a lone ',' followed by exactly three digits groups thousands ("1,234"); a lone '.' followed
// by three digits is ambiguous and refused ("1.234"). Grouping must be in threes, at most two
// decimals, at most nine whole digits.
bool YellowbackTab::parseDollars(const QString& text, qint64* cents) {
    QString t = text.trimmed();
    t.remove('$').remove(' ');
    static const QRegularExpression chars("^[0-9.,]+$");
    if (!chars.match(t).hasMatch()) return false;
    const int lastDot = t.lastIndexOf('.'), lastComma = t.lastIndexOf(',');
    QChar decimal, group;                                           // null QChar = none
    if (lastDot >= 0 && lastComma >= 0) {
        decimal = lastDot > lastComma ? '.' : ',';
        group   = decimal == '.' ? ',' : '.';
    } else if (lastComma >= 0) {
        const int after = t.length() - lastComma - 1;
        if (after == 3 || t.count(',') > 1) group = ','; else decimal = ',';
    } else if (lastDot >= 0) {
        decimal = '.';
    }
    QString whole = t, frac;
    if (!decimal.isNull()) {
        const int at = t.lastIndexOf(decimal);
        if (t.indexOf(decimal) != at) return false;                 // two decimal marks
        whole = t.left(at); frac = t.mid(at + 1);
        if (frac.isEmpty() || frac.length() > 2 || (!group.isNull() && frac.contains(group))) return false;
    }
    if (!group.isNull()) {
        const QStringList groups = whole.split(group);
        if (groups.size() < 2 || groups[0].isEmpty() || groups[0].length() > 3) return false;
        for (int i = 1; i < groups.size(); i++) if (groups[i].length() != 3) return false;
        whole.remove(group);
    }
    static const QRegularExpression digits("^\\d{1,9}$");
    if (!digits.match(whole).hasMatch()) return false;
    if (frac.length() == 1) frac += "0";
    *cents = whole.toLongLong() * 100 + (frac.isEmpty() ? 0 : frac.toLongLong());
    return true;
}

void YellowbackTab::setupSend() {
    // The recipient field was a size-hint-wide box on macOS (the form's default keeps fields at their
    // hint): let the fields grow with the window, and start wide enough for a whole address.
    uiSend->txtRecipient->setMinimumWidth(uiSend->txtRecipient->fontMetrics().averageCharWidth() * 44);
    QObject::connect(uiSend->btnClear, &QPushButton::clicked, [=, this]() {
        uiSend->txtRecipient->clear();
        uiSend->txtAmount->clear();
        uiSend->lblSendHint->clear();
        uiSend->lblSendStatus->clear();
    });
    QObject::connect(uiSend->btnSend, &QPushButton::clicked, [=, this]() { doSend(); });

    auto validateLive = [=, this]() {
        if (ctl == nullptr) return;
        QString addr = uiSend->txtRecipient->text().trimmed();
        if (addr.isEmpty()) { uiSend->lblSendHint->clear(); return; }
        if (ctl->isShieldedAddress(addr))
            uiSend->lblSendHint->setText(tr("That is a Ycash address, not a Yellowback address. YED cannot be sent to s1..., ys... or z... addresses; ask the recipient for their Yellowback (%1...) address.").arg(ctl->addressPrefix()));
        else if (!ctl->looksLikeYellowbackAddress(addr))
            uiSend->lblSendHint->setText(tr("A %1 Yellowback address starts with %2.").arg(ctl->network()).arg(ctl->addressPrefix()));
        else
            uiSend->lblSendHint->clear();
    };
    QObject::connect(uiSend->txtRecipient, &QLineEdit::textChanged, validateLive);
    // The amount as the wallet reads it, beside the field while it is typed (audit F-3)
    QObject::connect(uiSend->txtAmount, &QLineEdit::textChanged, [=, this](const QString& text) {
        qint64 cents = 0;
        if (text.trimmed().isEmpty())           uiSend->lblAmountParsed->setText("-");
        else if (parseDollars(text, &cents))    uiSend->lblAmountParsed->setText("= " % YellowbackFormat::cents(cents));
        else                                    uiSend->lblAmountParsed->setText(tr("not an amount"));
    });
}

void YellowbackTab::doSend() {
    if (ctl == nullptr || !actionsEnabled) return;
    QString addr = uiSend->txtRecipient->text().trimmed();
    qint64 cents = 0;

    if (addr.isEmpty()) { uiSend->lblSendHint->setText(tr("Enter a recipient.")); return; }
    if (ctl->isShieldedAddress(addr)) {
        uiSend->lblSendHint->setText(tr("Refused: %1 is a Ycash address. YED can only be sent to a Yellowback (%2...) address.").arg(addr).arg(ctl->addressPrefix()));
        return;
    }
    if (!ctl->looksLikeYellowbackAddress(addr)) {
        uiSend->lblSendHint->setText(tr("Refused: not a %1 Yellowback address (expected prefix %2).").arg(ctl->network()).arg(ctl->addressPrefix()));
        return;
    }
    if (!parseDollars(uiSend->txtAmount->text(), &cents) || cents <= 0) {
        uiSend->lblSendHint->setText(tr("Enter an amount in dollars, e.g. 12.50."));
        return;
    }
    const qint64 minOut = ctl->minOutputCents();
    if (cents < minOut) {
        uiSend->lblSendHint->setText(tr("The smallest YED payment is %1.").arg(YellowbackFormat::cents(minOut)));
        return;
    }
    if (cents > ctl->confirmedCents()) {
        uiSend->lblSendHint->setText(tr("Only %1 is confirmed and spendable.").arg(YellowbackFormat::cents(ctl->confirmedCents())));
        return;
    }
    // Change floor (§4.6): the node refuses with `change-floor` when change would be under the
    // minimum output. This is only a first check on the whole balance; the node's message
    // names the exact workable amounts for the inputs it selected.
    qint64 change = ctl->confirmedCents() - cents;
    if (change > 0 && change < minOut) {
        uiSend->lblSendHint->setText(tr("This amount would leave %1 of change, below the %2 minimum. Send exactly %3 (everything) or at most %4.")
            .arg(YellowbackFormat::cents(change))
            .arg(YellowbackFormat::cents(minOut))
            .arg(YellowbackFormat::cents(ctl->confirmedCents()))
            .arg(YellowbackFormat::cents(ctl->confirmedCents() - minOut)));
        return;
    }
    uiSend->lblSendHint->clear();

    ctl->validateAddress(addr,
        [=, this](const json& v) {
            if (!YellowbackJson::toBool(v, YellowbackRpc::ValidateAddress::ISVALID)) {
                uiSend->lblSendHint->setText(tr("The node rejected this address: %1")
                    .arg(YellowbackJson::toStr(v, YellowbackRpc::ValidateAddress::REASON, tr("invalid"))));
                return;
            }
            QString text = tr("Send %1 of YED to\n%2\n\nThe YEC network fee is paid from your transparent YEC. "
                              "This transfer is transparent and cannot be undone.")
                              .arg(YellowbackFormat::cents(cents)).arg(addr);
            if (!confirm(tr("Confirm Yellowback transfer"), text))
                return;

            uiSend->btnSend->setEnabled(false);
            uiSend->lblSendStatus->setText(tr("Sending..."));
            ctl->send(addr, cents,
                [=, this](const json& r) {
                    uiSend->btnSend->setEnabled(actionsEnabled);
                    QString txid = YellowbackJson::toStr(r, YellowbackRpc::SendResult::TXID);
                    uiSend->lblSendStatus->setText(tr("Sent %1. txid: %2 (change %3, expires at height %4 unless mined)")
                        .arg(YellowbackFormat::cents(cents)).arg(txid)
                        .arg(YellowbackFormat::cents(YellowbackJson::toInt(r, YellowbackRpc::SendResult::CHANGE_CENTS)))
                        .arg(YellowbackJson::toInt(r, YellowbackRpc::SendResult::EXPIRY_HEIGHT)));
                    uiSend->txtAmount->clear();
                    ctl->refresh(true);
                },
                [=, this](const QString& e) {
                    uiSend->btnSend->setEnabled(actionsEnabled);
                    uiSend->lblSendStatus->setText(tr("yed_send failed: ") % e);
                    // change-floor (§4.6): the node names the two amounts that work for the
                    // inputs it selected — offer them in the hint so the user can pick one.
                    qint64 all = 0, atMost = 0;
                    if (e.startsWith(YellowbackRpc::Errors::CHANGE_FLOOR, Qt::CaseInsensitive) || e.contains("(C20)")) {
                        uiSend->lblSendHint->setText(YellowbackController::parseChangeFloor(e, &all, &atMost)
                            ? tr("The change left over would be below the %1 minimum. Send exactly %2 (everything the node selected) or at most %3.")
                                  .arg(YellowbackFormat::cents(ctl->minOutputCents())).arg(YellowbackFormat::cents(all)).arg(YellowbackFormat::cents(atMost))
                            : tr("The change left over would be below the minimum output; the node's message names the amounts that work."));
                    }
                    failed("yed_send", e);
                });
        },
        [=, this](const QString& e) {
            uiSend->lblSendHint->setText(tr("yed_validateaddress failed: ") % e);
        });
}

// ── Mint ──────────────────────────────────────────────────────────────────────────────────
// The page derives the class from the lock length and shows yed_estimatecollateral as the user
// types; Mint is enabled only for an estimate that matches the current input and passes the
// MINTPOL-1 gate. The confirmation adds the pool fee and payee (yed_getfeepayee) and
// then makes the one yed_mint call (§4.8 Mint row).

void YellowbackTab::setupMint() {
    estimateTimer = new QTimer(this);
    estimateTimer->setSingleShot(true);
    estimateTimer->setInterval(400);
    QObject::connect(estimateTimer, &QTimer::timeout, [=, this]() { requestEstimate(); });

    QObject::connect(uiMint->txtAmount, &QLineEdit::textChanged, [=, this](const QString&) { estimateTimer->start(); });
    QObject::connect(uiMint->cmbTier, &QComboBox::currentIndexChanged, [=, this](int) { estimateTimer->start(); });
    QObject::connect(uiMint->cmbFundFrom, &QComboBox::currentIndexChanged, [=, this](int) {
        if (ctl != nullptr) uiMint->lblYecAvailable->setText(Settings::getZECDisplayFormat(ctl->yecBalanceAt(fundingSource())));
        estimateTimer->start();
    });
    QObject::connect(uiMint->btnMint, &QPushButton::clicked, [=, this]() { doMint(); });
    refreshFundingSources();

    uiMint->lblGate->setVisible(false);
    uiMint->lblClassNote->setVisible(false);
    uiMint->btnMint->setEnabled(false);
    uiMint->lblMintPageStatus->setText(tr("Your own node builds, signs and sends the mint. Back up wallet.dat afterwards: the vault's key exists only there."));
}

void YellowbackTab::updateMintClasses() {
    if (ctl == nullptr) return;
    // H-5: a disabled class (empty term range) is not offered; the class note says why
    auto classes = ctl->enabledClasses();
    uiMint->m1->setText(tr("Amount to mint (dollars, %1 – %2)").arg(YellowbackFormat::cents(ctl->minMintCents())).arg(YellowbackFormat::cents(ctl->maxMintCents())));
    const QString note = ctl->mintClassNote();
    uiMint->lblClassNote->setText(note);
    uiMint->lblClassNote->setVisible(!note.isEmpty());
    if (classes.isEmpty() || uiMint->cmbTier->count() == classes.size()) return;
    QSignalBlocker block(uiMint->cmbTier);
    uiMint->cmbTier->clear();
    for (const auto& c : classes)
        uiMint->cmbTier->addItem(tr("Class %1: %2–%3 blocks (~%4–%5 days), base ratio %6").arg(c.name)
            .arg(c.minBlocks).arg(c.maxBlocks)
            .arg((qint64)c.minBlocks * YellowbackRpc::SECONDS_PER_BLOCK / 86400)
            .arg((qint64)c.maxBlocks * YellowbackRpc::SECONDS_PER_BLOCK / 86400)
            .arg(YellowbackFormat::bpsAsPercent(c.baseRatioBps)), c.minBlocks);
}

QString YellowbackTab::fundingSource() const {
    return uiMint->cmbFundFrom->currentData().toString();
}

void YellowbackTab::refreshFundingSources() {
    // Plan I2: the transparent total, then every Sapling address of the wallet with its balance.
    // Rebuilt on each refresh cycle; the current choice survives as long as the address still exists.
    QString keep = fundingSource();
    QSignalBlocker block(uiMint->cmbFundFrom);
    uiMint->cmbFundFrom->clear();
    double t = ctl != nullptr ? ctl->yecBalance() : 0.0;
    uiMint->cmbFundFrom->addItem(tr("Transparent balance (all s1… addresses): %1").arg(Settings::getZECDisplayFormat(t)), QString());
    if (ctl != nullptr) {
        for (const auto& a : ctl->saplingAddresses()) {
            uiMint->cmbFundFrom->addItem(tr("Shielded %1…%2: %3").arg(a.first.left(14)).arg(a.first.right(6))
                                             .arg(Settings::getZECDisplayFormat(a.second)), a.first);
        }
    }
    int idx = keep.isEmpty() ? 0 : uiMint->cmbFundFrom->findData(keep);
    uiMint->cmbFundFrom->setCurrentIndex(idx < 0 ? 0 : idx);
    uiMint->lblYecAvailable->setText(ctl != nullptr ? Settings::getZECDisplayFormat(ctl->yecBalanceAt(fundingSource())) : "-");
}

void YellowbackTab::updateMintGate() {
    if (ctl == nullptr) return;
    qint64 cents = 0;
    bool haveAmount = parseDollars(uiMint->txtAmount->text(), &cents);
    const QString termClass = ctl->classForLock(uiMint->cmbTier->currentData().toInt()).name;
    QString blocker = ctl->mintBlocker(haveAmount ? cents : 0, termClass);
    // W16: under a global-ratio halt the page stays usable for the recapitalising class, with the
    // limit shown in the same banner; the classes that cannot mint are greyed out in the list
    const QString limit = ctl->mintLimit();
    uiMint->lblGate->setVisible(!blocker.isEmpty() || !limit.isEmpty());
    uiMint->lblGate->setText(blocker.isEmpty() ? limit : blocker);
    applyMintableClasses();
    // The "Minted. txid: …" line outlives its usefulness two blocks later (the owner saw it sit on
    // the page through a price shock); the notice and the Transactions list carry the record
    if (mintedStatusHeight >= 0 && ctl->height() >= mintedStatusHeight + 2) {
        mintedStatusHeight = -1;
        if (uiMint->lblMintPageStatus->text().startsWith(tr("Minted. txid: "))) uiMint->lblMintPageStatus->clear();
    }
    bool estimateCurrent = haveAmount && estimateZat >= 0 && estimateCents == cents &&
                           estimateLockBlocks == uiMint->cmbTier->currentData().toInt();
    uiMint->btnMint->setEnabled(actionsEnabled && blocker.isEmpty() && estimateCurrent);
}

void YellowbackTab::applyMintableClasses() {
    if (ctl == nullptr) return;
    auto* model = qobject_cast<QStandardItemModel*>(uiMint->cmbTier->model());
    if (model == nullptr) return;
    const QStringList mintable = ctl->mintableClasses();
    const bool limited = !ctl->mintLimit().isEmpty();
    for (int i = 0; i < uiMint->cmbTier->count(); i++) {
        QStandardItem* item = model->item(i);
        if (item == nullptr) continue;
        const QString cls = ctl->classForLock(uiMint->cmbTier->itemData(i).toInt()).name;
        item->setEnabled(!limited || mintable.contains(cls));
    }
}

void YellowbackTab::requestEstimate() {
    if (ctl == nullptr) return;
    qint64 cents = 0;
    int lockBlocks = uiMint->cmbTier->currentData().toInt();
    if (!parseDollars(uiMint->txtAmount->text(), &cents) || cents <= 0 || lockBlocks <= 0) {
        estimateZat = -1;
        uiMint->lblCollateral->setText("-");
        uiMint->lblUnlock->setText("-");
        uiMint->lblRatio->setText("-");
        uiMint->lblPrice->setText("-");
        uiMint->lblSource->setText("-");
        uiMint->lblDivergence->setVisible(false);
        uiMint->lblMintHint->clear();
        updateMintGate();
        return;
    }
    if (cents < ctl->minMintCents() || cents > ctl->maxMintCents()) {
        estimateZat = -1;
        uiMint->lblMintHint->setText(tr("A mint must be between %1 and %2.")
            .arg(YellowbackFormat::cents(ctl->minMintCents())).arg(YellowbackFormat::cents(ctl->maxMintCents())));
        updateMintGate();
        return;
    }
    uiMint->lblMintHint->clear();

    int seq = ++estimateSeq;
    ctl->estimateCollateral(cents, lockBlocks,
        [=, this](const json& e) {
            if (seq != estimateSeq) return;      // a newer request is in flight
            using namespace YellowbackRpc::Estimate;
            estimateCents      = cents;
            estimateLockBlocks = lockBlocks;
            estimateZat        = YellowbackJson::toInt(e, REQUIRED_ZAT, -1);
            uiMint->lblRatio->setText(tr("%1 (base %2 × σ %3)")
                .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, MIN_RATIO_BPS)))
                .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, BASE_RATIO_BPS)))
                .arg(YellowbackFormat::bpsAsMultiplier(YellowbackJson::toInt(e, SIGMA_MULT_BPS, 10000))));
            uiMint->lblPrice->setText(YellowbackFormat::priceOrUndefined(e, P_MINT) % tr(" per YEC"));
            // v3: the two source bounds and which one set pMint (display only; the two-step
            // mint flow and bundle-insufficient handling come with A5-b)
            QString source = YellowbackJson::toStr(e, SOURCE);
            if (!YellowbackJson::toBool(e, ARMED) || source.isEmpty())
                uiMint->lblSource->setText(tr("pools %1 (attestation layer not armed)").arg(YellowbackFormat::priceOrUndefined(e, X_MINT)));
            else
                uiMint->lblSource->setText(tr("pools %1, attestors %2 — %3")
                    .arg(YellowbackFormat::priceOrUndefined(e, X_MINT)).arg(YellowbackFormat::priceOrUndefined(e, A_MINT))
                    .arg(source == SOURCE_A ? tr("the attestors' price bound the mint") : tr("the pools' price bound the mint")));
            QString diverged = YellowbackController::describeDivergence(e, ctl->divergeBpsAttest());
            uiMint->lblDivergence->setText(diverged);
            uiMint->lblDivergence->setVisible(!diverged.isEmpty());
            // requiredZat is null when pMint is undefined: no estimate, the gate says why
            if (estimateZat < 0) {
                uiMint->lblCollateral->setText("-");
                uiMint->lblUnlock->setText("-");
                uiMint->lblMintHint->setText(tr("No estimate: the mint price is undefined at the reference height."));
                updateMintGate();
                return;
            }
            uiMint->lblCollateral->setText(YellowbackFormat::zec(estimateZat));
            int lock = (int)YellowbackJson::toInt(e, LOCK_HEIGHT);
            uiMint->lblUnlock->setText(YellowbackFormat::heightWithEstimate(lock, ctl->height()) %
                tr("  — class %1, claim height %2; dates are estimates at 75 s/block")
                    .arg(YellowbackJson::toStr(e, TERM_CLASS)).arg(YellowbackJson::toInt(e, CLAIM_HEIGHT)));
            double haveZec = ctl->yecBalanceAt(fundingSource());
            if ((double)estimateZat / 100000000.0 > haveZec)
                uiMint->lblMintHint->setText(fundingSource().isEmpty()
                    ? tr("You have %1 of transparent YEC; this mint needs %2 plus the fees. Choose a shielded address above to fund it from there instead.")
                          .arg(Settings::getZECDisplayFormat(haveZec)).arg(YellowbackFormat::zec(estimateZat))
                    : tr("The selected shielded address holds %1 of YEC; this mint needs %2 plus the fees.")
                          .arg(Settings::getZECDisplayFormat(haveZec)).arg(YellowbackFormat::zec(estimateZat)));
            updateMintGate();
        },
        [=, this](const QString& err) {
            if (seq != estimateSeq) return;
            estimateZat = -1;
            uiMint->lblCollateral->setText("-");
            uiMint->lblMintHint->setText(tr("yed_estimatecollateral failed: ") % err);
            // v3: mint10-diverged is the one refusal with its own banner (§4.8 Mint row)
            QString diverged = YellowbackController::describeDivergenceError(err, ctl->divergeBpsAttest());
            uiMint->lblDivergence->setText(diverged);
            uiMint->lblDivergence->setVisible(!diverged.isEmpty());
            updateMintGate();
        });
}

// v3: "n of m selected attestors reachable" from yed_getselection at the reference height a
// mint built now would cite. Display only; A5-b makes the mint action wait on it.
void YellowbackTab::updateMintAttest() {
    if (ctl == nullptr) { uiMint->lblSelection->setText("-"); return; }
    QString line = YellowbackController::describeSelection(ctl->selection());
    uiMint->lblSelection->setText(line.isEmpty() ? tr("not required (attestation layer not armed)") : line);
}

// H-9.3: the refusal when the node's figures fail the wallet's own recomputation
static QString implausibleText(const QString& what, const QStringList& bad) {
    return QObject::tr("The node's figures for this %1 do not match what the wallet computes from the network's parameters, so nothing was sent:\n\n- %2\n\n"
                       "If the chain just advanced, try again. Otherwise the node is misconfigured, or is not the node you meant to trust with this wallet.")
        .arg(what).arg(bad.join("\n- "));
}

void YellowbackTab::doMint() {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    qint64 cents = 0;
    int lockBlocks = uiMint->cmbTier->currentData().toInt();
    if (!parseDollars(uiMint->txtAmount->text(), &cents) || cents <= 0 || lockBlocks <= 0) {
        uiMint->lblMintHint->setText(tr("Enter an amount in dollars and choose a lock length."));
        return;
    }
    // the class matters: under the global-ratio halt or above the supply cap only some classes mint (W16, W20)
    QString blocker = ctl->mintBlocker(cents, ctl->classForLock(lockBlocks).name);
    if (!blocker.isEmpty()) { uiMint->lblMintHint->setText(blocker); return; }
    auto cls = ctl->classForLock(lockBlocks);
    if (cls.name.isEmpty()) { uiMint->lblMintHint->setText(tr("A lock of %1 blocks falls in no term class.").arg(lockBlocks)); return; }
    const QString from = fundingSource();

    uiMint->btnMint->setEnabled(false);
    uiMint->lblMintPageStatus->setText(tr("Estimating..."));
    // A fresh estimate at confirmation time: the figure the user agrees to is the one the node
    // would use now, not the debounced one from a few seconds ago.
    ctl->estimateCollateral(cents, lockBlocks,
        [=, this](const json& e) {
            if (YellowbackJson::isNull(e, Estimate::REQUIRED_ZAT)) {
                updateMintGate();
                uiMint->lblMintPageStatus->setText(tr("No estimate: the mint price is undefined at the reference height."));
                return;
            }
            // H-9.3: every figure the dialog shows is recomputed from yed_getinfo.params first
            const QStringList bad = YellowbackController::checkEstimate(ctl->params(), ctl->height(), cents, lockBlocks, e);
            if (!bad.isEmpty()) {
                updateMintGate();
                uiMint->lblMintPageStatus->setText(tr("Not sent: the node's estimate failed the wallet's check."));
                notice(tr("Mint not sent"), implausibleText(tr("mint"), bad), true);
                return;
            }
            const qint64 required   = YellowbackJson::toInt(e, Estimate::REQUIRED_ZAT);
            // MINT-5: the vault locks max(required, 4 · FEE_MIN), so the figure shown and capped is that one
            const qint64 collateral = YellowbackController::mintCollateralZatFor(ctl->params(), required);
            const int    lockHeight = (int)YellowbackJson::toInt(e, Estimate::LOCK_HEIGHT);
            const int    claimHeight= (int)YellowbackJson::toInt(e, Estimate::CLAIM_HEIGHT);
            const int    refHeight  = (int)YellowbackJson::toInt(e, Estimate::REF_HEIGHT, ctl->refHeightNow());
            const QString termClass = YellowbackJson::toStr(e, Estimate::TERM_CLASS, cls.name);
            const QString ratio = tr("%1 (base %2 × σ %3)")
                .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, Estimate::MIN_RATIO_BPS)))
                .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, Estimate::BASE_RATIO_BPS)))
                .arg(YellowbackFormat::bpsAsMultiplier(YellowbackJson::toInt(e, Estimate::SIGMA_MULT_BPS, 10000)));
            const QString price = YellowbackFormat::priceOrUndefined(e, Estimate::P_MINT);
            // The cap the node is held to (audit F-1): 1 % above the estimate, so a block that
            // moves the reference price by more than that refuses the mint instead of locking
            // more YEC than the dialog showed.
            const qint64 maxCollateral = capAbove(collateral);
            const int    heightAtDialog = ctl->height();

            auto ask = [=, this](const QString& feeLine) {
                QString text = tr("Mint %1 of YED against %2 of YEC locked in a new vault (at most %14; the node refuses the mint if the price moves further).\n\n"
                                  "Lock: %3 blocks (class %4, about %5 days). Collateral can leave the vault from height %6 on (%7); "
                                  "its claim height is %8 (%15): redeem or renew it before then.\n"
                                  "Collateral ratio: %9 at a mint price of %10 per YEC (reference height %11).\n"
                                  "%12\n"
                                  "Funded from: %13.\n\n"
                                  "Every full node checks, as Ycash consensus rules, that the collateral cannot leave the vault before its lock height, that only your key can spend it before the claim height, "
                                  "and that it is released only against the burn of %1 of YED. After the claim height anyone may claim the vault if it is underwater, "
                                  "after a delay in which a member of the attestor set can cancel a claim made at a wrong price.\n\n"
                                  "Back up wallet.dat after this mint: the vault's key is created now and exists only in that file.")
                    .arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::zec(collateral))
                    .arg(lockBlocks).arg(termClass).arg((qint64)lockBlocks * SECONDS_PER_BLOCK / 86400)
                    .arg(lockHeight).arg(YellowbackFormat::estimateDate(lockHeight, ctl->height()).toString("yyyy-MM-dd") % tr(", estimated"))
                    .arg(claimHeight).arg(ratio).arg(price).arg(refHeight).arg(feeLine)
                    .arg(from.isEmpty() ? tr("your transparent YEC") : tr("shielded address %1").arg(from))
                    .arg(YellowbackFormat::zec(maxCollateral))
                    .arg(YellowbackFormat::estimateDate(claimHeight, ctl->height()).toString("yyyy-MM-dd") % tr(", estimated"));
                if (!confirm(tr("Confirm mint"), text)) {
                    updateMintGate();
                    uiMint->lblMintPageStatus->clear();
                    return;
                }
                // A block arrived while the dialog was open: the estimate it showed is stale, so
                // estimate again and show the dialog again rather than send the old figure.
                if (ctl->height() != heightAtDialog) {
                    uiMint->lblMintPageStatus->setText(tr("The chain advanced while you were confirming; estimating again."));
                    doMint();
                    return;
                }
                uiMint->lblMintPageStatus->setText(tr("Minting..."));
                ctl->mint(cents, lockBlocks, from, maxCollateral,
                    [=, this](const json& r) {
                        // The vault key exists from the carrier step on: nag for the backup at once
                        Settings::getInstance()->setYellowbackBackupPending(true);
                        updateBackupNag();
                        followPending(Transaction::TYPE_MINT, r, uiMint->lblMintPageStatus, [=, this](const json& done) {
                            QString summary = mintSummary(cents, done, collateral);
                            QString warning = YellowbackJson::toStr(r, MintResult::WARNING);
                            if (!warning.isEmpty()) summary += "\n\n" % tr("Node warning: ") % warning;
                            summary += "\n\n" % tr("The YED arrives once the transaction is mined. Back up wallet.dat now.");
                            uiMint->lblMintPageStatus->setText(tr("Minted. txid: ") % YellowbackJson::toStr(done, MintResult::TXID));
                            mintedStatusHeight = ctl->height();
                            uiMint->txtAmount->clear();
                            notice(tr("Mint sent"), summary);
                            ctl->refresh(true);
                        });
                    },
                    [=, this](const QString& e) {
                        updateMintGate();
                        uiMint->lblMintPageStatus->setText(tr("yed_mint failed: ") % e);
                        if (bundleInsufficientRetry(tr("The mint"), e)) return;
                        // mint10-diverged: the page's own banner, the same one the estimate shows
                        QString diverged = YellowbackController::describeDivergenceError(e, ctl->divergeBpsAttest());
                        uiMint->lblDivergence->setText(diverged);
                        uiMint->lblDivergence->setVisible(!diverged.isEmpty());
                        failed("yed_mint", e);
                    });
            };

            // The pool fee and its payee (FEE-1, FEE-W) for this collateral at the
            // reference height; under FEE-0 the node refuses with fee-no-eligible-payee and the
            // mint carries no fee output.
            ctl->getFeePayee(refHeight, collateral,
                [=, this](const json& f) {
                    const json& def = YellowbackJson::obj(f, FeePayee::DEFAULT);
                    QString payee = YellowbackJson::has(f, FeePayee::PREFERRED) && !YellowbackJson::isNull(f, FeePayee::PREFERRED)
                        ? YellowbackJson::toStr(f, FeePayee::PREFERRED)
                        : YellowbackJson::toStr(def, FeePayeeDefault::PAYOUT_ADDRESS);
                    // H-9.3: FEE-1 recomputed locally; the node's figure must match it
                    const qint64 fee = YellowbackController::feeZatFor(ctl->params(), collateral);
                    if (YellowbackJson::toInt(f, FeePayee::FEE_ZAT) != fee) {
                        updateMintGate();
                        uiMint->lblMintPageStatus->setText(tr("Not sent: the node's fee failed the wallet's check."));
                        notice(tr("Mint not sent"), implausibleText(tr("mint"), { tr("the pool fee is %1 where FEE-1 of the collateral %2 gives %3")
                            .arg(YellowbackFormat::zec(YellowbackJson::toInt(f, FeePayee::FEE_ZAT))).arg(YellowbackFormat::zec(collateral)).arg(YellowbackFormat::zec(fee)) }), true);
                        return;
                    }
                    ask(tr("Pool fee: %1 of YEC, paid from your own YEC on top of the collateral (as are any attestation fee and the network fee) to the pool %2 (a pool that published a price quote in the 100 blocks up to the reference height).")
                            .arg(YellowbackFormat::zec(fee)).arg(payee));
                },
                [=, this](const QString& e) {
                    if (e.startsWith(Errors::FEE_NO_ELIGIBLE_PAYEE, Qt::CaseInsensitive))
                        ask(tr("Pool fee: none (no pool published a price quote in the payee window, so the mint carries no fee output)."));
                    else
                        ask(tr("Pool fee: could not be estimated (%1); the node reports the fee it pays after the mint.").arg(e));
                });
        },
        [=, this](const QString& e) {
            updateMintGate();
            uiMint->lblMintPageStatus->setText(tr("yed_estimatecollateral failed: ") % e);
            failed("yed_estimatecollateral", e);
        });
}

// The mint result, from yed_mint's full shape or from yed_gettxinfo after a pending reply: the
// keys the two share (txid, feeZat, payee, pMint, xMint, aMint, bundleSeqs, attestFeeZat,
// attestPayee) are read the same way; what only yed_mint carries (vault, class, heights) is
// shown when present.
QString YellowbackTab::mintSummary(qint64 cents, const json& r, qint64 confirmedZat) const {
    using namespace YellowbackRpc;
    const QString txid = YellowbackJson::toStr(r, MintResult::TXID);
    QString out = tr("Minted %1 of YED.\ntxid %2").arg(YellowbackFormat::cents(cents)).arg(txid);
    // The collateral the vault really locked against the figure the dialog showed (audit F-1)
    if (confirmedZat > 0 && YellowbackJson::has(r, MintResult::COLLATERAL_ZAT)) {
        const qint64 locked = YellowbackJson::toInt(r, MintResult::COLLATERAL_ZAT);
        if (locked > 0 && locked != confirmedZat)
            out = tr("NOTE: the vault locked %1 of YEC, not the %2 shown when you confirmed (the reference price moved between the dialog and the mint).")
                      .arg(YellowbackFormat::zec(locked)).arg(YellowbackFormat::zec(confirmedZat)) % "\n\n" % out;
    }
    if (YellowbackJson::has(r, MintResult::VAULT))
        out += "\n" % tr("vault %1 (class %2)\ncollateral %3, lock height %4, claim height %5")
            .arg(YellowbackJson::toStr(r, MintResult::VAULT)).arg(YellowbackJson::toStr(r, MintResult::TERM_CLASS))
            .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, MintResult::COLLATERAL_ZAT)))
            .arg(YellowbackJson::toInt(r, MintResult::LOCK_HEIGHT)).arg(YellowbackJson::toInt(r, MintResult::CLAIM_HEIGHT));
    else
        out += "\n" % tr("vault %1:0").arg(txid);
    QString payee = YellowbackJson::isNull(r, MintResult::PAYEE) ? tr("none") : YellowbackJson::toStr(r, MintResult::PAYEE);
    out += "\n" % tr("pool fee %1 to %2").arg(YellowbackFormat::zec(YellowbackJson::toInt(r, MintResult::FEE_ZAT))).arg(payee);
    if (YellowbackJson::has(r, MintResult::FUNDED_FROM))
        out += "\n" % tr("funded from %1").arg(YellowbackJson::toStr(r, MintResult::FUNDED_FROM));
    // v3: the price the mint was sized at and which source bound it
    if (YellowbackJson::has(r, MintResult::P_MINT)) {
        QString source = YellowbackJson::toStr(r, MintResult::SOURCE);
        if (source.isEmpty() && YellowbackJson::has(r, MintResult::A_MINT))
            source = YellowbackJson::toInt(r, MintResult::A_MINT) <= YellowbackJson::toInt(r, MintResult::X_MINT) ? Estimate::SOURCE_A : Estimate::SOURCE_X;
        out += "\n" % tr("mint price %1 per YEC — %2 (pools %3, attestors %4)")
            .arg(YellowbackFormat::price(YellowbackJson::toInt(r, MintResult::P_MINT)))
            .arg(source == Estimate::SOURCE_A ? tr("bound by the attestors") : source == Estimate::SOURCE_X ? tr("bound by the pools") : tr("pool quotes alone, layer not armed"))
            .arg(YellowbackFormat::priceOrUndefined(r, MintResult::X_MINT)).arg(YellowbackFormat::priceOrUndefined(r, MintResult::A_MINT));
    }
    if (r.is_object() && r.find(MintResult::BUNDLE_SEQS) != r.end() && r[MintResult::BUNDLE_SEQS].is_array()) {
        QStringList seqs;
        for (auto& it : r[MintResult::BUNDLE_SEQS]) if (it.is_number()) seqs << QString::number(it.get<qint64>());
        out += "\n" % tr("price proof from attestor seq %1").arg(seqs.isEmpty() ? tr("none (no bundle)") : seqs.join(", "));
    }
    qint64 attestFee = YellowbackJson::toInt(r, MintResult::ATTEST_FEE_ZAT);
    if (YellowbackJson::has(r, MintResult::ATTEST_FEE_ZAT))
        out += "\n" % (attestFee > 0
            ? tr("attestation fee %1 to %2").arg(YellowbackFormat::zec(attestFee)).arg(YellowbackJson::toStr(r, MintResult::ATTEST_PAYEE, tr("an attestor")))
            : tr("attestation fee: none"));
    return out;
}

QString YellowbackTab::claimSummary(const json& r) const {
    using namespace YellowbackRpc;
    // yed_claim's shape and yed_gettxinfo's differ in the burn key only
    qint64 burned = YellowbackJson::has(r, RedeemResult::BURNED_CENTS) ? YellowbackJson::toInt(r, RedeemResult::BURNED_CENTS)
                                                                        : YellowbackJson::toInt(r, TxInfo::BURNED);
    QString payee = YellowbackJson::isNull(r, RedeemResult::PAYEE) ? tr("none") : YellowbackJson::toStr(r, RedeemResult::PAYEE);
    QString out = tr("txid %1\nYED burned: %2%3\npool fee: %4 to %5")
        .arg(YellowbackJson::toStr(r, RedeemResult::TXID)).arg(YellowbackFormat::cents(burned)).arg(extraBurnLine(r))
        .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RedeemResult::FEE_ZAT))).arg(payee);
    // rpcversion 5 (U-23): the collateral sits in a claimant intent until CLAIM_DELAY has passed
    const QString txid = YellowbackJson::toStr(r, RedeemResult::TXID);
    const int     h    = (int)YellowbackJson::toInt(r, TxInfo::HEIGHT, 0);
    const QString releaseAt = h > 0 && ctl != nullptr ? tr("height %1").arg(h + ctl->claimDelay())
                                                      : tr("%1 blocks after the claim confirms").arg(ctl != nullptr ? ctl->claimDelay() : 0);
    if (YellowbackJson::has(r, RedeemResult::COLLATERAL_OUT))
        out += "\n" % tr("claim intent: %1 for %2, in intent %3:0").arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RedeemResult::COLLATERAL_OUT)))
                           .arg(YellowbackJson::toStr(r, RedeemResult::TO)).arg(txid);
    else
        out += "\n" % tr("claim intent %1:0").arg(txid);
    out += "\n" % tr("Release it on the Pending claims page from %1 on. Until then a member of the attestor set may cancel the claim if it was made at a wrong price: "
                      "the collateral then returns to the vault and the YED burned is not refunded.").arg(releaseAt);
    QString path = YellowbackJson::toStr(r, ClaimResult::CLAIM_PATH);
    if (path == ClaimResult::PATH_B)
        out += "\n" % tr("claim path: emergency clause (b), after a persisted notice; the claimant keeps exactly the debt at the claim price");
    else if (path == ClaimResult::PATH_A)
        out += "\n" % tr("claim path: underwater (a) at the combined claim price %1").arg(YellowbackFormat::priceOrUndefined(r, ClaimResult::P_CLAIM));
    qint64 residual = YellowbackJson::toInt(r, ClaimResult::RESIDUAL_ZAT);
    if (YellowbackJson::has(r, ClaimResult::RESIDUAL_ZAT))
        out += "\n" % (residual > 0 ? tr("residual intent for the vault owner: %1 (anyone may release it to the owner after the delay)").arg(YellowbackFormat::zec(residual)) : tr("residual to the owner: none"));
    qint64 attestFee = YellowbackJson::toInt(r, ClaimResult::ATTEST_FEE_ZAT);
    if (YellowbackJson::has(r, ClaimResult::ATTEST_FEE_ZAT))
        out += "\n" % (attestFee > 0 ? tr("attestation fee %1 to %2").arg(YellowbackFormat::zec(attestFee)).arg(YellowbackJson::toStr(r, ClaimResult::ATTEST_PAYEE, tr("an attestor")))
                                     : tr("attestation fee: none"));
    return out;
}

// ── Vaults (positions) ────────────────────────────────────────────────────────────────────

YellowbackTab::VaultActions YellowbackTab::vaultActions(const YellowbackPosition& p, int height) {
    using namespace YellowbackRpc::Position;
    VaultActions a;
    const QString lockAt = tr("lock height %1").arg(p.lockHeight);
    if (p.status == STATUS_VOID) {
        if (p.canRedeem || height >= p.lockHeight) {
            a.release = true;
            a.text = tr("VOID vault (recorded before the vault upgrade): Release returns the collateral with no YED burned and no fee paid. "
                        "Do it before height %1.").arg(p.claimHeight);
        } else {
            a.text = tr("VOID vault (recorded before the vault upgrade): the collateral can be released from %1 on (no burn, no fee), and should be released before height %2.")
                        .arg(lockAt).arg(p.claimHeight);
        }
    } else if (p.status == STATUS_CLAIMING) {
        // rpcversion 5 (U-23, U-24): someone claimed it; the owner has nothing to do but watch
        const YellowbackIntent* c = p.claimantIntent();
        a.text = c == nullptr ? tr("This vault is being claimed.")
            : height + 1 < c->releaseHeight
                ? tr("This vault is being claimed (claim %1 at height %2, %3 of YED burned): the collateral goes to the claimant at height %4 "
                     "unless a member of the attestor set cancels the claim as made at a wrong price, which makes the vault ACTIVE again.")
                      .arg(c->txid.left(16) % "…").arg(c->height).arg(YellowbackFormat::cents(p.mintedCents)).arg(c->releaseHeight)
                : tr("This vault was claimed (claim %1 at height %2): its claim delay has passed and the claimant may release the collateral now.")
                      .arg(c->txid.left(16) % "…").arg(c->height);
        if (const YellowbackIntent* r = p.residualIntent())
            a.text += " " % tr("Your residual (the collateral above the claimant's share) waits in intent %1 and can be released to you from height %2 (Pending claims page).")
                                .arg(r->outpoint()).arg(r->releaseHeight);
    } else if (p.status == STATUS_ACTIVE) {
        // H-9.2: the two deadlines as dates, on every ACTIVE vault
        const QString lockDate  = YellowbackFormat::estimateDate(p.lockHeight, height).toString("yyyy-MM-dd HH:mm");
        const QString claimDate = YellowbackFormat::estimateDate(p.claimHeight, height).toString("yyyy-MM-dd HH:mm");
        if (p.canRedeem || height >= p.lockHeight) {
            a.redeem = true;
            a.renew  = true;
            a.text += (a.text.isEmpty() ? QString() : QString(" ")) %
                      tr("Redeem burns %1 of your YED and pays a pool fee from the collateral; the rest returns to you.")
                          .arg(YellowbackFormat::cents(p.mintedCents));
            a.text += " " % tr("Renew redeems it and mints %1 again in a new vault, in one confirmation.").arg(YellowbackFormat::cents(p.mintedCents));
            a.text += " " % tr("Claim height %1 (~%2): act before it.").arg(p.claimHeight).arg(claimDate);
        } else {
            a.text = tr("Active vault: redeemable or renewable from %1 (~%2) on by burning %3 of YED; claim height %4 (~%5).")
                         .arg(lockAt).arg(lockDate).arg(YellowbackFormat::cents(p.mintedCents)).arg(p.claimHeight).arg(claimDate);
        }
        if (p.claimable)
            a.text += " " % tr("This vault is past its claim height and underwater: anyone may claim it by burning its debt.");
        if (!p.scriptPubKey.isEmpty())
            a.text += " " % tr("Its collateral is a Ycash vault output (the YED vault template under the attestor set).");
        // v3: a standing claim notice (NOT-1) opens the emergency claim clause after it persists
        if (p.noticed)
            a.text += " " % tr("A claim notice stands against it (confirmed at height %1): from reference height %2 a claim may take the collateral under the emergency clause. Redeem or add collateral before then.")
                                .arg(p.noticeHeight).arg(p.emergencyOpenAt);
        else if (p.canNotice)
            a.text += " " % tr("Under the attested prices this node holds it is below the emergency ratio: a claim notice could be posted against it.");
    } else if (p.status == STATUS_CLOSED) {
        a.text = p.unbacked
            ? tr("Closed without its burn at height %1 (before the vault upgrade); the %2 minted against it are unbacked.").arg(p.closeHeight).arg(YellowbackFormat::cents(p.mintedCents))
            : tr("Closed at height %1 after burning %2.").arg(p.closeHeight).arg(YellowbackFormat::cents(p.burnedCents));
    } else if (p.status == STATUS_CLAIMED) {
        a.text = tr("Claimed by someone else (closed at height %1): the vault was underwater past its claim height, they burned %2 and released the collateral after the claim delay.")
                    .arg(p.closeHeight).arg(YellowbackFormat::cents(p.burnedCents));
    } else {
        a.text = p.status;
    }
    return a;
}

void YellowbackTab::setupPositions() {
    uiPositions->tblPositions->horizontalHeader()->setStretchLastSection(true);
    uiPositions->btnWhyVoid->setEnabled(false);
    uiPositions->btnRelease->setEnabled(false);
    uiPositions->btnRedeem->setEnabled(false);
    uiPositions->btnRenew->setEnabled(false);
    uiPositions->btnRenew->setToolTip(tr("Redeem the vault and mint its YED again in a new vault with a fresh lock and claim height: one confirmation, two transactions (yed_redeem, then yed_mint)."));
    uiPositions->btnRelease->setToolTip(tr("Return a VOID vault's collateral: no YED burned, no fee (yed_redeem)."));
    uiPositions->btnRedeem->setToolTip(tr("Burn the vault's YED and take the collateral back, minus the pool fee (yed_redeem)."));
    uiPositions->btnNotice->setEnabled(false);
    uiPositions->btnNotice->setToolTip(tr("Post a claim notice against a vault below the emergency ratio under the attested prices (yed_claimnotice). Anyone may; it costs the carrier and a network fee, no YED."));

    // "Show": open vaults by default -- the owner's list was mostly CLAIMED and CLOSED rows
    uiPositions->cmbPositionsFilter->addItem(tr("Open (active, claiming and void)"), "open");
    uiPositions->cmbPositionsFilter->addItem(tr("All"), "");
    for (const char* st : { "ACTIVE", "CLAIMING", "CLOSED", "CLAIMED", "VOID" }) uiPositions->cmbPositionsFilter->addItem(QString(st), QString(st));
    QObject::connect(uiPositions->cmbPositionsFilter, &QComboBox::currentIndexChanged, this, [=, this](int) {
        if (positionsFilter) positionsFilter->setStatusFilter(uiPositions->cmbPositionsFilter->currentData().toString());
        updateVaultButtons();
    });

    auto selected = [=, this]() -> const YellowbackPosition* { return selectedPosition(); };
    QObject::connect(uiPositions->btnRelease, &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) redeemVault(*p); });
    QObject::connect(uiPositions->btnRedeem,  &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) redeemVault(*p); });
    QObject::connect(uiPositions->btnRenew,   &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) renewVault(*p); });
    QObject::connect(uiPositions->btnNotice,  &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) noticeVault(*p); });
    QObject::connect(uiPositions->btnWhyVoid, &QPushButton::clicked, [=, this]() {
        auto p = selectedPosition();
        if (p != nullptr) explainVoid(*p);
    });
}

const YellowbackPosition* YellowbackTab::selectedPosition() const {
    if (ctl == nullptr) return nullptr;
    QModelIndex idx = uiPositions->tblPositions->currentIndex();
    if (!idx.isValid()) return nullptr;
    if (positionsFilter != nullptr) idx = positionsFilter->mapToSource(idx);
    return ctl->positionsModel()->positionAt(idx.isValid() ? idx.row() : -1);
}

void YellowbackTab::updatePositions() {
    if (ctl == nullptr) return;
    uiPositions->tblPositions->resizeColumnsToContents();

    // Selection-dependent buttons are re-evaluated whenever the selection changes
    auto sel = uiPositions->tblPositions->selectionModel();
    if (sel != nullptr) {
        QObject::disconnect(sel, nullptr, this, nullptr);
        QObject::connect(sel, &QItemSelectionModel::currentRowChanged, this, [=, this](const QModelIndex&, const QModelIndex&) {
            updateVaultButtons();
        });
    }
    updateVaultButtons();
}

void YellowbackTab::updateVaultButtons() {
    if (ctl == nullptr) return;
    auto p = selectedPosition();
    if (p == nullptr) {
        const int shown = uiPositions->tblPositions->model() ? uiPositions->tblPositions->model()->rowCount(QModelIndex()) : 0;
        const int owned = ctl->positionsModel()->rowCount(QModelIndex());
        uiPositions->lblVaultAction->setText(owned == 0 ? tr("You own no vaults.")
                                           : shown == 0 ? tr("No vault matches the filter; %1 in all (\"Show: All\").").arg(owned)
                                           : tr("Select a vault to see what can be done with it."));
        uiPositions->btnWhyVoid->setEnabled(false);
        uiPositions->btnNotice->setVisible(false);
        uiPositions->btnNotice->setEnabled(false);
        uiPositions->btnRenew->setVisible(false);
        uiPositions->btnRenew->setEnabled(false);
        return;
    }
    VaultActions a = vaultActions(*p, ctl->height());
    // H-9.2: renew needs a mint now; when the gate is shut the row says why and offers redeem only
    QString renewBlocker;
    if (a.renew) {
        const int lockBlocks = ctl->renewLockBlocks(*p);
        renewBlocker = lockBlocks <= 0 ? tr("no term class is enabled") : ctl->mintBlocker(p->mintedCents, ctl->classForLock(lockBlocks).name);
        if (!renewBlocker.isEmpty()) a.text += " " % tr("Renew is not possible now: %1").arg(renewBlocker);
        if (renew.active && renew.vaultTxid == p->txid) a.text += " " % tr("A renewal of this vault is in progress.");
    }
    // H-9.3: heights that fail the identities are named, not hidden
    const QStringList bad = YellowbackController::checkVaultHeights(ctl->params(), *p);
    if (!bad.isEmpty()) a.text += " " % tr("Warning: the node's heights for this vault are inconsistent (%1).").arg(bad.join("; "));
    uiPositions->lblVaultAction->setText(a.text);
    uiPositions->btnRenew->setVisible(a.renew);
    uiPositions->btnRenew->setEnabled(actionsEnabled && a.renew && renewBlocker.isEmpty() && !renew.active);
    uiPositions->btnWhyVoid->setEnabled(actionsEnabled && p->status == YellowbackRpc::Position::STATUS_VOID);
    uiPositions->btnRelease->setVisible(a.release);
    uiPositions->btnRedeem->setVisible(a.redeem || !a.release);
    uiPositions->btnRelease->setEnabled(actionsEnabled && a.release);
    uiPositions->btnRedeem->setEnabled(actionsEnabled && a.redeem);
    // v3: the notice is offered where the node says it could be posted, and not once one stands
    bool canNotice = p->canNotice && !p->noticed && p->status == YellowbackRpc::Position::STATUS_ACTIVE;
    uiPositions->btnNotice->setVisible(canNotice);
    uiPositions->btnNotice->setEnabled(actionsEnabled && canNotice);
}

// ── Redeem / Release dialogs (one confirmation, one call; V24, L14) ───────────────────────

// H4: a REDEEM or CLAIM whose only workable selection leaves change under the output floor burns
// that sub-dollar remainder with the debt; the node reports it and the dialog says so.
QString YellowbackTab::extraBurnLine(const nlohmann::json& r) {
    qint64 extra = YellowbackJson::toInt(r, YellowbackRpc::RedeemResult::EXTRA_BURN_CENTS);
    return extra > 0 ? tr(" (plus %1 burned as a remainder too small to pay back as change)").arg(YellowbackFormat::cents(extra))
                     : QString();
}

void YellowbackTab::redeemVault(const YellowbackPosition& p, const QString& to) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const bool isVoid = p.status == Position::STATUS_VOID;
    const QString what = isVoid ? tr("Release") : tr("Redeem");
    const QString dest = to.isEmpty() ? tr("a fresh transparent address of this wallet") : to;
    if (ctl->height() < p.lockHeight) {
        notice(what, tr("The vault is locked until height %1 (the chain is at %2). Every Ycash node enforces that lock.").arg(p.lockHeight).arg(ctl->height()));
        return;
    }
    if (!isVoid && p.mintedCents > ctl->confirmedCents()) {
        notice(what, tr("Redeeming burns %1 of YED but only %2 is confirmed in this wallet.")
            .arg(YellowbackFormat::cents(p.mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())), true);
        return;
    }

    auto run = [=, this](const QString& text) {
        if (!confirm(tr("Confirm %1").arg(what.toLower()), text)) return;
        ctl->redeem(p.txid, to,
            [=, this](const json& r) {
                QString payee = YellowbackJson::isNull(r, RedeemResult::PAYEE) ? tr("none") : YellowbackJson::toStr(r, RedeemResult::PAYEE);
                notice(tr("%1 sent").arg(what),
                    tr("txid %1\nYED burned: %2%7\npool fee: %3 to %4\ncollateral out: %5 to %6\n\nThe vault closes when the transaction is mined.")
                        .arg(YellowbackJson::toStr(r, RedeemResult::TXID))
                        .arg(YellowbackFormat::cents(YellowbackJson::toInt(r, RedeemResult::BURNED_CENTS)))
                        .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RedeemResult::FEE_ZAT))).arg(payee)
                        .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RedeemResult::COLLATERAL_OUT)))
                        .arg(YellowbackJson::toStr(r, RedeemResult::TO))
                        .arg(extraBurnLine(r)));
                ctl->refresh(true);
            },
            [=, this](const QString& e) { failed("yed_redeem", e); });
    };

    if (isVoid) {
        run(tr("Release the collateral of VOID vault %1.\n\n"
               "No YED is burned and no fee is paid: this vault never carried a debt (its mint was recorded as %2 before the vault upgrade). "
               "The full %3 of YEC returns to %4.\n\n"
               "Do it before height %5.")
            .arg(p.txid).arg(p.voidReason.isEmpty() ? tr("void") : p.voidReason)
            .arg(YellowbackFormat::zec(p.collateralZat)).arg(dest).arg(p.claimHeight));
        return;
    }

    auto ask = [=, this](qint64 feeZat, const QString& feeLine) {
        run(tr("Redeem vault %1.\n\n"
               "Burn: %2 of YED from this wallet (%3 confirmed).\n"
               "%4\n"
               "Collateral out: about %5 of YEC to %6.\n\n"
               "Your own node builds the transaction, checks it against the Yellowback consensus rules, signs it and sends it; "
               "nothing is committed if that check fails.")
            .arg(p.txid).arg(YellowbackFormat::cents(p.mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents()))
            .arg(feeLine).arg(YellowbackFormat::zec(p.collateralZat - feeZat)).arg(dest));
    };
    ctl->getFeePayee(ctl->refHeightNow(), p.collateralZat,
        [=, this](const json& f) {
            const json& def = YellowbackJson::obj(f, FeePayee::DEFAULT);
            qint64 fee = YellowbackJson::toInt(f, FeePayee::FEE_ZAT);
            QString payee = YellowbackJson::has(f, FeePayee::PREFERRED) && !YellowbackJson::isNull(f, FeePayee::PREFERRED)
                ? YellowbackJson::toStr(f, FeePayee::PREFERRED) : YellowbackJson::toStr(def, FeePayeeDefault::PAYOUT_ADDRESS);
            // H-9.3: FEE-1 recomputed locally (the node pays exactly FEE-1; a different figure is a bad reply)
            const qint64 local = YellowbackController::feeZatFor(ctl->params(), p.collateralZat);
            if (fee != local) {
                notice(tr("Redeem not sent"), implausibleText(tr("redemption"), { tr("the pool fee is %1 where FEE-1 of the collateral %2 gives %3")
                    .arg(YellowbackFormat::zec(fee)).arg(YellowbackFormat::zec(p.collateralZat)).arg(YellowbackFormat::zec(local)) }), true);
                return;
            }
            ask(fee, tr("Pool fee: %1 of YEC from the collateral to the pool %2.").arg(YellowbackFormat::zec(fee)).arg(payee));
        },
        [=, this](const QString& e) {
            if (e.startsWith(Errors::FEE_NO_ELIGIBLE_PAYEE, Qt::CaseInsensitive))
                ask(0, tr("Pool fee: none (no pool published a price quote in the payee window)."));
            else
                ask(0, tr("Pool fee: could not be estimated (%1); the node reports it after the redemption.").arg(e));
        });
}

// v3: yed_claimnotice, step 1 of the emergency claim (NOT-1). Two-step like Mint; the vault
// row then shows emergencyOpenAt once the notice confirms.
// ── Renew (hardening H-9.2): redeem and re-mint in one flow ───────────────────────────────
// The redeem goes out at once; the mint waits for the redeem to close the vault (the collateral
// it pays back funds the new vault) and is held to the cap the one confirmation showed.

void YellowbackTab::renewVault(const YellowbackPosition& p) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const QString what = tr("Renew");
    if (renew.active) { notice(what, tr("A renewal is already in progress (vault %1).").arg(renew.vaultTxid), true); return; }
    if (p.status != Position::STATUS_ACTIVE) { notice(what, tr("Only an ACTIVE vault can be renewed."), true); return; }
    if (ctl->height() < p.lockHeight) {
        notice(what, tr("The vault is locked until height %1 (the chain is at %2). Every Ycash node enforces that lock.").arg(p.lockHeight).arg(ctl->height()));
        return;
    }
    if (p.mintedCents > ctl->confirmedCents()) {
        notice(what, tr("Renewing first redeems the vault, which burns %1 of YED, but only %2 is confirmed in this wallet.")
            .arg(YellowbackFormat::cents(p.mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())), true);
        return;
    }
    const int lockBlocks = ctl->renewLockBlocks(p);
    const QString cls = ctl->classForLock(lockBlocks).name;
    const QString blocker = lockBlocks <= 0 ? tr("no term class is enabled on this network.") : ctl->mintBlocker(p.mintedCents, cls);
    if (!blocker.isEmpty()) {
        notice(what, tr("Renew is not possible now, because a new vault cannot be minted: %1\n\nRedeem the vault instead if its claim height is near.").arg(blocker), true);
        return;
    }
    const qint64 cents = p.mintedCents;

    ctl->estimateCollateral(cents, lockBlocks,
        [=, this](const json& e) {
            if (YellowbackJson::isNull(e, Estimate::REQUIRED_ZAT)) {
                notice(what, tr("No estimate: the mint price is undefined at the reference height, so the new vault cannot be sized. Redeem instead, or try again later."), true);
                return;
            }
            const QStringList bad = YellowbackController::checkEstimate(ctl->params(), ctl->height(), cents, lockBlocks, e);
            if (!bad.isEmpty()) { notice(tr("Renew not sent"), implausibleText(tr("renewal's new mint"), bad), true); return; }
            const qint64 collateral = YellowbackController::mintCollateralZatFor(ctl->params(), YellowbackJson::toInt(e, Estimate::REQUIRED_ZAT));
            const qint64 maxCollateral = capAbove(collateral);
            const int newLock  = (int)YellowbackJson::toInt(e, Estimate::LOCK_HEIGHT);
            const int newClaim = (int)YellowbackJson::toInt(e, Estimate::CLAIM_HEIGHT);
            const QString ratio = YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, Estimate::MIN_RATIO_BPS));
            const QString price = YellowbackFormat::priceOrUndefined(e, Estimate::P_MINT);

            auto ask = [=, this](qint64 redeemFee, const QString& feeLine) {
                const QString text = tr("Renew vault %1: redeem it, then mint %2 of YED again in a new vault.\n\n"
                                        "1. Redeem now: burn %2 of YED from this wallet (%3 confirmed). %4 "
                                        "Collateral out: about %5 to a fresh transparent address of this wallet.\n\n"
                                        "2. Mint, once the redeem is mined: %2 of YED against %6 (at most %7; the node refuses the mint if the price moves further), "
                                        "lock %8 blocks (class %9). Lock height about %10 (~%11), claim height about %12 (~%13). "
                                        "Collateral ratio %14 at a mint price of %15 per YEC. The new vault is funded from your transparent YEC, "
                                        "which the redeem pays back into, and pays its own pool fee.\n\n"
                                        "These are two transactions. If the mint cannot be built when the redeem confirms (minting paused, or the price moved past the cap), "
                                        "the vault stays redeemed, nothing else is sent, and the wallet tells you. "
                                        "Back up wallet.dat after the renewal: the new vault's key exists only there.")
                    .arg(p.txid).arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::cents(ctl->confirmedCents())).arg(feeLine)
                    .arg(YellowbackFormat::zec(p.collateralZat - redeemFee)).arg(YellowbackFormat::zec(collateral)).arg(YellowbackFormat::zec(maxCollateral))
                    .arg(lockBlocks).arg(cls)
                    .arg(newLock).arg(YellowbackFormat::estimateDate(newLock, ctl->height()).toString("yyyy-MM-dd"))
                    .arg(newClaim).arg(YellowbackFormat::estimateDate(newClaim, ctl->height()).toString("yyyy-MM-dd"))
                    .arg(ratio).arg(price);
                if (!confirm(tr("Confirm renew"), text)) return;
                uiPositions->lblVaultAction->setText(tr("Renew: redeeming..."));
                ctl->redeem(p.txid, QString(),
                    [=, this](const json& r) {
                        renew = RenewState();
                        renew.active = true;
                        renew.vaultTxid = p.txid;
                        renew.redeemTxid = YellowbackJson::toStr(r, RedeemResult::TXID);
                        renew.cents = cents;
                        renew.lockBlocks = lockBlocks;
                        renew.maxCollateralZat = maxCollateral;
                        renew.shownCollateralZat = collateral;
                        uiPositions->lblVaultAction->setText(tr("Renew: redeem %1 sent; the new mint follows when it is mined.").arg(renew.redeemTxid));
                        ctl->refresh(true);
                    },
                    [=, this](const QString& err) { updateVaultButtons(); failed("yed_redeem", err); });
            };
            ctl->getFeePayee(ctl->refHeightNow(), p.collateralZat,
                [=, this](const json& f) {
                    const qint64 fee = YellowbackJson::toInt(f, FeePayee::FEE_ZAT);
                    const qint64 local = YellowbackController::feeZatFor(ctl->params(), p.collateralZat);
                    if (fee != local) {
                        notice(tr("Renew not sent"), implausibleText(tr("renewal's redemption"), { tr("the pool fee is %1 where FEE-1 of the collateral %2 gives %3")
                            .arg(YellowbackFormat::zec(fee)).arg(YellowbackFormat::zec(p.collateralZat)).arg(YellowbackFormat::zec(local)) }), true);
                        return;
                    }
                    ask(fee, tr("Pool fee: %1 from the collateral.").arg(YellowbackFormat::zec(fee)));
                },
                [=, this](const QString& err) {
                    if (err.startsWith(Errors::FEE_NO_ELIGIBLE_PAYEE, Qt::CaseInsensitive)) ask(0, tr("Pool fee: none (no pool published a price quote in the payee window)."));
                    else ask(0, tr("Pool fee: could not be estimated (%1).").arg(err));
                });
        },
        [=, this](const QString& err) { failed("yed_estimatecollateral", err); });
}

void YellowbackTab::continueRenew() {
    if (ctl == nullptr || !renew.active || renew.minting) return;
    using namespace YellowbackRpc;
    const YellowbackPosition* p = nullptr;
    for (int i = 0; ; i++) {
        const YellowbackPosition* q = ctl->positionsModel()->positionAt(i);
        if (q == nullptr) break;
        if (q->txid == renew.vaultTxid) { p = q; break; }
    }
    if (p == nullptr || p->status == Position::STATUS_ACTIVE) return;     // the redeem is not mined yet
    const RenewState r = renew;
    auto stop = [=, this](const QString& why) {
        renew = RenewState();
        updateVaultButtons();
        notice(tr("Renew stopped"), tr("Vault %1 is redeemed (txid %2), but the new mint was not sent: %3\n\nMint again from the Mint page when it clears.")
            .arg(r.vaultTxid).arg(r.redeemTxid).arg(why), true);
    };
    if (p->status != Position::STATUS_CLOSED) {
        renew = RenewState();
        updateVaultButtons();
        notice(tr("Renew stopped"), tr("Vault %1 ended %2 instead of being redeemed by this wallet; no mint was sent.").arg(r.vaultTxid).arg(p->status), true);
        return;
    }
    renew.minting = true;
    const QString cls = ctl->classForLock(r.lockBlocks).name;
    const QString blocker = ctl->mintBlocker(r.cents, cls);
    if (!blocker.isEmpty()) { stop(blocker); return; }
    uiPositions->lblVaultAction->setText(tr("Renew: the redeem is mined; minting %1 again...").arg(YellowbackFormat::cents(r.cents)));
    ctl->estimateCollateral(r.cents, r.lockBlocks,
        [=, this](const json& e) {
            if (YellowbackJson::isNull(e, Estimate::REQUIRED_ZAT)) { stop(tr("the mint price is undefined at the reference height.")); return; }
            const QStringList bad = YellowbackController::checkEstimate(ctl->params(), ctl->height(), r.cents, r.lockBlocks, e);
            if (!bad.isEmpty()) { stop(tr("the node's estimate failed the wallet's check (%1).").arg(bad.join("; "))); return; }
            const qint64 collateral = YellowbackController::mintCollateralZatFor(ctl->params(), YellowbackJson::toInt(e, Estimate::REQUIRED_ZAT));
            if (collateral > r.maxCollateralZat) {
                stop(tr("the new vault now needs %1, above the %2 you confirmed.").arg(YellowbackFormat::zec(collateral)).arg(YellowbackFormat::zec(r.maxCollateralZat)));
                return;
            }
            ctl->mint(r.cents, r.lockBlocks, QString(), r.maxCollateralZat,
                [=, this](const json& m) {
                    Settings::getInstance()->setYellowbackBackupPending(true);
                    updateBackupNag();
                    auto done = [=, this](const json& info) {
                        renew = RenewState();
                        updateVaultButtons();
                        notice(tr("Vault renewed"), tr("Vault %1 redeemed (txid %2).\n\n").arg(r.vaultTxid).arg(r.redeemTxid) %
                               mintSummary(r.cents, info, r.shownCollateralZat) % "\n\n" % tr("Back up wallet.dat now."));
                        ctl->refresh(true);
                    };
                    if (!YellowbackJson::toBool(m, MintResult::PENDING)) { done(m); return; }
                    uiPositions->lblVaultAction->setText(tr("Renew: preparing price proof (1 block): carrier %1 sent; the mint follows when it confirms.")
                        .arg(YellowbackJson::toStr(m, MintResult::CARRIER_TXID)));
                    ctl->awaitPending(Transaction::TYPE_MINT, YellowbackJson::toStr(m, MintResult::CARRIER_TXID),
                        (int)YellowbackJson::toInt(m, MintResult::REF_HEIGHT, ctl->refHeightNow()), done,
                        [=, this](const QString& err) { stop(err); });
                },
                [=, this](const QString& err) { stop(tr("yed_mint failed: %1").arg(err) % [&]() { QString x = YellowbackController::explainError(err); return x.isEmpty() ? QString() : QString("\n\n") % x; }()); });
        },
        [=, this](const QString& err) { stop(tr("yed_estimatecollateral failed: %1").arg(err)); });
}

void YellowbackTab::noticeVault(const YellowbackPosition& p) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    if (p.noticed) {
        notice(tr("Claim notice"), tr("A claim notice already stands against vault %1 (confirmed at height %2); a second one cannot reset its clock.").arg(p.txid).arg(p.noticeHeight));
        return;
    }
    int persist = (int)YellowbackJson::toInt(YellowbackJson::obj(ctl->params(), Params::ATTEST), ParamsAttest::EMERGENCY_PERSIST);
    QString text = tr("Post a claim notice against vault %1 (owner %2, %3 minted against %4 of YEC).\n\n"
                      "Under the attested prices this node holds, the vault is below the emergency ratio. A notice records that on chain; "
                      "if it still holds %5 blocks after the notice's reference height, anyone may claim the vault under the emergency clause "
                      "even though the combined price has not put it underwater. The owner can redeem in the meantime.\n\n"
                      "Cost: the carrier and a network fee from your YEC; no YED is burned by a notice. "
                      "Your own node builds it in two steps: the carrier now, the notice when the carrier confirms.")
        .arg(p.txid).arg(p.ownerAddress).arg(YellowbackFormat::cents(p.mintedCents)).arg(YellowbackFormat::zec(p.collateralZat)).arg(persist);
    if (!confirm(tr("Confirm claim notice"), text)) return;
    uiPositions->lblVaultAction->setText(tr("Posting the claim notice..."));
    ctl->claimNotice(p.txid,
        [=, this](const json& r) {
            const int openAt = (int)YellowbackJson::toInt(r, NoticeResult::EMERGENCY_OPEN_AT);
            followPending(Transaction::TYPE_NOTICE, r, uiPositions->lblVaultAction, [=, this](const json& done) {
                int at = openAt > 0 ? openAt : (int)YellowbackJson::toInt(r, NoticeResult::REF_HEIGHT) + persist;
                notice(tr("Claim notice sent"),
                    tr("txid %1\nvault %2\n%3\n\nThe Vaults page shows the notice on the row once it confirms.")
                        .arg(YellowbackJson::toStr(done, NoticeResult::TXID)).arg(p.vaultName())
                        .arg(at > 0 ? tr("emergency claim possible from reference height %1 on, while the vault stays below the emergency ratio").arg(at)
                                    : tr("the emergency clause opens once the notice has persisted")));
                ctl->refresh(true);
            }, p.txid);
        },
        [=, this](const QString& e) {
            updateVaultButtons();
            if (bundleInsufficientRetry(tr("The claim notice"), e)) return;
            failed("yed_claimnotice", e);
        });
}

void YellowbackTab::explainVoid(const YellowbackPosition& p) {
    if (ctl == nullptr) return;
    const QString vault = p.txid;
    const QString known = p.voidReason;
    ctl->getTxInfo(vault,
        [=, this](const json& info) {
            QString verdict = YellowbackJson::toStr(info, YellowbackRpc::Transaction::VERDICT, known.isEmpty() ? tr("(no verdict returned)") : known);
            QMessageBox::information(this, tr("Why this vault is VOID"),
                tr("The Yellowback index recorded the mint %1 with verdict:\n\n%2\n\n%3")
                   .arg(vault).arg(verdict).arg(YellowbackFormat::voidReason(verdict)));
        },
        [=, this](const QString& e) {
            QMessageBox::information(this, tr("Why this vault is VOID"),
                tr("%1\n\n(yed_gettxinfo failed: %2)").arg(YellowbackFormat::voidReason(known.isEmpty() ? tr("(unknown)") : known)).arg(e));
        });
}

// ── Claim ─────────────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupClaim() {
    uiClaim->tblClaimable->horizontalHeader()->setStretchLastSection(true);
    uiClaim->btnClaim->setEnabled(false);
    uiClaim->btnClaim->setToolTip(tr("Burn the vault's debt from your YED and move its collateral into a claim intent you release after the claim delay (yed_claim)."));
    QObject::connect(uiClaim->btnClaim, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        auto idx = uiClaim->tblClaimable->currentIndex();
        auto c = ctl->claimableModel()->rowAt(idx.isValid() ? idx.row() : -1);
        if (c != nullptr) claimVault(*c);
    });
}

void YellowbackTab::updateClaimPage() {
    if (ctl == nullptr) return;
    uiClaim->tblClaimable->resizeColumnsToContents();
    auto sel = uiClaim->tblClaimable->selectionModel();
    if (sel != nullptr) {
        QObject::disconnect(sel, nullptr, this, nullptr);
        QObject::connect(sel, &QItemSelectionModel::currentRowChanged, this, [=, this](const QModelIndex& cur, const QModelIndex&) {
            uiClaim->btnClaim->setEnabled(actionsEnabled && ctl->claimableModel()->rowAt(cur.isValid() ? cur.row() : -1) != nullptr);
        });
    }
    int n = ctl->claimableModel()->rowCount(QModelIndex());
    auto idx = uiClaim->tblClaimable->currentIndex();
    uiClaim->btnClaim->setEnabled(actionsEnabled && ctl->claimableModel()->rowAt(idx.isValid() ? idx.row() : -1) != nullptr);
    if (n == 0) {
        uiClaim->lblClaimHint->setText(tr("No vault is claimable at the current claim price."));
        return;
    }
    uiClaim->lblClaimHint->setText(tr("%n claimable vault(s). A claim burns the vault's debt from your confirmed YED (%1 available), pays the fees from your YEC and "
                                      "moves the collateral, less the owner's residual, into a claim intent you release after %2 blocks (Pending claims page). "
                                      "Select a row and press Claim.", "", n)
                                       .arg(YellowbackFormat::cents(ctl->confirmedCents())).arg(ctl->claimDelay()));
}

// v3: which clause opened the claim and what the claim must give back (RED-5), from the
// yed_listclaimable row; "" for claimPath means the node could build no bundle for it.
QString YellowbackTab::describeClaimPath(const YellowbackClaimable& c) {
    using namespace YellowbackRpc::ClaimResult;
    QString out;
    if (c.claimPath == PATH_B)
        out = tr("Claim path: emergency clause (b) — a notice against this vault (height %1) has persisted and it is still below the emergency ratio. "
                 "Under this clause you keep exactly the debt's worth at the claim price, no margin.").arg(c.noticeHeight);
    else if (c.claimPath == PATH_A)
        out = tr("Claim path: underwater (a) at the combined claim price.");
    else
        out = tr("Claim path: none yet — this node cannot build the price proof (too few fresh attestations in its pool), so the node will refuse the claim until the subscriber refills it.");
    if (c.residualZat > 0)
        out += " " % tr("Residual: %1 of YEC goes to the vault owner in its own intent (the collateral above the debt's worth); it is not yours.").arg(YellowbackFormat::zec(c.residualZat));
    else
        out += " " % tr("Residual to the owner: none.");
    if (c.attestFeeZat > 0)
        out += " " % tr("Attestation fee: %1 of YEC from your own YEC to an attestor.").arg(YellowbackFormat::zec(c.attestFeeZat));
    return out;
}

void YellowbackTab::claimVault(const YellowbackClaimable& c, const QString& to) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const QString dest = to.isEmpty() ? tr("a fresh transparent address of this wallet") : to;
    if (c.mintedCents > ctl->confirmedCents()) {
        notice(tr("Claim"), tr("A claim burns %1 of YED but only %2 is confirmed in this wallet.")
            .arg(YellowbackFormat::cents(c.mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())), true);
        return;
    }
    // D-U1: a claim intent commits a transparent recipient script; the node refuses a Sapling one (bad-address)
    if (!to.isEmpty() && !Settings::isTAddress(to)) {
        notice(tr("Claim"), tr("A claim pays a claim intent, whose recipient must be a transparent address of this wallet; %1 is not one. "
                               "Claim to a transparent address and shield the YEC once it is released.").arg(to), true);
        return;
    }
    QString txid = c.txid();
    // H-9.3: the row's fee, attestor fee and claim height recomputed from the parameters
    const QStringList bad = YellowbackController::checkClaimable(ctl->params(), ctl->height(), c);
    if (!bad.isEmpty()) {
        notice(tr("Claim not sent"), implausibleText(tr("claim"), bad), true);
        return;
    }
    // rpcversion 5 (U-23, finding 33): the claimant intent carries the collateral less the RED-5
    // residual; the enforcement and attestation fees are paid from the claimant's own YEC
    const qint64 youGet = c.collateralZat - c.residualZat;
    // The floor the node is held to (audit F-1): 1 % under the row's figure; a claim whose intent
    // would carry less is refused (claim-out-below-min) rather than sent.
    const qint64 minOut = capBelow(youGet);
    // maxBurnCents (rpcversion 4, H-9.3): the debt the row shows, plus at most the sub-dollar
    // remainder H4 may burn with it (below MIN_OUTPUT); the node refuses claim-burn-above-max beyond
    const qint64 maxBurn = c.mintedCents + ctl->minOutputCents() - 1;
    const int delay = ctl->claimDelay();
    QString text = tr("Claim vault %1 (owner %2).\n\n"
                      "Burn: %3 of YED from this wallet (%4 confirmed), at most %14.\n"
                      "Fees from your own YEC: the pool fee %5 to a pool that published a price quote%15, and the network fee.\n"
                      "Claim intent: about %6 of YEC (collateral %7 less the owner's residual) for %8, at least %13 (the node refuses the claim if the price moves further).\n"
                      "%12\n\n"
                      "The YEC is not paid at once: it waits in a claim intent for %16 blocks, then you release it on the Pending claims page. "
                      "Until then any member of the YED attestor set may cancel the claim if it was made at a wrong price. "
                      "A cancel returns the collateral to the vault and does NOT refund the %3 of YED you burn: claim only at a price you are sure of.\n\n"
                      "The vault is past its claim height (%9) and underwater at the claim price of %10 per YEC (underwater below %11). "
                      "Your own node builds the claim in two steps (the carrier with the price proof now, the claim when it confirms) "
                      "and checks it against the Yellowback consensus rules before it signs and sends it.")
        .arg(txid).arg(c.ownerAddress).arg(YellowbackFormat::cents(c.mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents()))
        .arg(YellowbackFormat::zec(c.feeZat)).arg(YellowbackFormat::zec(youGet)).arg(YellowbackFormat::zec(c.collateralZat)).arg(dest)
        .arg(c.claimHeight).arg(YellowbackFormat::price(c.pClaim)).arg(YellowbackFormat::price(c.underwaterAt))
        .arg(describeClaimPath(c)).arg(YellowbackFormat::zec(minOut))
        .arg(YellowbackFormat::cents(maxBurn))
        .arg(c.attestFeeZat > 0 ? tr(" and the attestation fee %1").arg(YellowbackFormat::zec(c.attestFeeZat)) : QString())
        .arg(delay);
    if (!confirm(tr("Confirm claim"), text)) return;
    uiClaim->lblClaimHint->setText(tr("Claiming..."));
    ctl->claim(txid, to, minOut, maxBurn,
        [=, this](const json& r) {
            followPending(Transaction::TYPE_CLAIM, r, uiClaim->lblClaimHint, [=, this](const json& done) {
                notice(tr("Claim sent"), claimSummary(done));
                ctl->refresh(true);
            });
        },
        [=, this](const QString& e) {
            updateClaimPage();
            if (bundleInsufficientRetry(tr("The claim"), e)) return;
            failed("yed_claim", e);
        });
}

// ── Pending claims (rpcversion 5) ─────────────────────────────────────────────────────────
// Every CLAIMING vault's intents (yed_listvaults "CLAIMING"). A claimant intent is released to
// the claimant from releaseHeight on (vault_release finds the recipient among this wallet's keys);
// the owner's residual intent may be released by anyone, given the owner's P2PKH script. Before
// releaseHeight a current member of the YED attestor set may cancel a claimant intent: the
// collateral returns to the vault (ACTIVE again under the cancel's txid) and the burn is lost.

void YellowbackTab::setupPendingClaims() {
    QVBoxLayout* layout = new QVBoxLayout(pages[PendingClaims]);
    lblPendingIntro = new QLabel(pages[PendingClaims]);
    lblPendingIntro->setObjectName("lblPendingIntro");
    lblPendingIntro->setWordWrap(true);
    lblPendingIntro->setText(tr("Claims waiting out the claim delay. A claim burns the vault's debt and moves its collateral into a claim intent: "
                                "from its release height the claimant releases it (Release), and the vault is CLAIMED. Before then any current member "
                                "of the YED attestor set may cancel a claim made at a wrong price: the collateral returns to the vault, which is ACTIVE again, "
                                "and the claimant's burn is not refunded. The owner's residual, when there is one, waits in its own intent and is released to the owner."));
    layout->addWidget(lblPendingIntro);
    tblPendingClaims = new QTableView(pages[PendingClaims]);
    tblPendingClaims->setObjectName("tblPendingClaims");
    tblPendingClaims->setSelectionMode(QAbstractItemView::SingleSelection);
    tblPendingClaims->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblPendingClaims->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(tblPendingClaims);
    lblPendingAction = new QLabel(pages[PendingClaims]);
    lblPendingAction->setObjectName("lblPendingAction");
    lblPendingAction->setWordWrap(true);
    lblPendingAction->setStyleSheet("QLabel { color: #444; }");
    layout->addWidget(lblPendingAction);
    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    btnReleaseClaim = new QPushButton(tr("Release…"), pages[PendingClaims]);
    btnReleaseClaim->setObjectName("btnReleaseClaim");
    btnReleaseClaim->setToolTip(tr("Pay a matured claim intent to its recipient (vault_release); the network fee comes from your YEC."));
    btnCancelClaim = new QPushButton(tr("Cancel wrong-price claim…"), pages[PendingClaims]);
    btnCancelClaim->setObjectName("btnCancelClaim");
    btnCancelClaim->setToolTip(tr("Attestors only: stop a claim made at a wrong price before it matures (vault_buildcancel, set_signcancel, vault_send). "
                                  "The collateral returns to the vault and the claimant's burn is lost."));
    buttons->addWidget(btnReleaseClaim);
    buttons->addWidget(btnCancelClaim);
    layout->addLayout(buttons);
    lblCancelledClaims = new QLabel(pages[PendingClaims]);
    lblCancelledClaims->setObjectName("lblCancelledClaims");
    lblCancelledClaims->setWordWrap(true);
    lblCancelledClaims->setStyleSheet("QLabel { color: #8a1f1f; }");
    lblCancelledClaims->setVisible(false);
    layout->addWidget(lblCancelledClaims);
    btnReleaseClaim->setEnabled(false);
    btnCancelClaim->setEnabled(false);
    btnCancelClaim->setVisible(false);
    QObject::connect(btnReleaseClaim, &QPushButton::clicked, [=, this]() { auto c = selectedPendingClaim(); if (c) releaseClaim(*c); });
    QObject::connect(btnCancelClaim,  &QPushButton::clicked, [=, this]() { auto c = selectedPendingClaim(); if (c) cancelClaim(*c); });
}

const YellowbackPendingClaim* YellowbackTab::selectedPendingClaim() const {
    if (ctl == nullptr || tblPendingClaims == nullptr) return nullptr;
    QModelIndex idx = tblPendingClaims->currentIndex();
    return ctl->pendingClaimsModel()->rowAt(idx.isValid() ? idx.row() : -1);
}

QString YellowbackTab::describeCancelledClaims(const QList<YellowbackClaimOutcome>& outcomes) {
    QStringList lines;
    for (const auto& o : outcomes) {
        if (o.outcome != "cancelled") continue;
        lines << tr("Your claim %1 (height %2) of vault %3 was cancelled by the attestor set: the %4 of YED it burned are not refunded, "
                    "and the collateral went back to the vault.")
                     .arg(o.claimTxid.left(16) % "…").arg(o.height).arg(o.vaultTxid.left(16) % "…").arg(YellowbackFormat::cents(o.burnedCents));
    }
    if (lines.isEmpty()) return QString();
    return lines.join("\n") % "\n" %
           tr("A cancel means at least one member of the attestor set judged the claim to be made at a wrong price. The burn is lost by design "
              "(it is the price of a wrong-price claim); it lowers the YED supply while the vault keeps its debt, so the system only becomes more collateralised.");
}

void YellowbackTab::updatePendingClaims() {
    if (ctl == nullptr || tblPendingClaims == nullptr) return;
    using namespace YellowbackRpc::PositionIntent;
    tblPendingClaims->resizeColumnsToContents();
    auto sel = tblPendingClaims->selectionModel();
    if (sel != nullptr) {
        QObject::disconnect(sel, nullptr, this, nullptr);
        QObject::connect(sel, &QItemSelectionModel::currentRowChanged, this, [=, this](const QModelIndex&, const QModelIndex&) { updatePendingClaims(); });
    }
    const QString cancelled = describeCancelledClaims(ctl->claimOutcomes());
    lblCancelledClaims->setText(cancelled);
    lblCancelledClaims->setVisible(!cancelled.isEmpty());
    const bool attestor = ctl->isAttestor();
    btnCancelClaim->setVisible(attestor);
    const YellowbackPendingClaim* c = selectedPendingClaim();
    const int n = ctl->pendingClaimsModel()->rowCount(QModelIndex());
    if (c == nullptr) {
        btnReleaseClaim->setEnabled(false);
        btnCancelClaim->setEnabled(false);
        lblPendingAction->setText(n == 0 || ctl->pendingClaimsModel()->rowAt(0) == nullptr
            ? tr("No claim is pending.")
            : tr("Select a pending claim.") % (attestor ? " " % tr("This node's wallet is a current member of the attestor set: it can cancel a claim made at a wrong price.") : QString()));
        return;
    }
    const int height = ctl->height();
    const bool matured = height + 1 >= c->intent.releaseHeight;
    const bool residual = c->intent.role == ROLE_RESIDUAL;
    // A claimant intent pays the claimant's key (only that wallet knows the script); the residual pays
    // the owner, whose P2PKH script anyone can derive from the vault's ownerKeyId
    const bool canRelease = matured && (residual || c->mineClaim);
    const bool canCancel = attestor && !residual && !matured;
    btnReleaseClaim->setEnabled(actionsEnabled && canRelease);
    btnCancelClaim->setEnabled(actionsEnabled && canCancel);
    QString text;
    if (residual)
        text = matured ? tr("The owner's residual of vault %1 can be released to the owner now (anyone may).").arg(c->vault.vaultName())
                       : tr("The owner's residual of vault %1 can be released to the owner from height %2 (%3).")
                             .arg(c->vault.vaultName()).arg(c->intent.releaseHeight).arg(YellowbackPendingClaimsModel::remaining(c->intent, height));
    else if (matured)
        text = c->mineClaim ? tr("Your claim of vault %1 has matured: release the collateral to your address now.").arg(c->vault.vaultName())
                            : tr("The claim of vault %1 has matured; only the claimant's wallet can release it.").arg(c->vault.vaultName());
    else {
        text = tr("Claim %1 of vault %2 releases at height %3 (%4).").arg(c->intent.outpoint()).arg(c->vault.vaultName())
                   .arg(c->intent.releaseHeight).arg(YellowbackPendingClaimsModel::remaining(c->intent, height));
        if (c->mineClaim) text += " " % tr("It is your claim: an attestor may cancel it before then if it was made at a wrong price, and your burn would be lost.");
        if (c->mineVault) text += " " % tr("It is your vault: it returns to you as ACTIVE only if an attestor cancels the claim.");
        if (attestor)     text += " " % tr("As a current member of the attestor set you may cancel it if its price was wrong (deadline: height %1).").arg(c->intent.releaseHeight - 1);
    }
    if (!Settings::getInstance()->getYellowbackSignedCancel(c->intent.outpoint()).isEmpty())
        text += " " % tr("This wallet already signed a cancel of this intent; Cancel only re-sends that transaction.");
    lblPendingAction->setText(text);
}

void YellowbackTab::releaseClaim(const YellowbackPendingClaim& c) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc::PositionIntent;
    const bool residual = c.intent.role == ROLE_RESIDUAL;
    if (ctl->height() + 1 < c.intent.releaseHeight) {
        notice(tr("Release"), tr("The intent %1 matures at height %2 (the chain is at %3).").arg(c.intent.outpoint()).arg(c.intent.releaseHeight).arg(ctl->height()));
        return;
    }
    // vault_release finds the claimant's script among this wallet's keys; the residual's recipient is
    // the owner's P2PKH script, passed explicitly unless this wallet owns the vault
    QString script;
    if (residual && !c.mineVault) {
        script = YellowbackController::p2pkhScriptForKeyId(c.vault.ownerKeyId);
        if (script.isEmpty()) { notice(tr("Release"), tr("The node reported no owner key id for vault %1.").arg(c.vault.vaultName()), true); return; }
    }
    const QString text = residual
        ? tr("Release the owner's residual intent %1 of vault %2 to the vault owner (%3).\n\nThe network fee is paid from your YEC.")
              .arg(c.intent.outpoint()).arg(c.vault.vaultName()).arg(c.vault.ownerAddress)
        : tr("Release claim intent %1 of vault %2 to this wallet.\n\nThe claim delay has passed, so no attestor can cancel it any more; "
             "the vault becomes CLAIMED. The network fee is paid from your YEC.").arg(c.intent.outpoint()).arg(c.vault.vaultName());
    if (!confirm(tr("Confirm release"), text)) return;
    ctl->releaseIntent(c.intent.outpoint(), script,
        [=, this](const json& r) {
            const QString txid = r.is_string() ? QString::fromStdString(r.get<std::string>()) : YellowbackJson::toStr(r, "txid");
            notice(tr("Release sent"), tr("txid %1\nintent %2 released%3.").arg(txid).arg(c.intent.outpoint())
                       .arg(residual ? tr(" to the vault owner") : tr(" to this wallet; vault %1 becomes CLAIMED when it is mined").arg(c.vault.vaultName())));
            ctl->refresh(true);
        },
        [=, this](const QString& e) { failed(VaultRpc::VAULT_RELEASE, e); });
}

void YellowbackTab::cancelClaim(const YellowbackPendingClaim& c) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace VaultRpc;
    const QString outpoint = c.intent.outpoint();
    if (c.intent.role != YellowbackRpc::PositionIntent::ROLE_CLAIMANT) {
        notice(tr("Cancel claim"), tr("Only a claimant intent can be cancelled; the owner's residual is only ever released."), true);
        return;
    }
    // A signed cancel is never signed twice: two different spends of one intent signed by one member
    // are a provable equivocation (SET_EQUIVOCATION ejects the member and freezes its bond)
    const QString stored = Settings::getInstance()->getYellowbackSignedCancel(outpoint);
    if (!stored.isEmpty()) {
        if (!confirm(tr("Re-send cancel"), tr("This wallet already signed a cancel of intent %1. It will not sign another one (two different signed cancels "
                                               "of one intent are a provable equivocation that ejects you and freezes your bond).\n\nRe-send the transaction it signed?").arg(outpoint)))
            return;
        ctl->sendVaultTx(stored,
            [=, this](const json& r) {
                notice(tr("Cancel sent"), tr("txid %1\nthe cancel of intent %2 was re-sent.").arg(r.is_string() ? QString::fromStdString(r.get<std::string>()) : QString()).arg(outpoint));
                ctl->refresh(true);
            },
            [=, this](const QString& e) { failed(VAULT_SEND, e); });
        return;
    }
    if (!ctl->isAttestor()) {
        notice(tr("Cancel claim"), tr("This wallet holds no current member key of the YED attestor set, so it cannot sign a cancel."), true);
        return;
    }
    if (ctl->height() + 1 >= c.intent.releaseHeight) {
        notice(tr("Cancel claim"), tr("The claim intent %1 matured at height %2: it can no longer be cancelled.").arg(outpoint).arg(c.intent.releaseHeight), true);
        return;
    }
    const int threshold = ctl->setCancelThreshold();
    QString text = tr("Cancel the claim of vault %1 (claim intent %2, made at height %3).\n\n"
                      "WARNING. Cancel only a claim made at a wrong price: one that the attested and pooled prices did not support. A cancel is final: "
                      "the collateral returns to the vault, which is ACTIVE again under the cancel's txid with its debt unchanged, and the claimant's burn of %4 of YED "
                      "is NOT refunded. Cancelling an honest claim takes that burn from someone who did nothing wrong.\n\n"
                      "You sign as a member of the YED attestor set (%5 signature(s) needed). Sign one cancel of this intent, once: two different signed spends of the same "
                      "intent are a provable equivocation that anyone can submit, which ejects your membership and freezes your bond. This wallet keeps the transaction it "
                      "signs and will only ever re-send it.\n\n"
                      "The cancel must confirm by height %6. The network fee is paid from your YEC.")
        .arg(c.vault.vaultName()).arg(outpoint).arg(c.intent.height).arg(YellowbackFormat::cents(c.vault.mintedCents))
        .arg(threshold).arg(c.intent.releaseHeight - 1);
    if (!confirm(tr("Confirm cancel of a wrong-price claim"), text)) return;
    lblPendingAction->setText(tr("Cancelling: building the cancel..."));
    ctl->buildCancel(outpoint,
        [=, this](const json& built) {
            const QString hex = YellowbackJson::toStr(built, Cancel::HEX);
            ctl->signCancel(hex,
                [=, this](const json& signedTx) {
                    const QString shex = YellowbackJson::toStr(signedTx, Cancel::HEX);
                    if (shex.isEmpty()) { notice(tr("Cancel not sent"), tr("set_signcancel returned no transaction."), true); return; }
                    // Kept before anything is sent: from here on this wallet only re-sends it
                    Settings::getInstance()->setYellowbackSignedCancel(outpoint, shex);
                    if (!YellowbackJson::toBool(signedTx, Cancel::COMPLETE)) {
                        QGuiApplication::clipboard()->setText(shex);
                        notice(tr("Cancel needs more signatures"),
                               tr("This wallet signed the cancel of intent %1 (%2 of %3 signatures). The partly signed transaction is on the clipboard: "
                                  "other members add theirs with set_signcancel, and the node that built it sends it with vault_send before height %4.")
                                   .arg(outpoint).arg(YellowbackJson::toInt(signedTx, Cancel::SIGNATURES)).arg(YellowbackJson::toInt(signedTx, Cancel::REQUIRED))
                                   .arg(YellowbackJson::toInt(built, Cancel::DEADLINE, c.intent.releaseHeight - 1)));
                        updatePendingClaims();
                        return;
                    }
                    ctl->sendVaultTx(shex,
                        [=, this](const json& r) {
                            const QString txid = r.is_string() ? QString::fromStdString(r.get<std::string>()) : QString();
                            notice(tr("Cancel sent"), tr("txid %1\nthe claim intent %2 is cancelled when this confirms (by height %3): vault %4 is re-created as ACTIVE at %1:0, "
                                                         "and the claimant's burn is not refunded.")
                                       .arg(txid).arg(outpoint).arg(YellowbackJson::toInt(built, Cancel::DEADLINE, c.intent.releaseHeight - 1)).arg(c.vault.vaultName()));
                            ctl->refresh(true);
                        },
                        [=, this](const QString& e) {
                            updatePendingClaims();
                            failed(VAULT_SEND, e % "\n\n" % tr("The signed cancel is kept: Cancel on this row re-sends it, and never signs another."));
                        });
                },
                [=, this](const QString& e) { updatePendingClaims(); failed(SET_SIGNCANCEL, e); });
        },
        [=, this](const QString& e) { updatePendingClaims(); failed(VAULT_BUILDCANCEL, e); });
}

// ── Transactions ──────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupTransactions() {
    uiTx->tblTransactions->horizontalHeader()->setStretchLastSection(true);
    uiTx->tblTransactions->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(uiTx->tblTransactions, &QTableView::customContextMenuRequested, [=, this](const QPoint& pos) {
        showTxContextMenu(uiTx->tblTransactions, pos);
    });
}

void YellowbackTab::showTxContextMenu(QTableView* table, const QPoint& pos) {
    if (ctl == nullptr) return;
    auto index = table->indexAt(pos);
    if (!index.isValid()) return;
    auto tx = ctl->transactionsModel()->txAt(index.row());
    if (tx == nullptr) return;
    QString txid = tx->txid, type = tx->type, verdict = tx->verdict;   // copies: the model may reset

    QMenu menu(this);
    menu.addAction(tr("Copy txid"), [=]() { QGuiApplication::clipboard()->setText(txid); });
    menu.addAction(tr("View on block explorer"), [=]() { Settings::openTxInExplorer(txid); });
    menu.addAction(tr("Details (yed_gettxinfo)"), [=, this]() {
        ctl->getTxInfo(txid,
            [=, this](const json& info) {
                QString body = QString::fromStdString(info.dump(2));
                QMessageBox box(this);
                box.setWindowTitle(tr("Yellowback transaction ") % txid.left(12) % "...");
                box.setText(tr("Type: %1    Verdict: %2").arg(YellowbackFormat::typeLabel(type)).arg(verdict));
                box.setDetailedText(body);
                box.exec();
            },
            [=, this](const QString& e) { QMessageBox::warning(this, tr("yed_gettxinfo failed"), e); });
    });
    menu.exec(table->viewport()->mapToGlobal(pos));
}

// ── Redeem page ───────────────────────────────────────────────────────────────────────────
// The same confirmation dialog as the Vaults row, with a choice of collateral destination
// (plan I2): a fresh own transparent address (default), one of the wallet's s1… or ys1… addresses.

void YellowbackTab::setupRedeem() {
    QObject::connect(uiRedeem->cmbVault, &QComboBox::currentIndexChanged, [=, this](int) { updateRedeemPage(); });
    QObject::connect(uiRedeem->btnStart, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        QString vault = uiRedeem->cmbVault->currentData().toString();
        for (auto& p : ctl->positionsModel()->redeemable())
            if (p.txid == vault) { redeemVault(p, redeemDestination()); return; }
    });
    uiRedeem->btnStart->setEnabled(false);
    refreshDestinations();
}

QString YellowbackTab::redeemDestination() const {
    return uiRedeem->cmbDestination->currentData().toString();
}

void YellowbackTab::refreshDestinations() {
    QString keep = redeemDestination();
    QSignalBlocker block(uiRedeem->cmbDestination);
    uiRedeem->cmbDestination->clear();
    uiRedeem->cmbDestination->addItem(tr("A fresh transparent address of this wallet (default)"), QString());
    if (ctl != nullptr) {
        for (const auto& a : ctl->transparentAddresses())
            uiRedeem->cmbDestination->addItem(tr("Transparent %1: %2").arg(a.first).arg(Settings::getZECDisplayFormat(a.second)), a.first);
        for (const auto& a : ctl->saplingAddresses())
            uiRedeem->cmbDestination->addItem(tr("Shielded %1…%2: %3").arg(a.first.left(14)).arg(a.first.right(6)).arg(Settings::getZECDisplayFormat(a.second)), a.first);
    }
    int idx = keep.isEmpty() ? 0 : uiRedeem->cmbDestination->findData(keep);
    uiRedeem->cmbDestination->setCurrentIndex(idx < 0 ? 0 : idx);
}

void YellowbackTab::updateRedeemPage() {
    if (ctl == nullptr) return;
    QString keep = uiRedeem->cmbVault->currentData().toString();
    auto list = ctl->positionsModel()->redeemable();

    uiRedeem->cmbVault->blockSignals(true);
    uiRedeem->cmbVault->clear();
    for (auto& p : list) {
        uiRedeem->cmbVault->addItem(tr("%1  —  %2 minted, %3 collateral, %4")
            .arg(p.txid.left(16) % "...").arg(YellowbackFormat::cents(p.mintedCents))
            .arg(YellowbackFormat::zec(p.collateralZat)).arg(p.status), p.txid);
    }
    int i = uiRedeem->cmbVault->findData(keep);
    if (i >= 0) uiRedeem->cmbVault->setCurrentIndex(i);
    uiRedeem->cmbVault->blockSignals(false);

    QString vault = uiRedeem->cmbVault->currentData().toString();
    const YellowbackPosition* sel = nullptr;
    for (auto& p : list) if (p.txid == vault) { sel = &p; break; }
    refreshDestinations();
    if (sel == nullptr) {
        uiRedeem->lblBurn->setText("-");
        uiRedeem->lblCollateral->setText("-");
        uiRedeem->lblRedeemHint->setText(list.isEmpty() ? tr("No vault of yours is redeemable right now.") : QString());
        uiRedeem->btnStart->setEnabled(false);
    } else {
        bool isVoid = sel->status == YellowbackRpc::Position::STATUS_VOID;
        uiRedeem->lblBurn->setText(isVoid ? tr("none (VOID vault: Release burns nothing and pays no fee)") : YellowbackFormat::cents(sel->mintedCents));
        uiRedeem->lblCollateral->setText(YellowbackFormat::zec(sel->collateralZat) %
            (isVoid ? QString() : tr("  minus the pool fee")));
        bool enough = isVoid || sel->mintedCents <= ctl->confirmedCents();
        uiRedeem->lblRedeemHint->setText(enough ? QString() : tr("You need %1 of confirmed YED to burn but have %2.")
                .arg(YellowbackFormat::cents(sel->mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())));
        uiRedeem->btnStart->setText(isVoid ? tr("Release…") : tr("Redeem…"));
        uiRedeem->btnStart->setEnabled(actionsEnabled && enough);
    }
}

// ── Attestors ─────────────────────────────────────────────────────────────────────────────
// P4-b: an attestor is a member of the YED attestor set, a signer set of the vault primitive. The
// table is yed_listattestors (the module's records); the membership box is set_getinfo of the set
// (this wallet's member keys: current or dormant, last act, bond frozen) with the heartbeat.

void YellowbackTab::setupAttestors() {
    uiAttestors->tblAttestors->horizontalHeader()->setStretchLastSection(true);
    uiAttestors->lblArming->setText(tr("Waiting for the node..."));
    uiAttestors->lblMembership->setText(tr("Waiting for the node..."));
    uiAttestors->btnRegister->setEnabled(false);
    uiAttestors->btnReport->setEnabled(false);
    uiAttestors->btnHeartbeat->setEnabled(false);
    uiAttestors->btnWithdraw->setEnabled(false);
    uiAttestors->btnRegister->setToolTip(tr("Join the YED attestor set with a bond and a fresh key of this wallet (yed_registerattestor, a set_join). The yellowback-attest agent then signs beside this node."));
    uiAttestors->btnWithdraw->setToolTip(tr("Spend the selected attestor's bond back to this wallet once its locktime has passed (yed_withdrawbond; needs the member key; a frozen bond cannot be withdrawn)."));
    uiAttestors->btnHeartbeat->setToolTip(tr("Send a heartbeat for this wallet's member key (set_heartbeat): it keeps the member live, or makes a dormant one eligible again."));
    uiAttestors->btnReport->setToolTip(tr("Report two attestations by one attestor for one height at two prices (yed_reportequivocation); the attestor is ejected and its bond frozen."));

    auto selected = [=, this]() -> const YellowbackAttestor* {
        if (ctl == nullptr) return nullptr;
        auto idx = uiAttestors->tblAttestors->currentIndex();
        return ctl->attestorsModel()->rowAt(idx.isValid() ? idx.row() : -1);
    };
    QObject::connect(uiAttestors->btnRegister, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        using namespace YellowbackRpc;
        const json& ap = YellowbackJson::obj(ctl->params(), Params::ATTEST);
        QString bond = QString::number(YellowbackJson::toInt(ap, ParamsAttest::BOND_MIN_ZAT) / 100000000.0, 'f', 8);
        QString lock = QString::number(YellowbackJson::toInt(ap, ParamsAttest::BOND_MIN_LOCK));
        if (!inputFn(tr("Join the attestor set"), tr("Bond in YEC (minimum %1):").arg(bond), &bond)) return;
        if (!inputFn(tr("Join the attestor set"), tr("Bond lock in blocks (minimum %1):").arg(lock), &lock)) return;
        // The bond is sent as the decimal string typed (at most 8 decimals), never through a
        // double; every parse checks its ok flag (audit F-8).
        static const QRegularExpression bondRe("^\\d{1,9}(?:\\.\\d{1,8})?$");
        bool okLock = false;
        const QString b = bond.trimmed();
        int l = lock.trimmed().toInt(&okLock);
        if (!bondRe.match(b).hasMatch() || b.toDouble() <= 0 || !okLock || l <= 0) {
            notice(tr("Join the attestor set"), tr("Enter a bond in YEC (up to 8 decimals) and a lock in blocks."), true);
            return;
        }
        registerAttestor(b, l);
    });
    QObject::connect(uiAttestors->btnWithdraw, &QPushButton::clicked, [=, this]() { auto a = selected(); if (a) withdrawBond(*a); });
    QObject::connect(uiAttestors->btnHeartbeat, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        // The selected row's key when it is this wallet's, else this wallet's first member key
        QString key;
        const auto mine = ctl->walletMembers();
        if (auto a = selected())
            for (const auto& m : mine) if (m.key == a->attestorPubKey) key = m.key;
        if (key.isEmpty() && !mine.isEmpty()) key = mine.first().key;
        heartbeatMember(key);
    });
    QObject::connect(uiAttestors->btnReport, &QPushButton::clicked, [=, this]() {
        QString a, b;
        if (!inputFn(tr("Report equivocation"), tr("First attestation (148 hex characters):"), &a)) return;
        if (!inputFn(tr("Report equivocation"), tr("Second attestation, same attestor and height, a different price:"), &b)) return;
        reportEquivocation(a, b);
    });
}

void YellowbackTab::updateAttestors() {
    if (ctl == nullptr) return;
    QString banner = YellowbackController::describeAttest(ctl->attest());
    uiAttestors->lblArming->setText(banner.isEmpty() ? tr("The node reports no attestation state (not answered yet).") : banner);
    uiAttestors->tblAttestors->resizeColumnsToContents();
    auto sel = uiAttestors->tblAttestors->selectionModel();
    if (sel != nullptr) {
        QObject::disconnect(sel, nullptr, this, nullptr);
        QObject::connect(sel, &QItemSelectionModel::currentRowChanged, this, [=, this](const QModelIndex&, const QModelIndex&) { updateAttestorButtons(); });
    }
    updateMembership();
    updateAttestorButtons();
}

void YellowbackTab::updateMembership() {
    if (ctl == nullptr) return;
    const QString set = ctl->attestorSetId();
    if (set.isEmpty()) { uiAttestors->lblMembership->setText(tr("The node reports no YED attestor set.")); return; }
    if (ctl->attestorSet().empty()) { uiAttestors->lblMembership->setText(tr("YED attestor set %1: waiting for set_getinfo.").arg(set)); return; }
    const auto all  = ctl->setMembers();
    const auto mine = ctl->walletMembers();
    int current = 0;
    for (const auto& m : all) if (m.current) current++;
    QString text = tr("YED attestor set %1…: %2 member(s), %3 current; cancel threshold %4, liveness window %5 blocks%6.")
        .arg(set.left(16)).arg(all.size()).arg(current).arg(ctl->setCancelThreshold()).arg(ctl->setLivenessWindow())
        .arg(YellowbackJson::toBool(ctl->attestorSet(), VaultRpc::Set::DORMANT) ? tr("; the set is DORMANT (too few live members)") : QString());
    if (mine.isEmpty()) {
        text += "\n" % tr("This wallet holds no member key: it is not an attestor. \"Join the attestor set\" registers it.");
    } else {
        for (const auto& m : mine)
            text += "\n" % tr("Your member %1…: %2; bond %3 locked until %4.").arg(m.key.left(16)).arg(m.describe(ctl->height(), ctl->setLivenessWindow()))
                                .arg(YellowbackFormat::zec(m.bondValue)).arg(m.bondLocktime);
        if (ctl->isAttestor())
            text += "\n" % tr("As a current member this node may cancel a claim made at a wrong price (Pending claims page). Signing prices is not an act: "
                               "send a heartbeat before the liveness window runs out, or the yellowback-attest agent does it for you.");
    }
    uiAttestors->lblMembership->setText(text);
}

// The node does not say which attestor records are this wallet's (the bond is not IsMine, R6),
// so Withdraw is offered on any row that qualifies by status and height; the node refuses with
// attest-key-not-held for somebody else's. Heartbeat goes to this wallet's member keys (set_getinfo).
void YellowbackTab::updateAttestorButtons() {
    if (ctl == nullptr) return;
    using namespace YellowbackRpc::Attestor;
    uiAttestors->btnRegister->setEnabled(actionsEnabled && !ctl->attestorSetId().isEmpty());
    uiAttestors->btnReport->setEnabled(actionsEnabled);
    bool canHeartbeat = false;
    for (const auto& m : ctl->walletMembers()) if (m.current && !m.bondFrozen) canHeartbeat = true;
    uiAttestors->btnHeartbeat->setEnabled(actionsEnabled && canHeartbeat);
    auto idx = uiAttestors->tblAttestors->currentIndex();
    auto a = ctl->attestorsModel()->rowAt(idx.isValid() ? idx.row() : -1);
    if (a == nullptr) {
        uiAttestors->btnWithdraw->setEnabled(false);
        uiAttestors->lblAttestorAction->setText(ctl->attestorsModel()->rowCount(QModelIndex()) == 0
            ? tr("No attestor has joined yet.") : tr("Select an attestor of yours to withdraw its bond."));
        return;
    }
    bool withdrawable = a->bondSpentHeight < 0 && a->status != STATUS_WITHDRAWN && !a->bondFrozen && ctl->height() >= a->bondLocktime;
    uiAttestors->btnWithdraw->setEnabled(actionsEnabled && withdrawable);
    QString text;
    if (a->status == STATUS_WITHDRAWN || a->bondSpentHeight >= 0)
        text = tr("Attestor %1: its bond was withdrawn at height %2.").arg(a->seq).arg(a->bondSpentHeight);
    else if (a->bondFrozen)
        text = tr("Attestor %1: its bond of %2 is FROZEN by the set (a removal with burn or an equivocation) and can never be withdrawn.")
                   .arg(a->seq).arg(YellowbackFormat::zec(a->bondZat));
    else if (withdrawable)
        text = tr("Attestor %1: the bond of %2 is past its locktime (%3) and can be withdrawn by the wallet holding its member key.")
                   .arg(a->seq).arg(YellowbackFormat::zec(a->bondZat)).arg(a->bondLocktime);
    else
        text = tr("Attestor %1: the bond of %2 is locked until height %3 (the chain is at %4).")
                   .arg(a->seq).arg(YellowbackFormat::zec(a->bondZat)).arg(a->bondLocktime).arg(ctl->height());
    if (a->status == STATUS_DORMANT)
        text += " " % tr("It is DORMANT: a heartbeat by the wallet holding its member key makes it eligible again, with its age kept.");
    if (a->lastAct >= 0) text += " " % tr("Last act at height %1.").arg(a->lastAct);
    uiAttestors->lblAttestorAction->setText(text);
}

void YellowbackTab::registerAttestor(const QString& bondYec, int lockBlocks) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const json& ap = YellowbackJson::obj(ctl->params(), Params::ATTEST);
    const int locktime = ctl->height() + 1 + lockBlocks;
    const int maturity = (int)YellowbackJson::toInt(ap, ParamsAttest::BOND_MATURITY);
    QString text = tr("Join the YED attestor set with this wallet.\n\n"
                      "Bond: %1 of YEC, locked in a bond output until height %2 (%3 blocks, about %4 days; estimated %5). "
                      "Every Ycash node enforces that lock; only the member key in this wallet can spend it afterwards, and the set freezes it for an equivocation.\n"
                      "Your node sends a join act to the set %6…; the member becomes eligible for bundles at least %7 blocks after the join confirms, and gets its seq number then (see the table).\n\n"
                      "One fresh key is drawn: it is your member key, the hot key the agent signs prices with, the bond key and the fee key at once (it must stay online). "
                      "Back up wallet.dat after this; the key exists only there.\n\n"
                      "Stay live: a member with no heartbeat within the set's liveness window is dormant (signing prices is not an act). "
                      "Run the yellowback-attest agent on this node only. One key on two nodes defeats the node's equivocation guard: "
                      "the two would sooner or later sign different prices for one height, and anyone can report that, eject the member and freeze the bond.")
        .arg(QString::number(bondYec.toDouble(), 'f', 8)).arg(locktime).arg(lockBlocks)
        .arg((qint64)lockBlocks * SECONDS_PER_BLOCK / 86400)
        .arg(YellowbackFormat::estimateDate(locktime, ctl->height()).toString("yyyy-MM-dd"))
        .arg(ctl->attestorSetId().left(16)).arg(maturity);
    if (!confirm(tr("Confirm joining the attestor set"), text)) return;
    ctl->registerAttestor(bondYec, lockBlocks,
        [=, this](const json& r) {
            Settings::getInstance()->setYellowbackBackupPending(true);
            updateBackupNag();
            notice(tr("Join sent"),
                tr("txid %1\nbond %2 of YEC in %3, locked until height %4\nmember key %5\nfee address %6 (attestation fees are paid here)\n"
                   "eligible from height %7; the seq number appears in the table once the join confirms.\n\n"
                   "Back up wallet.dat now, then start the agent: yellowback-attest attest --conf attest.toml with that seq.")
                    .arg(YellowbackJson::toStr(r, RegisterResult::TXID))
                    .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RegisterResult::BOND_ZAT)))
                    .arg(YellowbackJson::toStr(r, RegisterResult::BOND_ADDRESS)).arg(YellowbackJson::toInt(r, RegisterResult::BOND_LOCKTIME))
                    .arg(YellowbackJson::toStr(r, RegisterResult::ATTESTOR_PUBKEY)).arg(YellowbackJson::toStr(r, RegisterResult::BOND_KEY_ADDRESS))
                    .arg(YellowbackJson::toInt(r, RegisterResult::MATURES_AT)));
            ctl->refresh(true);
        },
        [=, this](const QString& e) { failed("yed_registerattestor", e); });
}

void YellowbackTab::withdrawBond(const YellowbackAttestor& a, const QString& to) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const QString dest = to.isEmpty() ? tr("a fresh transparent address of this wallet") : to;
    if (a.bondFrozen) {
        notice(tr("Withdraw bond"), tr("The bond of attestor %1 is frozen by the attestor set and can never be withdrawn.").arg(a.seq), true);
        return;
    }
    if (ctl->height() < a.bondLocktime) {
        notice(tr("Withdraw bond"), tr("The bond of attestor %1 is locked until height %2 (the chain is at %3).").arg(a.seq).arg(a.bondLocktime).arg(ctl->height()));
        return;
    }
    QString text = tr("Withdraw the bond of attestor %1.\n\n"
                      "%2 of YEC, minus the network fee, goes to %3. The member becomes WITHDRAWN: it leaves the attestor set; "
                      "joining again means a new bond and a new seq.\n\n"
                      "This wallet must hold the member key of that join.")
        .arg(a.seq).arg(YellowbackFormat::zec(a.bondZat)).arg(dest);
    if (!confirm(tr("Confirm bond withdrawal"), text)) return;
    ctl->withdrawBond(a.seq, to,
        [=, this](const json& r) {
            notice(tr("Withdrawal sent"), tr("txid %1\nattestor %2\nbond out: %3 to %4")
                .arg(YellowbackJson::toStr(r, WithdrawResult::TXID)).arg(YellowbackJson::toInt(r, WithdrawResult::SEQ))
                .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, WithdrawResult::BOND_OUT))).arg(YellowbackJson::toStr(r, WithdrawResult::TO)));
            ctl->refresh(true);
        },
        [=, this](const QString& e) { failed("yed_withdrawbond", e); });
}

void YellowbackTab::heartbeatMember(const QString& memberKey) {
    if (ctl == nullptr || !actionsEnabled) return;
    if (memberKey.isEmpty()) {
        notice(tr("Heartbeat"), tr("This wallet holds no member key of the YED attestor set."), true);
        return;
    }
    YellowbackSetMember m;
    for (const auto& x : ctl->walletMembers()) if (x.key == memberKey) m = x;
    QString text = tr("Send a heartbeat for member key %1 of the YED attestor set.\n\n"
                      "It records an act at the height it confirms: the member stays live for the next %2 blocks, and a dormant member is eligible again with its age kept. "
                      "Current state: %3.\n\nCost: a network fee from your YEC.")
        .arg(memberKey).arg(ctl->setLivenessWindow()).arg(m.key.isEmpty() ? tr("unknown") : m.describe(ctl->height(), ctl->setLivenessWindow()));
    if (!confirm(tr("Confirm heartbeat"), text)) return;
    ctl->heartbeat(memberKey,
        [=, this](const json& r) {
            notice(tr("Heartbeat sent"), tr("txid %1\nmember key %2").arg(YellowbackJson::toStr(r, VaultRpc::Heartbeat::TXID))
                                             .arg(YellowbackJson::toStr(r, VaultRpc::Heartbeat::MEMBER_KEY, memberKey)));
            ctl->refresh(true);
        },
        [=, this](const QString& e) { failed(VaultRpc::SET_HEARTBEAT, e); });
}

void YellowbackTab::reportEquivocation(const QString& hexA, const QString& hexB) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const QString a = hexA.trimmed(), b = hexB.trimmed();
    static const QRegularExpression hex("^[0-9a-fA-F]+$");
    if (a.length() != ATTESTATION_HEX_LENGTH || b.length() != ATTESTATION_HEX_LENGTH || !hex.match(a).hasMatch() || !hex.match(b).hasMatch()) {
        notice(tr("Report equivocation"), tr("Each attestation is 74 bytes: %1 characters of hex.").arg(ATTESTATION_HEX_LENGTH), true);
        return;
    }
    if (a.compare(b, Qt::CaseInsensitive) == 0) {
        notice(tr("Report equivocation"), tr("The two attestations are the same; an equivocation is two different prices from one attestor for one height."), true);
        return;
    }
    QString text = tr("Report an equivocation.\n\n"
                      "Your node checks that the two attestations come from one attestor, cite one height on this chain and carry different prices, "
                      "both validly signed. If they do, the report ejects that attestor and the set freezes its bond. "
                      "Anyone may report; the cost is the carrier and a network fee from your YEC, built in two steps like a mint.");
    if (!confirm(tr("Confirm equivocation report"), text)) return;
    uiAttestors->lblAttestorAction->setText(tr("Reporting..."));
    ctl->reportEquivocation(a, b,
        [=, this](const json& r) {
            const int seq = (int)YellowbackJson::toInt(r, EquivocationResult::SEQ);
            followPending(Transaction::TYPE_EQUIVOCATION, r, uiAttestors->lblAttestorAction, [=, this](const json& done) {
                notice(tr("Equivocation report sent"), tr("txid %1\nattestor %2, cited height %3, prices %4 and %5")
                    .arg(YellowbackJson::toStr(done, EquivocationResult::TXID)).arg(seq)
                    .arg(YellowbackJson::toInt(r, EquivocationResult::CITED_HEIGHT))
                    .arg(YellowbackFormat::price(YellowbackJson::toInt(r, EquivocationResult::PRICE_A)))
                    .arg(YellowbackFormat::price(YellowbackJson::toInt(r, EquivocationResult::PRICE_B))));
                ctl->refresh(true);
            });
        },
        [=, this](const QString& e) {
            updateAttestorButtons();
            failed("yed_reportequivocation", e);
        });
}

// ── Settings page ─────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupSettings() {
    uiSettings->cmbTransportKind->addItem(tr("dir (shared directory)"), "dir");
    uiSettings->cmbTransportKind->addItem(tr("iroh (gossip network)"), "iroh");
    QObject::connect(uiSettings->cmbTransportKind, &QComboBox::currentIndexChanged, [=, this](int) {
        bool dir = uiSettings->cmbTransportKind->currentData().toString() == "dir";
        uiSettings->txtTransportPath->setEnabled(dir);
        uiSettings->txtTransportRelays->setEnabled(!dir);
        uiSettings->txtTransportPeers->setEnabled(!dir);
    });
    updateSettingsPage();
    QObject::connect(uiSettings->btnSave,   &QPushButton::clicked, [=, this]() { saveSettings(); });
    QObject::connect(uiSettings->btnSubscriberStart, &QPushButton::clicked, [=, this]() { startSubscriber(); });
    QObject::connect(uiSettings->btnSubscriberStop,  &QPushButton::clicked, [=, this]() { stopSubscriber(); });
    QObject::connect(uiSettings->btnSweepCarriers,   &QPushButton::clicked, [=, this]() { sweepCarriers(); });
    uiSettings->btnSweepCarriers->setEnabled(false);
}

// ── v3 subscriber launcher (plan §4.8 Settings row) ───────────────────────────────────────
// The binary is looked for beside the wallet, where build.sh --attest puts it next to ycashd.

QString YellowbackTab::defaultSubscriberBinaryPath() {
    QDir appPath(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    return appPath.absoluteFilePath("yellowback-attest.exe");
#else
    return appPath.absoluteFilePath("yellowback-attest");
#endif
}

QString YellowbackTab::subscriberBinaryPath() const {
    return subscriberBinary.isEmpty() ? defaultSubscriberBinaryPath() : subscriberBinary;
}

QString YellowbackTab::cookiePathFor(const QString& zcashDir, const QString& network) {
    QString sub;
    if (network == "test")         sub = "testnet3/";
    else if (network == "regtest") sub = "regtest/";
    return QDir(zcashDir).absoluteFilePath(sub % ".cookie");
}

// Only the keys the subscriber reads (attest.toml.sample documents them): [node] for its own
// node's RPC, [transport] as saved on this page, [subscribe] at the default cadence. A value
// is quoted as a TOML basic string.
QString YellowbackTab::subscriberConfigToml(const QString& kind, const QString& path, const QString& relays, const QString& peers,
                                            const QString& rpcUrl, const QString& cookieFile, const QString& rpcUser, const QString& rpcPassword) {
    // Explicit return types: a deduced one would return a QStringBuilder over a dead local.
    // A TOML basic string: backslash, quote and every control character escaped (audit F-4),
    // so a newline pasted into a field cannot end the string or add a key.
    auto q = [](const QString& v) -> QString {
        QString e;
        for (const QChar ch : v) {
            const ushort u = ch.unicode();
            if      (ch == '\\') e += "\\\\";
            else if (ch == '"')  e += "\\\"";
            else if (ch == '\n') e += "\\n";
            else if (ch == '\r') e += "\\r";
            else if (ch == '\t') e += "\\t";
            else if (u < 0x20 || u == 0x7f) e += QString("\\u%1").arg(u, 4, 16, QChar('0'));
            else e += ch;
        }
        return "\"" % e % "\"";
    };
    auto list = [&](const QString& csv) -> QString {
        QStringList items;
        for (const QString& it : csv.split(',', Qt::SkipEmptyParts)) if (!it.trimmed().isEmpty()) items << q(it.trimmed());
        return "[" % items.join(", ") % "]";
    };
    QString out = "# Written by YecWallet for `yellowback-attest subscribe`; edit the Settings page, not this file.\n\n";
    out += "[node]\n";
    out += "rpc_url = " % q(rpcUrl) % "\n";
    if (!cookieFile.isEmpty())  out += "rpc_cookie = " % q(cookieFile) % "\n";
    else {
        out += "rpc_user = " % q(rpcUser) % "\n";
        out += "rpc_password = " % q(rpcPassword) % "\n";
    }
    out += "\n[transport]\n";
    if (kind == "dir") {
        out += "kind = \"dir\"\n";
        out += "path = " % q(path) % "\n";
    } else {
        out += "kind = \"iroh\"\n";
        if (!relays.trimmed().isEmpty()) out += "relays = " % list(relays) % "\n";
        if (!peers.trimmed().isEmpty())  out += "peers = " % list(peers) % "\n";
    }
    out += "\n[subscribe]\n";
    out += "listattestors_seconds = 60\n";
    return out;
}

// A file only its owner can read: created empty and made 0600 before a byte is written, so
// the RPC password in it is never world-readable for an instant (audit F-4).
bool YellowbackTab::writePrivateFile(const QString& path, const QByteArray& bytes, QString* error) {
    QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { if (error) *error = f.errorString(); return false; }
    if (!f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = tr("could not restrict the file's permissions: %1").arg(f.errorString());
        f.close(); QFile::remove(path);
        return false;
    }
    if (f.write(bytes) != bytes.size() || !f.flush() || f.error() != QFileDevice::NoError) {
        if (error) *error = f.errorString();
        f.close(); QFile::remove(path);
        return false;
    }
    f.close();
    return true;
}

void YellowbackTab::startSubscriber() {
    if (subscriber != nullptr && subscriber->state() != QProcess::NotRunning) return;
    const QString bin = subscriberBinaryPath();
    if (!QFileInfo(bin).isExecutable()) {
        uiSettings->lblSubscriberStatus->setText(tr("not running — %1 is not an executable file; run `yellowback-attest subscribe` by hand or package it beside the wallet.").arg(bin));
        return;
    }
    auto s = Settings::getInstance();
    if (s->getYellowbackTransportKind() == "dir" && s->getYellowbackTransportPath().trimmed().isEmpty()) {
        uiSettings->lblSubscriberStatus->setText(tr("not running — the dir transport needs a directory; set and save it above."));
        return;
    }
    // The node's RPC as this wallet reaches it: the cookie file when the node writes one, else
    // the rpcuser/rpcpassword of the connection (the wallet's own ycash.conf sets those).
    QString rpcUrl = "http://127.0.0.1:8832", cookie, user, pass;
    Connection* conn = ctl != nullptr ? ctl->connection() : nullptr;
    if (conn != nullptr && conn->config) {
        const auto& c = conn->config;
        rpcUrl = "http://" % (c->host.isEmpty() ? QString("127.0.0.1") : c->host) % ":" % (c->port.isEmpty() ? QString("8832") : c->port);
        user = c->rpcuser; pass = c->rpcpassword;
        // The cookie lives in the network directory the conf implies (datadir=, testnet=,
        // regtest=; audit F-4), not necessarily beside the conf.
        const QString confLocation = Settings::getInstance()->getZcashdConfLocation();
        QString candidate = !confLocation.isEmpty() ? QDir(NodeDataCheck::resolve(confLocation).netDir).filePath(".cookie")
                          : c->zcashDir.isEmpty()   ? QString() : cookiePathFor(c->zcashDir, ctl->network());
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) cookie = candidate;
    }
    QString toml = subscriberConfigToml(s->getYellowbackTransportKind(), s->getYellowbackTransportPath(), s->getYellowbackTransportRelays(),
                                        s->getYellowbackTransportPeers(), rpcUrl, cookie, user, pass);
    QDir dir(Settings::appDataLocation());
    dir.mkpath(".");
    subscriberConfPath = dir.absoluteFilePath("yellowback-subscribe.toml");
    QString writeError;
    if (!writePrivateFile(subscriberConfPath, toml.toUtf8(), &writeError)) {   // it may carry the RPC password
        uiSettings->lblSubscriberStatus->setText(tr("not running — cannot write %1: %2").arg(subscriberConfPath).arg(writeError));
        return;
    }

    // A fresh process per start (a finished one is dropped), so the connections are made once
    if (subscriber != nullptr && subscriber->parent() == this) subscriber->deleteLater();
    QProcess* proc = new QProcess(this);
    subscriber = proc;
    QObject::connect(proc, &QProcess::stateChanged, this, [=, this](QProcess::ProcessState) { if (subscriber == proc) updateSettingsPage(); });
    QObject::connect(proc, &QProcess::errorOccurred, this, [=, this](QProcess::ProcessError) {
        if (subscriber == proc) uiSettings->lblSubscriberStatus->setText(tr("not running — %1: %2").arg(bin).arg(proc->errorString()));
    });
    QObject::connect(proc, &QProcess::readyReadStandardError, this, [=, this]() {
        QString line = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (main != nullptr && main->logger != nullptr) main->logger->write("yellowback-attest: " + line);
    });
    proc->start(bin, QStringList() << "subscribe" << "--conf" << subscriberConfPath);
    updateSettingsPage();
}

void YellowbackTab::stopSubscriber() {
    if (subscriber == nullptr || subscriber->state() == QProcess::NotRunning) return;
    subscriber->terminate();
    if (!subscriber->waitForFinished(3000)) subscriber->kill();
    updateSettingsPage();
}

void YellowbackTab::sweepCarriers() {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    uiSettings->lblCarriers->setText(tr("Reclaiming..."));
    ctl->sweepCarriers(
        [=, this](const json& r) {
            int count = (int)YellowbackJson::toInt(r, SweepCarriersResult::COUNT);
            int outstanding = (int)YellowbackJson::toInt(r, SweepCarriersResult::OUTSTANDING);
            uiSettings->lblCarriers->setText(count == 0
                ? tr("No lapsed carrier to reclaim; %1 still inside their window.").arg(outstanding)
                : tr("Reclaimed %1 carrier(s), %2 of YEC net of the fee, in txid %3; %4 still inside their window.")
                      .arg(count).arg(YellowbackFormat::zec(YellowbackJson::toInt(r, SweepCarriersResult::RECLAIMED_ZAT)))
                      .arg(YellowbackJson::toStr(r, SweepCarriersResult::TXID)).arg(outstanding));
        },
        [=, this](const QString& e) {
            uiSettings->lblCarriers->setText(tr("yed_sweepcarriers failed: ") % e);
            failed("yed_sweepcarriers", e);
        });
}

QString YellowbackTab::subscriberStatus(const QProcess* p) {
    if (p == nullptr || p->state() == QProcess::NotRunning) return tr("not running");
    if (p->state() == QProcess::Starting) return tr("starting");
    return tr("running (pid %1)").arg(p->processId());
}

void YellowbackTab::updateSettingsPage() {
    auto s = Settings::getInstance();
    uiSettings->chkUnitCents->setChecked(s->getYellowbackUnitCents());
    uiSettings->chkAdvanced->setChecked(s->getYellowbackAdvanced());
    uiSettings->lblRpcVersion->setText(tr("This YecWallet understands Yellowback RPC version %1.").arg(Settings::getYellowbackRpcVersion()) %
        (ctl != nullptr && ctl->isVersionOk() ? tr(" The node matches.") : ""));
    // v3: the subscriber keeps this node's attestation pool filled; without it a mint cannot
    // build its bundle while the layer is armed.
    const bool running = subscriber != nullptr && subscriber->state() != QProcess::NotRunning;
    uiSettings->lblSubscriberStatus->setText(subscriberStatus(subscriber) %
        (running ? QString() : tr(" — until the subscriber runs, this node's attestation pool stays empty and a mint cannot build its price proof while the layer is armed.")));
    const QString bin = subscriberBinaryPath();
    const bool haveBinary = QFileInfo(bin).isExecutable();
    uiSettings->lblSubscriberBinary->setText(haveBinary ? bin
        : tr("%1 not found beside the wallet: the launcher is disabled. Package it (build.sh --attest) or run `yellowback-attest subscribe --conf attest.toml` by hand.").arg(bin));
    uiSettings->btnSubscriberStart->setEnabled(haveBinary && !running);
    uiSettings->btnSubscriberStop->setEnabled(running);
    int kind = uiSettings->cmbTransportKind->findData(s->getYellowbackTransportKind());
    uiSettings->cmbTransportKind->setCurrentIndex(kind < 0 ? 0 : kind);
    emit uiSettings->cmbTransportKind->currentIndexChanged(uiSettings->cmbTransportKind->currentIndex());
    uiSettings->txtTransportPath->setText(s->getYellowbackTransportPath());
    uiSettings->txtTransportRelays->setText(s->getYellowbackTransportRelays());
    uiSettings->txtTransportPeers->setText(s->getYellowbackTransportPeers());
}

void YellowbackTab::saveSettings() {
    auto s = Settings::getInstance();
    s->setYellowbackUnitCents(uiSettings->chkUnitCents->isChecked());
    s->setYellowbackAdvanced(uiSettings->chkAdvanced->isChecked());
    s->setYellowbackTransportKind(uiSettings->cmbTransportKind->currentData().toString());
    s->setYellowbackTransportPath(uiSettings->txtTransportPath->text().trimmed());
    s->setYellowbackTransportRelays(uiSettings->txtTransportRelays->text().trimmed());
    s->setYellowbackTransportPeers(uiSettings->txtTransportPeers->text().trimmed());
    if (ctl != nullptr) {
        updateBalances();
        updateOverview();
        ctl->refresh(true);   // re-renders the models in the chosen unit
    }
    updateRedeemPage();
}
