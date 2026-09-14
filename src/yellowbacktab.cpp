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
    delete uiAttestors;
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
    uiAttestors = new Ui::YellowbackAttestors();    uiAttestors->setupUi(pages[Attestors]);

    ui->subTabs->addTab(pages[Overview],     tr("Overview"));
    ui->subTabs->addTab(pages[Receive],      tr("Receive"));
    ui->subTabs->addTab(pages[Send],         tr("Send"));
    ui->subTabs->addTab(pages[Mint],         tr("Mint"));
    ui->subTabs->addTab(pages[Vaults],       tr("Vaults"));
    ui->subTabs->addTab(pages[Claim],        tr("Claim"));
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
    uiAttestors->tblAttestors->setModel(ctl->attestorsModel());

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
    updateAttestors();
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
        uiPositions->btnSweep->setEnabled(false);
        uiClaim->btnClaim->setEnabled(false);
        uiRedeem->btnStart->setEnabled(false);
    } else if (ctl != nullptr) {
        updateMintGate();
        updateVaultButtons();
        updateClaimPage();
        updateRedeemPage();
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

    // v3: the two pool cross-section prices and the arming state at the tip, from yed_getprice
    const json& pr = ctl->price();
    if (pr.empty()) {
        uiOverview->lblPoolPrices->setText("-");
        uiOverview->lblAttestation->setText("-");
    } else {
        uiOverview->lblPoolPrices->setText(YellowbackFormat::priceOrUndefined(pr, Price::X_MINT) % " / " %
                                           YellowbackFormat::priceOrUndefined(pr, Price::X_CLAIM));
        QString st = YellowbackJson::toStr(pr, Price::ATTEST_STATUS, "-");
        if (YellowbackJson::toBool(pr, Price::ARMED))
            uiOverview->lblAttestation->setText(tr("%1 — mints and claims use attested prices").arg(st));
        else if (st == Attest::STATUS_ARMED)
            uiOverview->lblAttestation->setText(tr("%1 — attestation layer disabled by parameter set").arg(st));
        else
            uiOverview->lblAttestation->setText(tr("%1 — prices come from pool quotes alone").arg(st));
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
// MINTPOL-1 gate. The confirmation adds the enforcement fee and payee (yed_getfeepayee) and
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
    uiMint->btnMint->setEnabled(false);
    uiMint->lblMintPageStatus->setText(tr("Your own node builds, signs and sends the mint. Back up wallet.dat afterwards: the vault's key exists only there."));
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
    bool estimateCurrent = haveAmount && estimateZat >= 0 && estimateCents == cents &&
                           estimateLockBlocks == uiMint->cmbTier->currentData().toInt();
    uiMint->btnMint->setEnabled(actionsEnabled && blocker.isEmpty() && estimateCurrent);
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

void YellowbackTab::doMint() {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    qint64 cents = 0;
    int lockBlocks = uiMint->cmbTier->currentData().toInt();
    if (!parseDollars(uiMint->txtAmount->text(), &cents) || cents <= 0 || lockBlocks <= 0) {
        uiMint->lblMintHint->setText(tr("Enter an amount in dollars and choose a lock length."));
        return;
    }
    QString blocker = ctl->mintBlocker(cents);
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
            const qint64 required   = YellowbackJson::toInt(e, Estimate::REQUIRED_ZAT);
            const int    lockHeight = (int)YellowbackJson::toInt(e, Estimate::LOCK_HEIGHT);
            const int    claimHeight= (int)YellowbackJson::toInt(e, Estimate::CLAIM_HEIGHT);
            const int    refHeight  = (int)YellowbackJson::toInt(e, Estimate::REF_HEIGHT, ctl->refHeightNow());
            const QString termClass = YellowbackJson::toStr(e, Estimate::TERM_CLASS, cls.name);
            const QString ratio = tr("%1 (base %2 × σ %3)")
                .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, Estimate::MIN_RATIO_BPS)))
                .arg(YellowbackFormat::bpsAsPercent(YellowbackJson::toInt(e, Estimate::BASE_RATIO_BPS)))
                .arg(YellowbackFormat::bpsAsMultiplier(YellowbackJson::toInt(e, Estimate::SIGMA_MULT_BPS, 10000)));
            const QString price = YellowbackFormat::priceOrUndefined(e, Estimate::P_MINT);

            auto ask = [=, this](const QString& feeLine) {
                QString text = tr("Mint %1 of YED against %2 of YEC locked in a new vault.\n\n"
                                  "Lock: %3 blocks (class %4, about %5 days). Collateral can leave the vault from height %6 on (%7); "
                                  "its claim height is %8.\n"
                                  "Collateral ratio: %9 at a mint price of %10 per YEC (reference height %11).\n"
                                  "%12\n"
                                  "Funded from: %13.\n\n"
                                  "Every Ycash node enforces that the collateral cannot leave the vault before its lock height, and that only your key can spend it before the claim height. "
                                  "The mining pools that run the Yellowback module enforce that it is released only against the burn of %1 of YED.\n\n"
                                  "Back up wallet.dat after this mint: the vault's key is created now and exists only in that file.")
                    .arg(YellowbackFormat::cents(cents)).arg(YellowbackFormat::zec(required))
                    .arg(lockBlocks).arg(termClass).arg((qint64)lockBlocks * SECONDS_PER_BLOCK / 86400)
                    .arg(lockHeight).arg(YellowbackFormat::estimateDate(lockHeight, ctl->height()).toString("yyyy-MM-dd") % tr(", estimated"))
                    .arg(claimHeight).arg(ratio).arg(price).arg(refHeight).arg(feeLine)
                    .arg(from.isEmpty() ? tr("your transparent YEC") : tr("shielded address %1").arg(from));
                if (!confirm(tr("Confirm mint"), text)) {
                    updateMintGate();
                    uiMint->lblMintPageStatus->clear();
                    return;
                }
                uiMint->lblMintPageStatus->setText(tr("Minting..."));
                ctl->mint(cents, lockBlocks, from,
                    [=, this](const json& r) {
                        Settings::getInstance()->setYellowbackBackupPending(true);
                        updateBackupNag();
                        QString payee = YellowbackJson::isNull(r, MintResult::PAYEE) ? tr("none") : YellowbackJson::toStr(r, MintResult::PAYEE);
                        QString summary = tr("Minted %1 of YED.\ntxid %2\nvault %3 (class %4)\ncollateral %5, lock height %6, claim height %7\nenforcement fee %8 to %9\nfunded from %10")
                            .arg(YellowbackFormat::cents(cents)).arg(YellowbackJson::toStr(r, MintResult::TXID))
                            .arg(YellowbackJson::toStr(r, MintResult::VAULT)).arg(YellowbackJson::toStr(r, MintResult::TERM_CLASS))
                            .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, MintResult::COLLATERAL_ZAT)))
                            .arg(YellowbackJson::toInt(r, MintResult::LOCK_HEIGHT)).arg(YellowbackJson::toInt(r, MintResult::CLAIM_HEIGHT))
                            .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, MintResult::FEE_ZAT))).arg(payee)
                            .arg(YellowbackJson::toStr(r, MintResult::FUNDED_FROM));
                        QString warning = YellowbackJson::toStr(r, MintResult::WARNING);
                        if (!warning.isEmpty()) summary += "\n\n" % tr("Node warning: ") % warning;
                        summary += "\n\n" % tr("The YED arrives once the transaction is mined. Back up wallet.dat now.");
                        uiMint->lblMintPageStatus->setText(tr("Minted. txid: ") % YellowbackJson::toStr(r, MintResult::TXID));
                        uiMint->txtAmount->clear();
                        notice(tr("Mint sent"), summary);
                        ctl->refresh(true);
                    },
                    [=, this](const QString& e) {
                        updateMintGate();
                        uiMint->lblMintPageStatus->setText(tr("yed_mint failed: ") % e);
                        failed("yed_mint", e);
                    });
            };

            // The enforcement fee and its payee (FEE-1, FEE-W) for this collateral at the
            // reference height; under FEE-0 the node refuses with fee-no-eligible-payee and the
            // mint carries no fee output.
            ctl->getFeePayee(refHeight, required,
                [=, this](const json& f) {
                    const json& def = YellowbackJson::obj(f, FeePayee::DEFAULT);
                    QString payee = YellowbackJson::has(f, FeePayee::PREFERRED) && !YellowbackJson::isNull(f, FeePayee::PREFERRED)
                        ? YellowbackJson::toStr(f, FeePayee::PREFERRED)
                        : YellowbackJson::toStr(def, FeePayeeDefault::PAYOUT_ADDRESS);
                    ask(tr("Enforcement fee: %1 of YEC, paid from the collateral to the pool %2 (a pool that published a price quote in the 100 blocks up to the reference height).")
                            .arg(YellowbackFormat::zec(YellowbackJson::toInt(f, FeePayee::FEE_ZAT))).arg(payee));
                },
                [=, this](const QString& e) {
                    if (e.startsWith(Errors::FEE_NO_ELIGIBLE_PAYEE, Qt::CaseInsensitive))
                        ask(tr("Enforcement fee: none (no pool published a price quote in the payee window, so the mint carries no fee output)."));
                    else
                        ask(tr("Enforcement fee: could not be estimated (%1); the node reports the fee it pays after the mint.").arg(e));
                });
        },
        [=, this](const QString& e) {
            updateMintGate();
            uiMint->lblMintPageStatus->setText(tr("yed_estimatecollateral failed: ") % e);
            failed("yed_estimatecollateral", e);
        });
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
        // v3: a standing claim notice (NOT-1) opens the emergency claim clause after it persists
        if (p.noticed)
            a.text += " " % tr("A claim notice stands against it (confirmed at height %1): from reference height %2 a claim may take the collateral under the emergency clause. Redeem or add collateral before then.")
                                .arg(p.noticeHeight).arg(p.emergencyOpenAt);
        else if (p.canNotice)
            a.text += " " % tr("Under the attested prices this node holds it is below the emergency ratio: a claim notice could be posted against it.");
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
    uiPositions->btnRelease->setToolTip(tr("Return a VOID vault's collateral: no YED burned, no fee (yed_redeem)."));
    uiPositions->btnRedeem->setToolTip(tr("Burn the vault's YED and take the collateral back, minus the enforcement fee (yed_redeem)."));
    uiPositions->btnSweep->setToolTip(tr("Under abandonment only: move the collateral out with no burn, leaving the YED unbacked (yed_sweep)."));

    auto selected = [=, this]() -> const YellowbackPosition* {
        if (ctl == nullptr) return nullptr;
        auto idx = uiPositions->tblPositions->currentIndex();
        return ctl->positionsModel()->positionAt(idx.isValid() ? idx.row() : -1);
    };
    QObject::connect(uiPositions->btnRelease, &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) redeemVault(*p); });
    QObject::connect(uiPositions->btnRedeem,  &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) redeemVault(*p); });
    QObject::connect(uiPositions->btnSweep,   &QPushButton::clicked, [=, this]() { auto p = selected(); if (p) sweepVault(*p); });
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
    uiPositions->lblVaultAction->setText(a.text);
    uiPositions->btnWhyVoid->setEnabled(actionsEnabled && p->status == YellowbackRpc::Position::STATUS_VOID);
    uiPositions->btnRelease->setVisible(a.release);
    uiPositions->btnRedeem->setVisible(a.redeem || (!a.release && !a.sweep));
    uiPositions->btnSweep->setVisible(a.sweep);
    uiPositions->btnRelease->setEnabled(actionsEnabled && a.release);
    uiPositions->btnRedeem->setEnabled(actionsEnabled && a.redeem);
    uiPositions->btnSweep->setEnabled(actionsEnabled && a.sweep);
}

