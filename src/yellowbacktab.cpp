#include "yellowbacktab.h"
#include "yellowbackcontroller.h"
#include "yellowbackmodels.h"
#include "yellowbackrpc.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "addressbook.h"
#include "settings.h"

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

using json = nlohmann::json;

// Phase 7b-a ships the node-context screens; the spending actions are Phase 7b-b. Every such
// button is disabled and its tooltip / status line says so (plan Phase 7b, N26).
static const char* LATER_RELEASE = QT_TRANSLATE_NOOP("YellowbackTab", "arrives in a later release of YecWallet (the node already supports it: use ycash-cli)");

YellowbackTab::YellowbackTab(MainWindow* main, QWidget* parent) : QWidget(parent) {
    this->main = main;
    ui = new Ui::YellowbackTab();
    ui->setupUi(this);

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
    delete uiOverview;
    delete uiReceive;
    delete uiSend;
    delete uiMint;
    delete uiPositions;
    delete uiClaim;
    delete uiTx;
    delete uiRedeem;
    delete uiSettings;
    delete ui;
}

// ── Construction ──────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupPages() {
    for (int i = 0; i < PageCount; i++)
        pages[i] = new QWidget(this);

    uiOverview  = new Ui::YellowbackOverview();     uiOverview->setupUi(pages[Overview]);
    uiReceive   = new Ui::YellowbackReceive();      uiReceive->setupUi(pages[Receive]);
    uiSend      = new Ui::YellowbackSend();         uiSend->setupUi(pages[Send]);
    uiMint      = new Ui::YellowbackMint();         uiMint->setupUi(pages[Mint]);
    uiPositions = new Ui::YellowbackPositions();    uiPositions->setupUi(pages[Vaults]);
    uiClaim     = new Ui::YellowbackClaim();        uiClaim->setupUi(pages[Claim]);
    uiTx        = new Ui::YellowbackTransactions(); uiTx->setupUi(pages[Transactions]);
    uiRedeem    = new Ui::YellowbackRedeem();       uiRedeem->setupUi(pages[Redeem]);
    uiSettings  = new Ui::YellowbackSettings();     uiSettings->setupUi(pages[Settings]);

    ui->subTabs->addTab(pages[Overview],     tr("Overview"));
    ui->subTabs->addTab(pages[Receive],      tr("Receive"));
    ui->subTabs->addTab(pages[Send],         tr("Send"));
    ui->subTabs->addTab(pages[Mint],         tr("Mint"));
    ui->subTabs->addTab(pages[Vaults],       tr("Vaults"));
    ui->subTabs->addTab(pages[Claim],        tr("Claim"));
    ui->subTabs->addTab(pages[Transactions], tr("Transactions"));
    ui->subTabs->addTab(pages[Redeem],       tr("Redeem"));
    ui->subTabs->addTab(pages[Settings],     tr("Settings"));

    setupOverview();
    setupReceive();
    setupSend();
    setupMint();
    setupPositions();
    setupClaim();
    setupTransactions();
    setupRedeem();
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
    uiPositions->tblPositions->setModel(ctl->positionsModel());
    uiClaim->tblClaimable->setModel(ctl->claimableModel());

    QObject::connect(ctl, &YellowbackController::availabilityChanged, this, [=, this](bool, const QString&) {
        updateBanner();
        updateMintGate();
    });
    QObject::connect(ctl, &YellowbackController::infoUpdated,         this, [=, this]() { updateBanner(); updateOverview(); updateMintClasses(); updateVaultButtons(); });
    QObject::connect(ctl, &YellowbackController::statsUpdated,        this, [=, this]() { updateBanner(); updateOverview(); updateMintGate(); });
    QObject::connect(ctl, &YellowbackController::balanceUpdated,      this, [=, this]() { updateBalances(); });
    QObject::connect(ctl, &YellowbackController::positionsUpdated,    this, [=, this]() { updatePositions(); updateRedeemPage(); });
    QObject::connect(ctl, &YellowbackController::claimableUpdated,    this, [=, this]() { updateClaimPage(); });
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
    // Phase 7b-b actions: never enabled in this build
    uiMint->btnMint->setEnabled(false);
    uiPositions->btnRelease->setEnabled(false);
    uiPositions->btnRedeem->setEnabled(false);
    uiPositions->btnSweep->setEnabled(false);
    uiClaim->btnClaim->setEnabled(false);
    uiRedeem->btnStart->setEnabled(false);
}

