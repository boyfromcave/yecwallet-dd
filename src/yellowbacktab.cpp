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
#include "ui_yellowbacktransactions.h"
#include "ui_yellowbackredeem.h"
#include "ui_yellowbacksettings.h"

using json = nlohmann::json;

YellowbackTab::YellowbackTab(MainWindow* main, QWidget* parent) : QWidget(parent) {
    this->main = main;
    ui = new Ui::YellowbackTab();
    ui->setupUi(this);

    setupPages();

    ui->lblBanner->setText(tr("Yellowback: waiting for the node..."));
    ui->lblBanner->setVisible(true);
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
    uiTx        = new Ui::YellowbackTransactions(); uiTx->setupUi(pages[Transactions]);
    uiRedeem    = new Ui::YellowbackRedeem();       uiRedeem->setupUi(pages[Redeem]);
    uiSettings  = new Ui::YellowbackSettings();     uiSettings->setupUi(pages[Settings]);

    ui->subTabs->addTab(pages[Overview],     tr("Overview"));
    ui->subTabs->addTab(pages[Receive],      tr("Receive"));
    ui->subTabs->addTab(pages[Send],         tr("Send"));
    ui->subTabs->addTab(pages[Mint],         tr("Mint"));
    ui->subTabs->addTab(pages[Vaults],       tr("Vaults"));
    ui->subTabs->addTab(pages[Transactions], tr("Transactions"));
    ui->subTabs->addTab(pages[Redeem],       tr("Redeem"));
    ui->subTabs->addTab(pages[Settings],     tr("Settings"));

    setupOverview();
    setupReceive();
    setupSend();
    setupMint();
    setupPositions();
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

    QObject::connect(ctl, &YellowbackController::availabilityChanged, this, [=, this](bool, const QString&) {
        updateBanner();
        updateMintGate();
    });
    QObject::connect(ctl, &YellowbackController::infoUpdated,         this, [=, this]() { updateBanner(); updateOverview(); });
    QObject::connect(ctl, &YellowbackController::statsUpdated,        this, [=, this]() { updateOverview(); updateMintGate(); });
    QObject::connect(ctl, &YellowbackController::balanceUpdated,      this, [=, this]() { updateBalances(); });
    QObject::connect(ctl, &YellowbackController::positionsUpdated,    this, [=, this]() { updatePositions(); updateRedeemPage(); });
    QObject::connect(ctl, &YellowbackController::transactionsUpdated, this, [=, this]() {
        uiOverview->tblRecent->resizeColumnsToContents();
        uiTx->tblTransactions->resizeColumnsToContents();
    });

    updateBanner();
    updateOverview();
    updateBalances();
    updateMintGate();
    updatePositions();
    updateRedeemPage();
    updateSettingsPage();
}

// ── Banner / enabling ─────────────────────────────────────────────────────────────────────

void YellowbackTab::updateBanner() {
    if (ctl == nullptr) {
        ui->lblBanner->setText(tr("Yellowback: not connected."));
        ui->lblBanner->setVisible(true);
        setActionsEnabled(false);
        return;
    }
    bool avail = ctl->isAvailable();
    ui->lblBanner->setVisible(!avail);
    if (!avail)
        ui->lblBanner->setText(tr("Yellowback unavailable: ") % ctl->unavailableReason());
    setActionsEnabled(avail);
}