// ── Redeem / Release / Sweep dialogs (one confirmation, one call; V24, L10, L14) ──────────

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
                    tr("txid %1\nYED burned: %2%7\nenforcement fee: %3 to %4\ncollateral out: %5 to %6\n\nThe vault closes when the transaction is mined.")
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
               "No YED is burned and no fee is paid: this vault never carried a debt (its mint was recorded as %2). "
               "The full %3 of YEC returns to %4.\n\n"
               "Do it before height %5: after that the vault's claim path is open to anyone.")
            .arg(p.txid).arg(p.voidReason.isEmpty() ? tr("void") : p.voidReason)
            .arg(YellowbackFormat::zec(p.collateralZat)).arg(dest).arg(p.claimHeight));
        return;
    }

    auto ask = [=, this](qint64 feeZat, const QString& feeLine) {
        run(tr("Redeem vault %1.\n\n"
               "Burn: %2 of YED from this wallet (%3 confirmed).\n"
               "%4\n"
               "Collateral out: about %5 of YEC to %6.\n\n"
               "Your own node builds the transaction, checks it against the enforcement rules, signs it and sends it; "
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
            ask(fee, tr("Enforcement fee: %1 of YEC from the collateral to the pool %2.").arg(YellowbackFormat::zec(fee)).arg(payee));
        },
        [=, this](const QString& e) {
            if (e.startsWith(Errors::FEE_NO_ELIGIBLE_PAYEE, Qt::CaseInsensitive))
                ask(0, tr("Enforcement fee: none (no pool published a price quote in the payee window)."));
            else
                ask(0, tr("Enforcement fee: could not be estimated (%1); the node reports it after the redemption.").arg(e));
        });
}