void YellowbackTab::notInThisBuild(const QString& what) {
    QMessageBox::information(this, tr("Not available in this build"),
        tr("%1 %2.").arg(what).arg(tr(LATER_RELEASE)));
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
    const json& act  = YellowbackJson::obj(info, Info::ACTIVATION);

    uiOverview->lblNetwork->setText(ctl->network().isEmpty() ? "-" : ctl->network());
    uiOverview->lblIndexHeight->setText(QString::number(ctl->height()) %
        (ctl->isSynced() ? tr(" (at the chain tip)") : tr(" (chain at %1)").arg(ctl->chainHeight())));
    uiOverview->lblYecBalance->setText(Settings::getZECUSDDisplayFormat(ctl->yecBalance()));

    // Activation and enforcement, from yed_getinfo
    QString status = YellowbackJson::toStr(act, Activation::STATUS, "-");
    if (status == Activation::STATUS_SIGNALING)
        uiOverview->lblActivation->setText(tr("signalling, %1/%2 of recent blocks")
            .arg(YellowbackJson::toInt(act, Activation::SIGNAL_COUNT)).arg(YellowbackJson::toInt(act, Activation::WINDOW)));
    else if (status == Activation::STATUS_LOCKED_IN)
        uiOverview->lblActivation->setText(tr("locked in at %1, active at %2")
            .arg(YellowbackJson::toInt(act, Activation::LOCK_IN_HEIGHT)).arg(YellowbackJson::toInt(act, Activation::ACTIVATE_HEIGHT)));
    else if (status == Activation::STATUS_ACTIVE)
        uiOverview->lblActivation->setText(tr("active since %1 (%2/%3 of recent blocks signalling)")
            .arg(YellowbackJson::toInt(act, Activation::ACTIVATE_HEIGHT))
            .arg(YellowbackJson::toInt(act, Activation::SIGNAL_COUNT)).arg(YellowbackJson::toInt(act, Activation::WINDOW)));
    else
        uiOverview->lblActivation->setText(status);

    QStringList enf;
    if (YellowbackJson::toBool(info, Info::ABANDONED))          enf << tr("abandoned");
    if (YellowbackJson::toBool(info, Info::VALVE_TRIPPED))      enf << tr("valve tripped — restart the node");
    if (YellowbackJson::toBool(info, Info::SUNSET))             enf << tr("sunset — upgrade the node");
    QStringList halts = YellowbackJson::strings(s, Stats::HALT_MASK);
    if (halts.contains(Stats::HALT_ENFORCEMENT))                enf << tr("suspended (under 50 % signalling)");
    if (enf.isEmpty())
        enf << (YellowbackJson::toBool(info, Info::ENFORCING) ? tr("this node enforces") : tr("this node does not enforce"));
    if (info.is_object() && !info.empty())
        enf << tr("template policy %1").arg(YellowbackJson::toStr(info, Info::TEMPLATE_POLICY, "-"));
    uiOverview->lblEnforcement->setText(enf.join(", "));

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
        uiOverview->lblCapHeadroom->setText(tr("%1 of %2 cap").arg(YellowbackFormat::cents(std::max<qint64>(0, cap - supply))).arg(YellowbackFormat::cents(cap)));
    }

    QString blocker = ctl->mintBlocker(0);
    uiOverview->lblMintStatus->setText(blocker.isEmpty() ? tr("open") : blocker);

    uiOverview->lblSupply->setText(YellowbackFormat::cents(YellowbackJson::toInt(s, Stats::SUPPLY_CENTS)) % " / " %
                                   YellowbackFormat::zec(YellowbackJson::toInt(s, Stats::COLLATERAL_ZAT)));
    uiOverview->lblVaults->setText(QString::number(YellowbackJson::toInt(s, Stats::ACTIVE_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, Stats::VOID_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, Stats::CLOSED_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, Stats::CLAIMED_VAULTS)));
    uiOverview->lblUnbacked->setText(YellowbackFormat::cents(YellowbackJson::toInt(s, Stats::UNBACKED_CENTS)));

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

