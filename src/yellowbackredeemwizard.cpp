#include "yellowbackredeemwizard.h"
#include "yellowbackcontroller.h"
#include "yellowbackrpc.h"
#include "connection.h"
#include "settings.h"

#include <QComboBox>
#include <QFormLayout>
#include <QVBoxLayout>

using json = nlohmann::json;

YellowbackRedeemWizard::YellowbackRedeemWizard(YellowbackController* ctl, const YellowbackPosition& position, QWidget* parent)
    : QWizard(parent), ctl(ctl), pos(position) {
    setWindowTitle(tr("Redeem vault ") % pos.vaultTxid.left(16) % "...");
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::NoBackButtonOnLastPage, true);
    setOption(QWizard::NoCancelButtonOnLastPage, true);
    setButtonText(QWizard::CancelButton, tr("Abort"));
    setMinimumSize(640, 480);

    for (auto& url : Settings::getInstance()->getYellowbackEndpoints()) {
        Operator op;
        op.url = url;
        op.status = tr("waiting");
        operators.append(op);
    }

    buildReviewPage();
    buildCollectPage();
    buildSubmitPage();

    timer = new QTimer(this);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, [=, this]() { tick(); });

    QObject::connect(this, &QWizard::currentIdChanged, [=, this](int id) {
        if (id == CollectPage && !collecting && !redeemIssued) startRedeem();
        if (id == SubmitPage) doSubmit();
    });
}

YellowbackRedeemWizard::~YellowbackRedeemWizard() {
    timer->stop();
}

// ── Page 1: review ────────────────────────────────────────────────────────────────────────

void YellowbackRedeemWizard::buildReviewPage() {
    pgReview = new YellowbackWizardPage(this);
    pgReview->setTitle(tr("1. Review"));
    pgReview->setSubTitle(tr("What this redemption will do"));
    auto layout = new QVBoxLayout(pgReview);
    lblReview = new QLabel(pgReview);
    lblReview->setWordWrap(true);
    lblReview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(lblReview);

    // Plan I2: the collateral destination. A fresh transparent address by default; any of the
    // wallet's Sapling addresses (paid as a Sapling output, straight into the shielded pool) or
    // transparent addresses otherwise.
    auto form = new QFormLayout();
    cmbDestination = new QComboBox(pgReview);
    cmbDestination->addItem(tr("A fresh transparent address of this wallet (default)"), QString());
    for (const auto& a : ctl->saplingAddresses())
        cmbDestination->addItem(tr("Shielded %1…%2 (balance %3)").arg(a.first.left(14)).arg(a.first.right(6)).arg(Settings::getZECDisplayFormat(a.second)), a.first);
    for (const auto& a : ctl->transparentAddresses())
        cmbDestination->addItem(tr("Transparent %1…%2 (balance %3)").arg(a.first.left(8)).arg(a.first.right(6)).arg(Settings::getZECDisplayFormat(a.second)), a.first);
    cmbDestination->setToolTip(tr("Where the released collateral goes. Choosing a shielded (ys1…) address sends it straight into the shielded pool in the redemption itself; nothing is unshielded or re-shielded afterwards."));
    form->addRow(tr("Return collateral to"), cmbDestination);
    layout->addLayout(form);
    layout->addStretch();
    setPage(ReviewPage, pgReview);

    QString text = tr("<b>Vault</b> %1<br>"
                      "<b>Status</b> %2<br>"
                      "<b>Minted</b> %3<br>"
                      "<b>YED to burn now</b> %4%5<br>"
                      "<b>Collateral returned to you</b> %6<br><br>")
                      .arg(pos.vaultTxid).arg(pos.status).arg(YellowbackFormat::cents(pos.mintedCents))
                      .arg(YellowbackFormat::cents(pos.requiredBurnCents))
                      .arg(pos.requiredBurnCents > pos.mintedCents ? tr(" (more than minted: emergency redemption ratio in effect)") : "")
                      .arg(YellowbackFormat::zec(pos.collateralZat));
    text += tr("Pressing Next asks your node to build and sign the redemption (yed_redeem). From then on the "
               "Yellowback to be burned is reserved until the redemption is submitted or aborted. The signed "
               "transaction is then sent to the federation operators for co-signatures, and finally your node "
               "checks every signature before broadcasting it. The whole thing must finish within %1 blocks "
               "(about %2 minutes); otherwise you abort and start over.<br><br>")
               .arg(YellowbackRpc::REDEEM_DEADLINE).arg(YellowbackRpc::REDEEM_DEADLINE * YellowbackRpc::SECONDS_PER_BLOCK / 60);
    if (operators.isEmpty()) {
        text += tr("<b>No operator endpoints are configured.</b> Add them on the Yellowback Settings page first.");
        lblReview->setText(text);
        pgReview->setOk(false);
        return;
    }
    text += tr("Operators configured: %1<br>Roster: checking...").arg(operators.size());
    lblReview->setText(text);
    pgReview->setOk(false);

    ctl->getRoster(
        [=, this](const json& r) {
            using namespace YellowbackRpc::Roster;
            rosterK = (int)YellowbackJson::toInt(r, K);
            rosterN = (int)YellowbackJson::toInt(r, N);
            QString t = text;
            t.replace(tr("Roster: checking..."),
                      tr("Roster #%1: %2 of %3 signatures needed.").arg(YellowbackJson::toInt(r, INDEX)).arg(rosterK).arg(rosterN));
            if (operators.size() < rosterK) {
                t += tr("<br><b>Only %1 endpoint(s) configured but %2 signatures are needed.</b> Add more on the Settings page.")
                        .arg(operators.size()).arg(rosterK);
                lblReview->setText(t);
                pgReview->setOk(false);
                return;
            }
            if (pos.requiredBurnCents > ctl->confirmedCents()) {
                t += tr("<br><b>You have %1 of confirmed YED but %2 must be burned.</b>")
                        .arg(YellowbackFormat::cents(ctl->confirmedCents())).arg(YellowbackFormat::cents(pos.requiredBurnCents));
                lblReview->setText(t);
                pgReview->setOk(false);
                return;
            }
            lblReview->setText(t);
            pgReview->setOk(true);
        },
        [=, this](const QString& e) {
            lblReview->setText(text % tr("<br><b>yed_getroster failed:</b> ") % e);
            pgReview->setOk(false);
        });
}