void YellowbackTab::sweepVault(const YellowbackPosition& p, const QString& to) {
    if (ctl == nullptr || !actionsEnabled) return;
    using namespace YellowbackRpc;
    const QString dest = to.isEmpty() ? tr("a fresh transparent address of this wallet") : to;
    // The L10 acknowledgement is what the node requires as its second argument; the dialog
    // carries it verbatim and the controller sends exactly that string.
    QString text = tr("Sweep vault %1.\n\n"
                      "The chain shows Yellowback enforcement abandoned. Sweeping moves the %2 of YEC collateral to %3 with no YED burned and no fee paid. "
                      "The %4 of YED minted against this vault stay in circulation unbacked from then on.\n\n"
                      "Sweep before height %5: after it the vault's claim path is open to anyone and, with nobody enforcing, whoever mines first takes the collateral.\n\n"
                      "By confirming you state: \"%6\"")
        .arg(p.txid).arg(YellowbackFormat::zec(p.collateralZat)).arg(dest).arg(YellowbackFormat::cents(p.mintedCents))
        .arg(p.sweepBefore > 0 ? p.sweepBefore : p.claimHeight).arg(SWEEP_ACKNOWLEDGEMENT);
    if (!confirm(tr("Confirm sweep"), text)) return;
    ctl->sweep(p.txid, to,
        [=, this](const json& r) {
            QString hex = YellowbackJson::toStr(r, SweepResult::HEX);
            if (!hex.isEmpty()) QGuiApplication::clipboard()->setText(hex);
            notice(tr("Sweep sent"),
                tr("txid %1\ncollateral out: %2 to %3\nYED now unbacked: %4\n\n"
                   "The raw transaction hex is on the clipboard so you can also submit it to any other node (sendrawtransaction).")
                    .arg(YellowbackJson::toStr(r, SweepResult::TXID))
                    .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, SweepResult::COLLATERAL_OUT)))
                    .arg(YellowbackJson::toStr(r, SweepResult::TO))
                    .arg(YellowbackFormat::cents(YellowbackJson::toInt(r, SweepResult::UNBACKED_CENTS))));
            ctl->refresh(true);
        },
        [=, this](const QString& e) { failed("yed_sweep", e); });
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
    uiClaim->btnClaim->setToolTip(tr("Burn the vault's debt from your YED and take its collateral, minus the enforcement fee (yed_claim)."));
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
    uiClaim->lblClaimHint->setText(tr("%n claimable vault(s). A claim burns the vault's debt from your confirmed YED (%1 available) and pays you its collateral minus the enforcement fee. Select a row and press Claim.", "", n)
                                       .arg(YellowbackFormat::cents(ctl->confirmedCents())));
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
    QString txid = c.vault.section(':', 0, 0);
    QString text = tr("Claim vault %1 (owner %2).\n\n"
                      "Burn: %3 of YED from this wallet (%4 confirmed).\n"
                      "Enforcement fee: %5 of YEC from the collateral to a pool that published a price quote.\n"
                      "You receive: about %6 of YEC (collateral %7 minus the fee) to %8.\n\n"
                      "The vault is past its claim height (%9) and underwater at the claim price of %10 per YEC (underwater below %11). "
                      "Your own node checks the claim against the enforcement rules before it signs and sends it.")
        .arg(txid).arg(c.ownerAddress).arg(YellowbackFormat::cents(c.mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents()))
        .arg(YellowbackFormat::zec(c.feeZat)).arg(YellowbackFormat::zec(c.collateralZat - c.feeZat)).arg(YellowbackFormat::zec(c.collateralZat)).arg(dest)
        .arg(c.claimHeight).arg(YellowbackFormat::price(c.pClaim)).arg(YellowbackFormat::price(c.underwaterAt));
    if (!confirm(tr("Confirm claim"), text)) return;
    ctl->claim(txid, to,
        [=, this](const json& r) {
            QString payee = YellowbackJson::isNull(r, RedeemResult::PAYEE) ? tr("none") : YellowbackJson::toStr(r, RedeemResult::PAYEE);
            notice(tr("Claim sent"),
                tr("txid %1\nYED burned: %2%7\nenforcement fee: %3 to %4\ncollateral out: %5 to %6")
                    .arg(YellowbackJson::toStr(r, RedeemResult::TXID))
                    .arg(YellowbackFormat::cents(YellowbackJson::toInt(r, RedeemResult::BURNED_CENTS)))
                    .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RedeemResult::FEE_ZAT))).arg(payee)
                    .arg(YellowbackFormat::zec(YellowbackJson::toInt(r, RedeemResult::COLLATERAL_OUT)))
                    .arg(YellowbackJson::toStr(r, RedeemResult::TO))
                    .arg(extraBurnLine(r)));
            ctl->refresh(true);
        },
        [=, this](const QString& e) { failed("yed_claim", e); });
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
            (isVoid ? QString() : tr("  minus the enforcement fee")));
        bool enough = isVoid || sel->mintedCents <= ctl->confirmedCents();
        uiRedeem->lblRedeemHint->setText(enough ? QString() : tr("You need %1 of confirmed YED to burn but have %2.")
                .arg(YellowbackFormat::cents(sel->mintedCents)).arg(YellowbackFormat::cents(ctl->confirmedCents())));
        uiRedeem->btnStart->setText(isVoid ? tr("Release…") : tr("Redeem…"));
        uiRedeem->btnStart->setEnabled(actionsEnabled && enough);
    }
}

// ── Attestors (v3, read-only) ─────────────────────────────────────────────────────────────

void YellowbackTab::setupAttestors() {
    uiAttestors->tblAttestors->horizontalHeader()->setStretchLastSection(true);
    uiAttestors->lblArming->setText(tr("Waiting for the node..."));
}

void YellowbackTab::updateAttestors() {
    if (ctl == nullptr) return;
    QString banner = YellowbackController::describeAttest(ctl->attest());
    uiAttestors->lblArming->setText(banner.isEmpty() ? tr("The node reports no attestation state (an rpcversion 2 node, or not answered yet).") : banner);
    uiAttestors->tblAttestors->resizeColumnsToContents();
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
    // build its bundle while the layer is armed. The launcher is A5-b; the status is read here.
    uiSettings->lblSubscriberStatus->setText(subscriberStatus(subscriber) %
        (subscriber == nullptr ? tr(" — the subscriber is started beside the node; until it runs, this node's attestation pool stays empty.") : QString()));
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