bool YellowbackTab::parseDollars(const QString& text, qint64* cents) {
    QString t = text.trimmed();
    t.remove('$').remove(',').remove(' ');
    static const QRegularExpression re("^(\\d{1,9})(?:\\.(\\d{1,2}))?$");
    auto m = re.match(t);
    if (!m.hasMatch()) return false;
    qint64 whole = m.captured(1).toLongLong();
    QString frac = m.captured(2);
    qint64 c = 0;
    if (!frac.isEmpty()) {
        if (frac.length() == 1) frac += "0";
        c = frac.toLongLong();
    }
    *cents = whole * 100 + c;
    return true;
}

void YellowbackTab::setupSend() {
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
            QString text = tr("Send %1 of YED to\n%2\n\nThe YEC fee is paid from your transparent YEC. "
                              "This transfer is transparent and cannot be undone.")
                              .arg(YellowbackFormat::cents(cents)).arg(addr);
            if (QMessageBox::question(this, tr("Confirm Yellowback transfer"), text,
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
                return;

            uiSend->btnSend->setEnabled(false);
            uiSend->lblSendStatus->setText(tr("Sending..."));
            ctl->send(addr, cents,
                [=, this](const json& r) {
                    uiSend->btnSend->setEnabled(actionsEnabled);
                    QString txid = YellowbackJson::toStr(r, YellowbackRpc::SendResult::TXID);
                    uiSend->lblSendStatus->setText(tr("Sent. txid: ") % txid);
                    uiSend->txtAmount->clear();
                    ctl->refresh(true);
                },
                [=, this](const QString& e) {
                    uiSend->btnSend->setEnabled(actionsEnabled);
                    QString msg = tr("yed_send failed: ") % e;
                    if (e.startsWith(YellowbackRpc::Errors::CHANGE_FLOOR))
                        msg += tr("\n\nThe change left over would be below the minimum output; the node's message above names the amounts that work.");
                    else if (e.contains(YellowbackRpc::RpcErrors::WALLET_LOCKED))
                        msg += tr("\n\nUnlock the wallet first (walletpassphrase in the console tab).");
                    uiSend->lblSendStatus->setText(msg);
                    QMessageBox::critical(this, tr("Yellowback transfer failed"), msg);
                });
        },
        [=, this](const QString& e) {
            uiSend->lblSendHint->setText(tr("yed_validateaddress failed: ") % e);
        });
}

// ── Mint ──────────────────────────────────────────────────────────────────────────────────
// Phase 7b-a: the page shows the live estimate for a class's shortest lock so the collateral
// figure and the MINTPOL-1 gate are visible; the Mint button itself is Phase 7b-b.

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
    uiMint->btnMint->setEnabled(false);
    uiMint->btnMint->setToolTip(tr("Minting from this screen %1.").arg(tr(LATER_RELEASE)));
    uiMint->lblMintStatus->setText(tr("Minting from this screen %1.").arg(tr(LATER_RELEASE)));
}

void YellowbackTab::updateMintClasses() {
    if (ctl == nullptr) return;
    auto classes = ctl->termClasses();
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
    QString blocker = ctl->mintBlocker(haveAmount ? cents : 0);
    uiMint->lblGate->setVisible(!blocker.isEmpty());
    uiMint->lblGate->setText(blocker);
    uiMint->btnMint->setEnabled(false);   // Phase 7b-b
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
            updateMintGate();
        });
}

void YellowbackTab::doMint() {
    // Phase 7b-b: the confirmation (class, σ, exact collateral, enforcement fee and payee) and
    // the yed_mint call. The button is never enabled in this build.
    notInThisBuild(tr("Minting from this screen"));
}

// ── Vaults (positions) ────────────────────────────────────────────────────────────────────