// ── Page 2: collect co-signatures ─────────────────────────────────────────────────────────

void YellowbackRedeemWizard::buildCollectPage() {
    pgCollect = new YellowbackWizardPage(this);
    pgCollect->setTitle(tr("2. Collect co-signatures"));
    pgCollect->setSubTitle(tr("Your node has signed; the federation operators add theirs"));
    pgCollect->setCommitPage(true);
    auto layout = new QVBoxLayout(pgCollect);

    lblProgress = new QLabel(tr("Building the redemption on your node..."), pgCollect);
    progress    = new QProgressBar(pgCollect);
    progress->setRange(0, 1);
    progress->setValue(0);
    lblDeadline = new QLabel("", pgCollect);
    lstOperators = new QListWidget(pgCollect);
    lblCollectStatus = new QLabel("", pgCollect);
    lblCollectStatus->setWordWrap(true);
    lblCollectStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    txtHex = new QPlainTextEdit(pgCollect);
    txtHex->setReadOnly(true);
    txtHex->setMaximumHeight(90);
    txtHex->setVisible(Settings::getInstance()->getYellowbackAdvanced());

    layout->addWidget(lblProgress);
    layout->addWidget(progress);
    layout->addWidget(lblDeadline);
    layout->addWidget(lstOperators);
    layout->addWidget(lblCollectStatus);
    layout->addWidget(txtHex);
    setPage(CollectPage, pgCollect);
    refreshOperatorList();
}

void YellowbackRedeemWizard::refreshOperatorList() {
    lstOperators->clear();
    for (auto& op : operators)
        lstOperators->addItem(op.url % "  —  " % op.status);
    if (rosterK > 0) {
        progress->setRange(0, rosterK);
        progress->setValue(qMin(signatures, rosterK));
        lblProgress->setText(tr("%1 of %2 co-signatures collected").arg(signatures).arg(rosterK));
    }
    if (txtHex->isVisible()) txtHex->setPlainText(hex);
}

QString YellowbackRedeemWizard::deadlineText() const {
    int left = deadlineHeight - ctl->height();
    if (left < 0) return tr("Deadline passed (height %1).").arg(deadlineHeight);
    return tr("Submit by height %1 (inclusive): %2 blocks left (about %3 minutes). Current index height %4.")
            .arg(deadlineHeight).arg(left).arg(left * YellowbackRpc::SECONDS_PER_BLOCK / 60).arg(ctl->height());
}