void YellowbackTab::setActionsEnabled(bool enabled) {
    actionsEnabled = enabled;
    uiReceive->btnNewAddress->setEnabled(enabled);
    uiSend->btnSend->setEnabled(enabled);
    uiMint->btnMint->setEnabled(enabled && estimateZat >= 0);
    uiPositions->btnRedeem->setEnabled(enabled);
    uiPositions->btnWhyVoid->setEnabled(enabled);
    uiRedeem->btnStart->setEnabled(enabled);
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
    using namespace YellowbackRpc::Stats;
    const json& s = ctl->stats();

    uiOverview->lblNetwork->setText(ctl->network().isEmpty() ? "-" : ctl->network());
    uiOverview->lblIndexHeight->setText(QString::number(ctl->height()) %
        (ctl->isSynced() ? tr(" (synced)") : tr(" (syncing)")));
    uiOverview->lblYecBalance->setText(Settings::getZECUSDDisplayFormat(ctl->yecBalance()));

    if (YellowbackJson::isNull(s, PRICE_MICRO_USD)) {
        uiOverview->lblYecPrice->setText(tr("no fresh price"));
        uiOverview->lblPriceAge->setText("-");
    } else {
        double price = (double)YellowbackJson::toInt(s, PRICE_MICRO_USD) / 1000000.0;
        uiOverview->lblYecPrice->setText("$" % QString::number(price, 'f', 4) % tr(" per YEC"));
        qint64 age = YellowbackJson::toInt(s, PRICE_AGE);
        uiOverview->lblPriceAge->setText(tr("%1 blocks (~%2 min), from height %3")
            .arg(age).arg(age * YellowbackRpc::SECONDS_PER_BLOCK / 60).arg(YellowbackJson::toInt(s, PRICE_HEIGHT)));
    }

    // yed_getprotectionstatus refines the three protection figures when it has answered
    const json& prot = ctl->protection();
    const bool haveProt = prot.is_object() && !prot.empty();
    const json& dca = haveProt && prot.find(YellowbackRpc::Protection::DCA) != prot.end() ? prot[YellowbackRpc::Protection::DCA] : json::object();
    const json& erj = haveProt && prot.find(YellowbackRpc::Protection::ERR) != prot.end() ? prot[YellowbackRpc::Protection::ERR] : json::object();

    qint64 health = YellowbackJson::toInt(s, HEALTH_PCT);
    QString band = YellowbackJson::toStr(dca, YellowbackRpc::Protection::DCA_BAND);
    uiOverview->lblHealth->setText(QString::number(health) % " %" %
        (!band.isEmpty() ? QString("  (" % band % ")") : QString()) %
        (health < 100 ? tr("  — below 100 %: emergency redemption ratio in effect, minting paused") : QString()));
    uiOverview->lblDca->setText(QString::number((double)YellowbackJson::toInt(s, DCA_BPS, 10000) / 10000.0, 'f', 2) % "x");
    bool errActive = haveProt ? YellowbackJson::toBool(erj, YellowbackRpc::Protection::ERR_ACTIVE) : health < 100;
    if (errActive) {
        qint64 mult = YellowbackJson::toInt(erj, YellowbackRpc::Protection::ERR_BURN_MULTIPLIER_BPS, 10000);
        uiOverview->lblErr->setText(tr("active: redemptions burn %1x the minted amount").arg((double)mult / 10000.0, 0, 'f', 2));
    } else {
        uiOverview->lblErr->setText(tr("inactive"));
    }

    QString blocker = ctl->mintBlocker(0);
    uiOverview->lblMintStatus->setText(blocker.isEmpty() ? tr("open") : tr("paused: ") % blocker);

    uiOverview->lblSupply->setText(YellowbackFormat::cents(YellowbackJson::toInt(s, SUPPLY_CENTS)) % " / " %
                                   YellowbackFormat::zec(YellowbackJson::toInt(s, COLLATERAL_ZAT)));
    uiOverview->lblVaults->setText(QString::number(YellowbackJson::toInt(s, ACTIVE_VAULTS)) % " / " %
                                   QString::number(YellowbackJson::toInt(s, VOID_VAULTS)));

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
    // Change floor (rule C20): the node refuses when change would be under the minimum output.
    // This is only a first check on the whole balance; the node's message names the exact
    // workable amounts for the inputs it selected.
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
                uiSend->lblSendHint->setText(tr("The node rejected this address as invalid."));
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
                    if (e.contains(YellowbackRpc::Errors::CHANGE_FLOOR))
                        msg += tr("\n\nThe change left over would be below the minimum output; the node's message above names the amounts that work.");
                    else if (e.contains(YellowbackRpc::Errors::WALLET_LOCKED))
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

void YellowbackTab::setupMint() {
    for (int t = 0; t < YellowbackRpc::TIER_COUNT; t++)
        uiMint->cmbTier->addItem(tr("Tier %1: %2 lock, %3 collateral").arg(t)
            .arg(YellowbackFormat::tierName(t)).arg(YellowbackFormat::tierRatio(t)), t);

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
    uiMint->btnMint->setEnabled(actionsEnabled && blocker.isEmpty() && estimateZat >= 0 && haveAmount && cents == estimateCents);
}

void YellowbackTab::requestEstimate() {
    if (ctl == nullptr) return;
    qint64 cents = 0;
    int tier = uiMint->cmbTier->currentData().toInt();
    if (!parseDollars(uiMint->txtAmount->text(), &cents) || cents <= 0) {
        estimateZat = -1;
        uiMint->lblCollateral->setText("-");
        uiMint->lblUnlock->setText("-");
        uiMint->lblRatio->setText(YellowbackFormat::tierRatio(tier));
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
    ctl->estimateCollateral(cents, tier,
        [=, this](const json& e) {
            if (seq != estimateSeq) return;      // a newer request is in flight
            using namespace YellowbackRpc::Estimate;
            estimateCents = cents;
            estimateTier  = tier;
            estimateZat   = YellowbackJson::toInt(e, REQUIRED_ZAT, -1);
            // The node answers `error` (bad-oracle-price, collateral-out-of-range) with a
            // null requiredZat instead of failing the call.
            QString estError = YellowbackJson::toStr(e, ERROR);
            if (estimateZat < 0 || !estError.isEmpty()) {
                estimateZat = -1;
                uiMint->lblCollateral->setText("-");
                uiMint->lblUnlock->setText("-");
                uiMint->lblRatio->setText(QString::number(YellowbackJson::toInt(e, RATIO_PCT)) % " %");
                uiMint->lblPrice->setText(YellowbackJson::isNull(e, PRICE_MICRO_USD) ? tr("no price") :
                    "$" % QString::number((double)YellowbackJson::toInt(e, PRICE_MICRO_USD) / 1000000.0, 'f', 4) % tr(" per YEC"));
                uiMint->lblMintHint->setText(tr("No estimate: %1").arg(estError.isEmpty() ? tr("the node returned no collateral figure") : estError));
                updateMintGate();
                return;
            }
            uiMint->lblCollateral->setText(YellowbackFormat::zec(estimateZat) %
                tr("  (%1 % ratio x %2 DCA)").arg(YellowbackJson::toInt(e, RATIO_PCT))
                    .arg((double)YellowbackJson::toInt(e, DCA_BPS, 10000) / 10000.0, 0, 'f', 2));
            uiMint->lblRatio->setText(QString::number(YellowbackJson::toInt(e, RATIO_PCT)) % " %");
            uiMint->lblPrice->setText("$" % QString::number((double)YellowbackJson::toInt(e, PRICE_MICRO_USD) / 1000000.0, 'f', 4) % tr(" per YEC"));
            int unlock = (int)YellowbackJson::toInt(e, UNLOCK_HEIGHT);
            uiMint->lblUnlock->setText(YellowbackFormat::heightWithEstimate(unlock, ctl->height()) %
                tr("  — %1 blocks; dates are estimates at 75 s/block").arg(YellowbackJson::toInt(e, LOCK_BLOCKS)));
            double haveZec = ctl->yecBalanceAt(fundingSource());
            if ((double)estimateZat / 100000000.0 > haveZec)
                uiMint->lblMintHint->setText(fundingSource().isEmpty()
                    ? tr("You have %1 of transparent YEC; this mint needs %2 plus the fee. Choose a shielded address above to fund it from there instead.")
                          .arg(Settings::getZECDisplayFormat(haveZec)).arg(YellowbackFormat::zec(estimateZat))
                    : tr("The selected shielded address holds %1 of YEC; this mint needs %2 plus the fee.")
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
    if (ctl == nullptr || !actionsEnabled || estimateZat < 0) return;
    qint64 cents = estimateCents;
    int tier = estimateTier;

    QString blocker = ctl->mintBlocker(cents);
    if (!blocker.isEmpty()) {
        QMessageBox::warning(this, tr("Mint not possible"), blocker);
        updateMintGate();
        return;
    }

    const QString from = fundingSource();
    QString text = tr("Mint %1 of Yellowback?\n\n"
                      "Collateral: about %2 of YEC, locked in a vault until height %3 (%4 lock). "
                      "The exact requirement is fixed when your node signs the transaction, using the price "
                      "and multiplier at that moment, and may differ slightly from this estimate.\n\n"
                      "Until the unlock height the collateral cannot leave the vault. After it, releasing "
                      "the collateral requires burning the vault's debt in YED.\n\n"
                      "The vault's owner key lives only in this node's wallet.dat. Back it up after minting.")
                      .arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::zec(estimateZat))
                      .arg(uiMint->lblUnlock->text().section(' ', 0, 0)).arg(YellowbackFormat::tierName(tier));
    text += from.isEmpty()
        ? tr("\n\nFunded from your transparent balance.")
        : tr("\n\nFunded from the shielded address %1 in the same transaction: the collateral amount and the vault are "
             "visible on the chain (as with any mint); the address it came from is not. Your node makes the Sapling "
             "proofs first, which takes a few seconds per note spent.").arg(from);
    if (QMessageBox::question(this, tr("Confirm mint"), text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    uiMint->btnMint->setEnabled(false);
    uiMint->lblMintStatus->setText(from.isEmpty() ? tr("Minting...") : tr("Minting from the shielded address (making Sapling proofs)..."));
    ctl->mint(cents, tier, from,
        [=, this](const json& r) {
            using namespace YellowbackRpc::MintResult;
            QString warning = YellowbackJson::toStr(r, WARNING);
            QString funded = YellowbackJson::toStr(r, FUNDED_FROM);
            uiMint->lblMintStatus->setText(tr("Mint submitted. txid: %1\nVault: %2, lock height %3, collateral %4 (%6), expires unmined at height %5")
                .arg(YellowbackJson::toStr(r, TXID)).arg(YellowbackJson::toStr(r, VAULT))
                .arg(YellowbackJson::toInt(r, LOCK_HEIGHT)).arg(YellowbackFormat::zec(YellowbackJson::toInt(r, COLLATERAL_ZAT)))
                .arg(YellowbackJson::toInt(r, EXPIRY_HEIGHT))
                .arg(funded == "sapling" ? tr("funded from a shielded address") : tr("funded from transparent YEC")) %
                (warning.isEmpty() ? QString() : "\n" % tr("Node warning: ") % warning));
            uiMint->txtAmount->clear();
            estimateZat = -1;
            Settings::getInstance()->setYellowbackBackupPending(true);
            updateBackupNag();
            ctl->refresh(true);

            auto r2 = QMessageBox::warning(this, tr("Back up wallet.dat now"),
                tr("Your mint was submitted. The key that owns the vault's collateral exists only in this node's "
                   "wallet.dat; it is not derived from a seed phrase and cannot be recovered without a backup.\n\n"
                   "Back it up now?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (r2 == QMessageBox::Yes && main != nullptr && main->ui != nullptr)
                main->ui->actionBackup_wallet_dat->trigger();
            updateMintGate();
        },
        [=, this](const QString& e) {
            QString msg = tr("yed_mint failed: ") % e;
            if (e.contains(YellowbackRpc::Errors::WALLET_LOCKED))
                msg += tr("\n\nUnlock the wallet first (walletpassphrase in the console tab).");
            uiMint->lblMintStatus->setText(msg);
            QMessageBox::critical(this, tr("Mint failed"), msg);
            updateMintGate();
        });
}

// ── Vaults (positions) ────────────────────────────────────────────────────────────────────

void YellowbackTab::setupPositions() {
    uiPositions->tblPositions->horizontalHeader()->setStretchLastSection(true);
    uiPositions->btnRedeem->setEnabled(false);
    uiPositions->btnWhyVoid->setEnabled(false);

    QObject::connect(uiPositions->btnRedeem, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        auto idx = uiPositions->tblPositions->currentIndex();
        auto p = ctl->positionsModel()->positionAt(idx.row());
        if (p == nullptr) return;
        if (!p->canRedeem) {
            QMessageBox::information(this, tr("Not redeemable yet"),
                tr("Vault %1 cannot be redeemed now (status %2, unlock height %3).")
                    .arg(p->vaultTxid).arg(p->status).arg(YellowbackFormat::heightWithEstimate(p->unlockHeight, ctl->height())));
            return;
        }
        startRedemption(p->vaultTxid);
    });
    QObject::connect(uiPositions->btnWhyVoid, &QPushButton::clicked, [=, this]() {
        if (ctl == nullptr) return;
        auto idx = uiPositions->tblPositions->currentIndex();
        auto p = ctl->positionsModel()->positionAt(idx.row());
        if (p != nullptr) explainVoid(p->vaultTxid);
    });
}

void YellowbackTab::updatePositions() {
    if (ctl == nullptr) return;
    uiPositions->tblPositions->resizeColumnsToContents();

    // Selection-dependent buttons are re-evaluated whenever the selection changes
    auto sel = uiPositions->tblPositions->selectionModel();
    if (sel != nullptr) {
        QObject::disconnect(sel, nullptr, this, nullptr);
        QObject::connect(sel, &QItemSelectionModel::currentRowChanged, this, [=, this](const QModelIndex& cur, const QModelIndex&) {
            auto p = ctl->positionsModel()->positionAt(cur.row());
            bool have = p != nullptr;
            bool pend = have && p->pending;
            uiPositions->btnRedeem->setEnabled(actionsEnabled && have && p->canRedeem && !pend);
            uiPositions->btnWhyVoid->setEnabled(actionsEnabled && have && p->status == YellowbackRpc::Position::STATUS_VOID);
        });
    }
}

void YellowbackTab::explainVoid(const QString& vaultTxid) {
    if (ctl == nullptr) return;
    ctl->getTxInfo(vaultTxid,
        [=, this](const json& info) {
            QString verdict = YellowbackJson::toStr(info, YellowbackRpc::Transaction::VERDICT, tr("(no verdict returned)"));
            QMessageBox::information(this, tr("Why this vault is VOID"),
                tr("The Yellowback index recorded the mint %1 with verdict:\n\n%2\n\n"
                   "A VOID mint created no YED. Its collateral is still locked until the unlock height "
                   "and is then returned with no burn required (redeem it like any other vault; the required burn is zero).")
                   .arg(vaultTxid).arg(verdict));
        },
        [=, this](const QString& e) {
            QMessageBox::warning(this, tr("yed_gettxinfo failed"), e);
        });
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

void YellowbackTab::setupRedeem() {
    QObject::connect(uiRedeem->cmbVault, &QComboBox::currentIndexChanged, [=, this](int) { updateRedeemPage(); });
    QObject::connect(uiRedeem->btnStart, &QPushButton::clicked, [=, this]() {
        QString vault = uiRedeem->cmbVault->currentData().toString();
        if (vault.isEmpty()) {
            uiRedeem->lblRedeemHint->setText(tr("No vault of yours is redeemable right now."));
            return;
        }
        startRedemption(vault);
    });
}

void YellowbackTab::updateRedeemPage() {
    if (ctl == nullptr) return;
    QString keep = uiRedeem->cmbVault->currentData().toString();
    auto list = ctl->positionsModel()->redeemable();

    uiRedeem->cmbVault->blockSignals(true);
    uiRedeem->cmbVault->clear();
    for (auto& p : list) {
        uiRedeem->cmbVault->addItem(tr("%1  —  %2 minted, %3 collateral, %4")
            .arg(p.vaultTxid.left(16) % "...").arg(YellowbackFormat::cents(p.mintedCents))
            .arg(YellowbackFormat::zec(p.collateralZat)).arg(p.status), p.vaultTxid);
    }
    int i = uiRedeem->cmbVault->findData(keep);
    if (i >= 0) uiRedeem->cmbVault->setCurrentIndex(i);
    uiRedeem->cmbVault->blockSignals(false);

    QString vault = uiRedeem->cmbVault->currentData().toString();
    const YellowbackPosition* sel = nullptr;
    for (auto& p : list) if (p.vaultTxid == vault) { sel = &p; break; }
    if (sel == nullptr) {
        uiRedeem->lblBurn->setText("-");
        uiRedeem->lblCollateral->setText("-");
        uiRedeem->lblRedeemHint->setText(list.isEmpty() ? tr("No vault of yours is redeemable right now.") : QString());
    } else {
        uiRedeem->lblBurn->setText(YellowbackFormat::cents(sel->requiredBurnCents) %
            (sel->requiredBurnCents > sel->mintedCents ? tr("  (more than minted: emergency redemption ratio)") : ""));
        uiRedeem->lblCollateral->setText(YellowbackFormat::zec(sel->collateralZat));
        if (sel->pending)
            uiRedeem->lblRedeemHint->setText(tr("A redemption of this vault is already in progress (see Vaults)."));
        else if (sel->requiredBurnCents > ctl->confirmedCents())
            uiRedeem->lblRedeemHint->setText(tr("You need %1 of confirmed YED to burn but have %2.")
                .arg(YellowbackFormat::cents(sel->requiredBurnCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())));
        else
            uiRedeem->lblRedeemHint->clear();
    }
}

void YellowbackTab::startRedemption(const QString& vaultTxid) {
    if (ctl == nullptr || !actionsEnabled) return;
    const YellowbackPosition* sel = nullptr;
    for (int r = 0; ; r++) {
        auto p = ctl->positionsModel()->positionAt(r);
        if (p == nullptr) break;
        if (p->vaultTxid == vaultTxid) { sel = p; break; }
    }
    if (sel == nullptr) return;
    if (sel->pending) {
        QMessageBox::information(this, tr("Already in progress"), tr("A redemption of this vault is already pending."));
        return;
    }

    // Phase 0 (plan §6, V24): the federation prototype's co-signing wizard is gone and the one-step
    // redemption (yed_redeem builds, signs and broadcasts) arrives with the v2 wallet screens
    // (Phase 7b-b). Until then this build cannot redeem.
    QMessageBox::information(this, tr("Not available in this build"),
        tr("Redeeming vault %1 is not available in this build of YecWallet. Redemption is being "
           "rebuilt as a single step handled by your own node; a later release adds it back.")
           .arg(vaultTxid));
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