YellowbackTab::VaultActions YellowbackTab::vaultActions(const YellowbackPosition& p, int height, bool abandoned) {
    using namespace YellowbackRpc::Position;
    VaultActions a;
    const QString lockAt = tr("lock height %1").arg(p.lockHeight);
    if (p.status == STATUS_VOID) {
        if (p.canRedeem || height >= p.lockHeight) {
            a.release = true;
            a.text = tr("VOID vault: Release returns the collateral with no YED burned and no fee paid. "
                        "Do it before height %1, after which the claim path is open to anyone.").arg(p.claimHeight);
        } else {
            a.text = tr("VOID vault: the collateral can be released from %1 on (no burn, no fee), and must be released before height %2.")
                        .arg(lockAt).arg(p.claimHeight);
        }
    } else if (p.status == STATUS_ACTIVE) {
        if (abandoned || p.canSweep) {
            a.sweep = true;
            a.text = tr("Enforcement is abandoned. Sweep moves the collateral out with no burn and no fee; the %1 minted against it stay in circulation unbacked. "
                        "Sweep before height %2 or whoever claims first takes the collateral.")
                        .arg(YellowbackFormat::cents(p.mintedCents)).arg(p.sweepBefore > 0 ? p.sweepBefore : p.claimHeight);
        }
        if (p.canRedeem || height >= p.lockHeight) {
            a.redeem = true;
            a.text += (a.text.isEmpty() ? QString() : QString(" ")) %
                      tr("Redeem burns %1 of your YED and pays an enforcement fee from the collateral; the rest returns to you.")
                          .arg(YellowbackFormat::cents(p.mintedCents));
        } else if (!a.sweep) {
            a.text = tr("Active vault: redeemable from %1 on by burning %2 of YED.").arg(lockAt).arg(YellowbackFormat::cents(p.mintedCents));
        }
        if (p.claimable)
            a.text += " " % tr("This vault is past its claim height and underwater: anyone may claim it by burning its debt.");
    } else if (p.status == STATUS_CLOSED) {
        a.text = p.unbacked
            ? tr("Closed without its burn at height %1; the %2 minted against it are unbacked.").arg(p.closeHeight).arg(YellowbackFormat::cents(p.mintedCents))
            : tr("Closed at height %1 after burning %2.").arg(p.closeHeight).arg(YellowbackFormat::cents(p.burnedCents));
    } else if (p.status == STATUS_CLAIMED) {
        a.text = tr("Claimed by someone else at height %1: the vault was underwater past its claim height and they burned %2 to take the collateral.")
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
    uiPositions->btnSweep->setEnabled(false);
    uiPositions->btnRelease->setToolTip(tr("Release %1.").arg(tr(LATER_RELEASE)));
    uiPositions->btnRedeem->setToolTip(tr("Redeem %1.").arg(tr(LATER_RELEASE)));
    uiPositions->btnSweep->setToolTip(tr("Sweep %1.").arg(tr(LATER_RELEASE)));

    QObject::connect(uiPositions->btnRelease, &QPushButton::clicked, [=, this]() { notInThisBuild(tr("Release")); });
    QObject::connect(uiPositions->btnRedeem,  &QPushButton::clicked, [=, this]() { notInThisBuild(tr("Redeem")); });
    QObject::connect(uiPositions->btnSweep,   &QPushButton::clicked, [=, this]() { notInThisBuild(tr("Sweep")); });
    QObject::connect(uiPositions->btnWhyVoid, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        auto idx = uiPositions->tblPositions->currentIndex();
        auto p = ctl->positionsModel()->positionAt(idx.row());
        if (p != nullptr) explainVoid(*p);
    });
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
    auto idx = uiPositions->tblPositions->currentIndex();
    auto p = ctl->positionsModel()->positionAt(idx.isValid() ? idx.row() : -1);
    if (p == nullptr) {
        uiPositions->lblVaultAction->setText(ctl->positionsModel()->rowCount(QModelIndex()) == 0
            ? tr("You own no vaults.") : tr("Select a vault to see what can be done with it."));
        uiPositions->btnWhyVoid->setEnabled(false);
        return;
    }
    VaultActions a = vaultActions(*p, ctl->height(), ctl->isAbandoned());
    QString later = tr(" (the action buttons %1)").arg(tr(LATER_RELEASE));
    uiPositions->lblVaultAction->setText(a.text % ((a.release || a.redeem || a.sweep) ? later : QString()));
    uiPositions->btnWhyVoid->setEnabled(actionsEnabled && p->status == YellowbackRpc::Position::STATUS_VOID);
    // Phase 7b-b: the buttons stay disabled; their visibility follows what the row offers
    uiPositions->btnRelease->setVisible(a.release);
    uiPositions->btnRedeem->setVisible(a.redeem || (!a.release && !a.sweep));
    uiPositions->btnSweep->setVisible(a.sweep);
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
    uiClaim->btnClaim->setToolTip(tr("Claiming %1.").arg(tr(LATER_RELEASE)));
    QObject::connect(uiClaim->btnClaim, &QPushButton::clicked, [=, this]() { notInThisBuild(tr("Claiming")); });
}