void YellowbackRedeemWizard::startRedeem() {
    collecting = true;
    destination = cmbDestination != nullptr ? cmbDestination->currentData().toString() : QString();
    lblCollectStatus->setText(destination.startsWith("ys") || destination.startsWith("ytestsapling") || destination.startsWith("yregtestsapling")
        ? tr("Calling yed_redeem (making the Sapling proof for the collateral output)...")
        : tr("Calling yed_redeem..."));
    ctl->redeem(pos.vaultTxid, destination,
        [=, this](const json& r) {
            using namespace YellowbackRpc::RedeemResult;
            hex               = YellowbackJson::toStr(r, HEX);
            collateralTo      = YellowbackJson::toStr(r, COLLATERAL_TO);
            shieldedCollateral = r.find(SHIELDED) != r.end() && r[SHIELDED].is_boolean() && r[SHIELDED].get<bool>();
            expiryHeight      = (int)YellowbackJson::toInt(r, EXPIRY_HEIGHT);
            requiredBurnCents = YellowbackJson::toInt(r, REQUIRED_BURN_CENTS, pos.requiredBurnCents);
            burnCents         = YellowbackJson::toInt(r, BURN_CENTS, requiredBurnCents);
            deadlineHeight    = (int)YellowbackJson::toInt(r, DEADLINE_HEIGHT, ctl->deadlineHeight(expiryHeight));
            redeemIssued      = true;
            ctl->addPendingRedemption(pos.vaultTxid, deadlineHeight);

            // The node's roster for this vault wins over the one shown on the review page
            if (r.find(ROSTER) != r.end() && r[ROSTER].is_object()) {
                rosterK = (int)YellowbackJson::toInt(r[ROSTER], YellowbackRpc::Roster::K, rosterK);
                rosterN = (int)YellowbackJson::toInt(r[ROSTER], YellowbackRpc::Roster::N, rosterN);
            }
            if (rosterK <= 0) rosterK = 1;

            lblCollectStatus->setText(tr("Your node signed the redemption (burning %1, expiry height %2; collateral to %3%4). Contacting operators...")
                .arg(YellowbackFormat::cents(burnCents)).arg(expiryHeight)
                .arg(collateralTo.isEmpty() ? tr("a fresh transparent address") : collateralTo)
                .arg(shieldedCollateral ? tr(", as a shielded output") : QString()));
            lblDeadline->setText(deadlineText());
            refreshOperatorList();
            timer->start();
            postNext();
        },
        [=, this](const QString& e) {
            collecting = false;
            lblProgress->setText(tr("yed_redeem failed"));
            lblCollectStatus->setText(tr("yed_redeem failed: %1\n\nNothing was reserved. Press Abort to close.").arg(e));
        });
}