void YellowbackTab::updateClaimPage() {
    if (ctl == nullptr) return;
    uiClaim->tblClaimable->resizeColumnsToContents();
    int n = ctl->claimableModel()->rowCount(QModelIndex());
    if (n == 0) {
        uiClaim->lblClaimHint->setText(tr("No vault is claimable at the current claim price."));
        return;
    }
    QString hint = tr("%n claimable vault(s). A claim burns the vault's debt from your confirmed YED (%1 available).", "", n)
                       .arg(YellowbackFormat::cents(ctl->confirmedCents()));
    uiClaim->lblClaimHint->setText(hint % " " % tr("The Claim button %1.").arg(tr(LATER_RELEASE)));
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
// Phase 7b-b replaces this page with a confirmation dialog on the Vaults row (§4.8). Until
// then it lists what is redeemable and what a redemption burns.

void YellowbackTab::setupRedeem() {
    QObject::connect(uiRedeem->cmbVault, &QComboBox::currentIndexChanged, [=, this](int) { updateRedeemPage(); });
    QObject::connect(uiRedeem->btnStart, &QPushButton::clicked, [=, this]() { notInThisBuild(tr("Redeem")); });
    uiRedeem->btnStart->setEnabled(false);
    uiRedeem->btnStart->setToolTip(tr("Redeem %1.").arg(tr(LATER_RELEASE)));
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
    if (sel == nullptr) {
        uiRedeem->lblBurn->setText("-");
        uiRedeem->lblCollateral->setText("-");
        uiRedeem->lblRedeemHint->setText(list.isEmpty() ? tr("No vault of yours is redeemable right now.") : QString());
    } else {
        bool isVoid = sel->status == YellowbackRpc::Position::STATUS_VOID;
        uiRedeem->lblBurn->setText(isVoid ? tr("none (VOID vault: Release burns nothing and pays no fee)") : YellowbackFormat::cents(sel->mintedCents));
        uiRedeem->lblCollateral->setText(YellowbackFormat::zec(sel->collateralZat) %
            (isVoid ? QString() : tr("  minus the enforcement fee")));
        if (!isVoid && sel->mintedCents > ctl->confirmedCents())
            uiRedeem->lblRedeemHint->setText(tr("You need %1 of confirmed YED to burn but have %2.")
                .arg(YellowbackFormat::cents(sel->mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())));
        else
            uiRedeem->lblRedeemHint->setText(tr("Redeem %1.").arg(tr(LATER_RELEASE)));
    }
}

// ── Settings page ─────────────────────────────────────────────────────────────────────────

void YellowbackTab::setupSettings() {
    updateSettingsPage();
    QObject::connect(uiSettings->btnSave,   &QPushButton::clicked, [=, this]() { saveSettings(); });
}

void YellowbackTab::updateSettingsPage() {
    auto s = Settings::getInstance();
    uiSettings->chkUnitCents->setChecked(s->getYellowbackUnitCents());
    uiSettings->chkAdvanced->setChecked(s->getYellowbackAdvanced());
    uiSettings->lblRpcVersion->setText(tr("This YecWallet understands Yellowback RPC version %1.").arg(Settings::getYellowbackRpcVersion()) %
        (ctl != nullptr && ctl->isVersionOk() ? tr(" The node matches.") : ""));
}

void YellowbackTab::saveSettings() {
    auto s = Settings::getInstance();
    s->setYellowbackUnitCents(uiSettings->chkUnitCents->isChecked());
    s->setYellowbackAdvanced(uiSettings->chkAdvanced->isChecked());
    if (ctl != nullptr) {
        updateBalances();
        updateOverview();
        ctl->refresh(true);   // re-renders the models in the chosen unit
    }
    updateRedeemPage();
}