void YellowbackRedeemWizard::postNext() {
    if (aborted || posting || !redeemIssued) return;
    if (signatures >= rosterK) { finishCollecting(); return; }

    int h = ctl->height();
    int pick = -1;
    bool anyWaiting = false;
    for (int i = 0; i < operators.size(); i++) {
        auto& op = operators[i];
        if (op.done || op.failed) continue;
        if (op.retryAfterHeight >= 0) {
            if (h > op.retryAfterHeight) { pick = i; break; }
            anyWaiting = true;
            continue;
        }
        pick = i;
        break;
    }
    if (pick < 0) {
        if (!anyWaiting) {
            lblCollectStatus->setText(tr("Every operator has answered and only %1 of %2 signatures were obtained. "
                                         "Fix the endpoint list or wait, then abort and start over.").arg(signatures).arg(rosterK));
        }
        return;   // tick() will call again once the height moves
    }

    auto conn = ctl->connection();
    if (conn == nullptr || conn->restclient == nullptr) {
        lblCollectStatus->setText(tr("No network client available."));
        return;
    }

    auto& op = operators[pick];
    op.status = tr("contacting...");
    refreshOperatorList();

    QUrl url(op.url);
    QString path = url.path();
    if (!path.endsWith(YellowbackRpc::Cosign::PATH)) {
        while (path.endsWith('/')) path.chop(1);
        url.setPath(path % YellowbackRpc::Cosign::PATH);
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(30000);

    json body = { {YellowbackRpc::Cosign::REQ_HEX, hex.toStdString()} };
    posting = true;
    QNetworkReply* reply = conn->restclient->post(req, QByteArray::fromStdString(body.dump()));
    QObject::connect(reply, &QNetworkReply::finished, this, [=, this]() {
        reply->deleteLater();
        posting = false;
        if (aborted) return;
        handleCosignReply(pick, reply);
    });
}

void YellowbackRedeemWizard::handleCosignReply(int opIndex, QNetworkReply* reply) {
    auto& op = operators[opIndex];
    QByteArray body = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto parsed = json::parse(body.toStdString(), nullptr, false);

    QString newHex;
    QString error;
    bool transient = false;
    int  quorum    = -1;     // quorumSignatures from the reply, -1 when absent
    bool complete  = false;

    if (reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300) {
        if (!parsed.is_discarded() && parsed.is_object()) {
            newHex   = YellowbackJson::toStr(parsed, YellowbackRpc::Cosign::RESP_HEX);
            error    = YellowbackJson::toStr(parsed, YellowbackRpc::Cosign::RESP_ERROR);
            transient = YellowbackJson::toBool(parsed, YellowbackRpc::Cosign::RESP_TRANSIENT);
            quorum   = (int)YellowbackJson::toInt(parsed, YellowbackRpc::Cosign::RESP_QUORUM_SIGNATURES, -1);
            complete = YellowbackJson::toBool(parsed, YellowbackRpc::Cosign::RESP_COMPLETE);
            int k    = (int)YellowbackJson::toInt(parsed, YellowbackRpc::Cosign::RESP_K, -1);
            if (k > 0 && k != rosterK) rosterK = k;   // the co-signer's roster wins
        } else {
            // plain-text hex
            static const QRegularExpression hexRe("^[0-9a-fA-F]+$");
            QString t = QString::fromUtf8(body).trimmed();
            if (hexRe.match(t).hasMatch()) newHex = t;
            else error = tr("unexpected reply: ") % t.left(200);
        }
    } else {
        if (!parsed.is_discarded() && parsed.is_object()) {
            error = YellowbackJson::toStr(parsed, YellowbackRpc::Cosign::RESP_ERROR, reply->errorString());
            transient = YellowbackJson::toBool(parsed, YellowbackRpc::Cosign::RESP_TRANSIENT);
        } else {
            error = reply->errorString();
            // Network-level failures (timeout, connection refused, TLS) are worth one retry per block
            transient = reply->error() != QNetworkReply::NoError && httpStatus == 0;
        }
    }

    if (!newHex.isEmpty() && error.isEmpty()) {
        // quorumSignatures is the count on the returned hex; a reply that did not raise it
        // added nothing. Without the field, fall back to the hex growing.
        bool added = quorum >= 0 ? quorum > signatures : newHex.length() > hex.length();
        if (!added) {
            op.failed = true;
            op.status = tr("refused: reply did not add a signature");
        } else {
            hex = newHex;
            signatures = quorum >= 0 ? quorum : signatures + 1;
            op.done = true;
            op.status = tr("signed");
        }
    } else {
        if (error.isEmpty()) error = tr("empty reply");
        if (transient || YellowbackController::isTransientRefusal(error)) {
            op.retryAfterHeight = ctl->height();
            op.status = tr("transient refusal, retrying after the next block: ") % error;
        } else {
            op.failed = true;
            op.status = tr("refused: ") % error;   // the co-signer's reason, verbatim (RED-3, RED-6, RED-8...)
        }
    }
    refreshOperatorList();

    if (complete || signatures >= rosterK) finishCollecting();
    else postNext();
}

void YellowbackRedeemWizard::tick() {
    if (aborted || !redeemIssued) return;
    lblDeadline->setText(deadlineText());

    if (ctl->height() > deadlineHeight && !didSubmit && !pgCollect->isComplete()) {
        timer->stop();
        collecting = false;
        lblCollectStatus->setText(tr("The deadline passed before enough signatures were collected. The redemption is being aborted; you can start over."));
        abortRedemption(false);
        return;
    }
    if (!posting && signatures < rosterK) postNext();
}

void YellowbackRedeemWizard::finishCollecting() {
    if (pgCollect->isComplete()) return;
    collecting = false;
    lblCollectStatus->setText(tr("%1 co-signatures collected. Press Next to have your node verify and broadcast the transaction.").arg(signatures));
    refreshOperatorList();
    pgCollect->setOk(true);
}

// ── Page 3: submit ────────────────────────────────────────────────────────────────────────

void YellowbackRedeemWizard::buildSubmitPage() {
    pgSubmit = new YellowbackWizardPage(this);
    pgSubmit->setTitle(tr("3. Submit"));
    pgSubmit->setSubTitle(tr("Your node verifies every signature and broadcasts"));
    auto layout = new QVBoxLayout(pgSubmit);
    lblSubmit = new QLabel(tr("Submitting..."), pgSubmit);
    lblSubmit->setWordWrap(true);
    lblSubmit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    btnSubmit = new QPushButton(tr("Retry yed_submitredeem"), pgSubmit);
    btnSubmit->setVisible(false);
    QObject::connect(btnSubmit, &QPushButton::clicked, [=, this]() { doSubmit(); });
    layout->addWidget(lblSubmit);
    layout->addWidget(btnSubmit);
    layout->addStretch();
    setPage(SubmitPage, pgSubmit);
    pgSubmit->setFinalPage(true);
}

void YellowbackRedeemWizard::doSubmit() {
    if (didSubmit) return;
    timer->stop();
    btnSubmit->setVisible(false);
    lblSubmit->setText(tr("Calling yed_submitredeem..."));
    ctl->submitRedeem(hex,
        [=, this](const json& r) {
            didSubmit = true;
            submittedTxid = YellowbackJson::toStr(r, YellowbackRpc::SubmitResult::TXID);
            qint64 q = YellowbackJson::toInt(r, YellowbackRpc::SubmitResult::QUORUM_SIGNATURES, signatures);
            ctl->removePendingRedemption(pos.vaultTxid);
            lblSubmit->setText(tr("Broadcast with %1 federation signature(s). txid: %2\n\n%3 of YED were burned; %4 of collateral goes to %5 once the transaction confirms.")
                .arg(q).arg(submittedTxid).arg(YellowbackFormat::cents(burnCents)).arg(YellowbackFormat::zec(pos.collateralZat))
                .arg(collateralTo.isEmpty() ? tr("a fresh transparent address of this wallet")
                                            : (shieldedCollateral ? tr("the shielded address %1").arg(collateralTo) : tr("the address %1").arg(collateralTo))));
            pgSubmit->setOk(true);
        },
        [=, this](const QString& e) {
            lblSubmit->setText(tr("yed_submitredeem failed: %1\n\nThe co-signed transaction is still held by this wizard. "
                                  "You can retry, or close the wizard to abort (which releases the reserved YED).").arg(e));
            btnSubmit->setVisible(true);
            pgSubmit->setOk(false);
            timer->start();
            setOption(QWizard::NoCancelButtonOnLastPage, false);
        });
}

// ── Abort ─────────────────────────────────────────────────────────────────────────────────

void YellowbackRedeemWizard::abortRedemption(bool silent) {
    aborted = true;
    timer->stop();
    if (!redeemIssued || didSubmit) return;
    ctl->abortRedeem(pos.vaultTxid,
        [=, this](const json&) {
            ctl->removePendingRedemption(pos.vaultTxid);
            redeemIssued = false;
            if (!silent)
                QMessageBox::information(this, tr("Redemption aborted"),
                    tr("The redemption of vault %1 was aborted; its YED is available again. Nothing was broadcast.").arg(pos.vaultTxid));
        },
        [=, this](const QString& e) {
            if (!silent)
                QMessageBox::warning(this, tr("yed_abortredeem failed"),
                    tr("%1\n\nThe pending redemption is still recorded on the node; abort it from the Vaults page.").arg(e));
        });
}

void YellowbackRedeemWizard::reject() {
    if (redeemIssued && !didSubmit) {
        auto r = QMessageBox::question(this, tr("Abort redemption?"),
            tr("Abort this redemption? The co-signatures collected so far are discarded and the reserved YED is released (yed_abortredeem)."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        abortRedemption(true);
    }
    aborted = true;
    timer->stop();
    QWizard::reject();
}
