#include "ydollarredeemwizard.h"
#include "ydollarcontroller.h"
#include "ydollarrpc.h"
#include "connection.h"
#include "settings.h"

#include <QVBoxLayout>

using json = nlohmann::json;

YDollarRedeemWizard::YDollarRedeemWizard(YDollarController* ctl, const YDollarPosition& position, QWidget* parent)
    : QWizard(parent), ctl(ctl), pos(position) {
    setWindowTitle(tr("Redeem vault ") % pos.vaultTxid.left(16) % "...");
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::NoBackButtonOnLastPage, true);
    setOption(QWizard::NoCancelButtonOnLastPage, true);
    setButtonText(QWizard::CancelButton, tr("Abort"));
    setMinimumSize(640, 480);

    for (auto& url : Settings::getInstance()->getYDollarEndpoints()) {
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

YDollarRedeemWizard::~YDollarRedeemWizard() {
    timer->stop();
}

// ── Page 1: review ────────────────────────────────────────────────────────────────────────

void YDollarRedeemWizard::buildReviewPage() {
    pgReview = new YDollarWizardPage(this);
    pgReview->setTitle(tr("1. Review"));
    pgReview->setSubTitle(tr("What this redemption will do"));
    auto layout = new QVBoxLayout(pgReview);
    lblReview = new QLabel(pgReview);
    lblReview->setWordWrap(true);
    lblReview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(lblReview);
    layout->addStretch();
    setPage(ReviewPage, pgReview);

    QString text = tr("<b>Vault</b> %1<br>"
                      "<b>Status</b> %2<br>"
                      "<b>Minted</b> %3<br>"
                      "<b>YDollar to burn now</b> %4%5<br>"
                      "<b>Collateral returned to you</b> %6<br><br>")
                      .arg(pos.vaultTxid).arg(pos.status).arg(YDollarFormat::cents(pos.mintedCents))
                      .arg(YDollarFormat::cents(pos.requiredBurnCents))
                      .arg(pos.requiredBurnCents > pos.mintedCents ? tr(" (more than minted: emergency redemption ratio in effect)") : "")
                      .arg(YDollarFormat::zec(pos.collateralZat));
    text += tr("Pressing Next asks your node to build and sign the redemption (yd_redeem). From then on the "
               "YDollar to be burned is reserved until the redemption is submitted or aborted. The signed "
               "transaction is then sent to the federation operators for co-signatures, and finally your node "
               "checks every signature before broadcasting it. The whole thing must finish within %1 blocks "
               "(about %2 minutes); otherwise you abort and start over.<br><br>")
               .arg(YDollarRpc::REDEEM_DEADLINE).arg(YDollarRpc::REDEEM_DEADLINE * YDollarRpc::SECONDS_PER_BLOCK / 60);
    if (operators.isEmpty()) {
        text += tr("<b>No operator endpoints are configured.</b> Add them on the YDollar Settings page first.");
        lblReview->setText(text);
        pgReview->setOk(false);
        return;
    }
    text += tr("Operators configured: %1<br>Roster: checking...").arg(operators.size());
    lblReview->setText(text);
    pgReview->setOk(false);

    ctl->getRoster(
        [=, this](const json& r) {
            using namespace YDollarRpc::Roster;
            rosterK = (int)YDollarJson::toInt(r, K);
            rosterN = (int)YDollarJson::toInt(r, N);
            QString t = text;
            t.replace(tr("Roster: checking..."),
                      tr("Roster #%1: %2 of %3 signatures needed.").arg(YDollarJson::toInt(r, INDEX)).arg(rosterK).arg(rosterN));
            if (operators.size() < rosterK) {
                t += tr("<br><b>Only %1 endpoint(s) configured but %2 signatures are needed.</b> Add more on the Settings page.")
                        .arg(operators.size()).arg(rosterK);
                lblReview->setText(t);
                pgReview->setOk(false);
                return;
            }
            if (pos.requiredBurnCents > ctl->confirmedCents()) {
                t += tr("<br><b>You have %1 of confirmed YDollar but %2 must be burned.</b>")
                        .arg(YDollarFormat::cents(ctl->confirmedCents())).arg(YDollarFormat::cents(pos.requiredBurnCents));
                lblReview->setText(t);
                pgReview->setOk(false);
                return;
            }
            lblReview->setText(t);
            pgReview->setOk(true);
        },
        [=, this](const QString& e) {
            lblReview->setText(text % tr("<br><b>yd_getroster failed:</b> ") % e);
            pgReview->setOk(false);
        });
}

// ── Page 2: collect co-signatures ─────────────────────────────────────────────────────────

void YDollarRedeemWizard::buildCollectPage() {
    pgCollect = new YDollarWizardPage(this);
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
    txtHex->setVisible(Settings::getInstance()->getYDollarAdvanced());

    layout->addWidget(lblProgress);
    layout->addWidget(progress);
    layout->addWidget(lblDeadline);
    layout->addWidget(lstOperators);
    layout->addWidget(lblCollectStatus);
    layout->addWidget(txtHex);
    setPage(CollectPage, pgCollect);
    refreshOperatorList();
}

void YDollarRedeemWizard::refreshOperatorList() {
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

QString YDollarRedeemWizard::deadlineText() const {
    int left = deadlineHeight - ctl->height();
    if (left <= 0) return tr("Deadline passed (height %1).").arg(deadlineHeight);
    return tr("Submit by height %1: %2 blocks left (about %3 minutes). Current index height %4.")
            .arg(deadlineHeight).arg(left).arg(left * YDollarRpc::SECONDS_PER_BLOCK / 60).arg(ctl->height());
}

void YDollarRedeemWizard::startRedeem() {
    collecting = true;
    lblCollectStatus->setText(tr("Calling yd_redeem..."));
    ctl->redeem(pos.vaultTxid,
        [=, this](const json& r) {
            using namespace YDollarRpc::RedeemResult;
            hex               = YDollarJson::toStr(r, HEX);
            expiryHeight      = (int)YDollarJson::toInt(r, EXPIRY_HEIGHT);
            requiredBurnCents = YDollarJson::toInt(r, REQUIRED_BURN_CENTS, pos.requiredBurnCents);
            deadlineHeight    = ctl->deadlineHeight(expiryHeight);
            redeemIssued      = true;
            ctl->addPendingRedemption(pos.vaultTxid, expiryHeight);

            // The node's roster for this vault wins over the one shown on the review page
            if (r.contains(ROSTER) && r[ROSTER].is_object()) {
                rosterK = (int)YDollarJson::toInt(r[ROSTER], YDollarRpc::Roster::K, rosterK);
                rosterN = (int)YDollarJson::toInt(r[ROSTER], YDollarRpc::Roster::N, rosterN);
            }
            if (rosterK <= 0) rosterK = 1;

            lblCollectStatus->setText(tr("Your node signed the redemption (burning %1, expiry height %2). Contacting operators...")
                .arg(YDollarFormat::cents(requiredBurnCents)).arg(expiryHeight));
            lblDeadline->setText(deadlineText());
            refreshOperatorList();
            timer->start();
            postNext();
        },
        [=, this](const QString& e) {
            collecting = false;
            lblProgress->setText(tr("yd_redeem failed"));
            lblCollectStatus->setText(tr("yd_redeem failed: %1\n\nNothing was reserved. Press Abort to close.").arg(e));
        });
}

void YDollarRedeemWizard::postNext() {
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
    if (!path.endsWith(YDollarRpc::Cosign::PATH)) {
        while (path.endsWith('/')) path.chop(1);
        url.setPath(path % YDollarRpc::Cosign::PATH);
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(30000);

    json body = { {YDollarRpc::Cosign::REQ_HEX, hex.toStdString()} };
    posting = true;
    QNetworkReply* reply = conn->restclient->post(req, QByteArray::fromStdString(body.dump()));
    QObject::connect(reply, &QNetworkReply::finished, this, [=, this]() {
        reply->deleteLater();
        posting = false;
        if (aborted) return;
        handleCosignReply(pick, reply);
    });
}

void YDollarRedeemWizard::handleCosignReply(int opIndex, QNetworkReply* reply) {
    auto& op = operators[opIndex];
    QByteArray body = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto parsed = json::parse(body.toStdString(), nullptr, false);

    QString newHex;
    QString error;
    bool transient = false;

    if (reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300) {
        if (!parsed.is_discarded() && parsed.is_object()) {
            newHex = YDollarJson::toStr(parsed, YDollarRpc::Cosign::RESP_HEX);
            error  = YDollarJson::toStr(parsed, YDollarRpc::Cosign::RESP_ERROR);
            transient = YDollarJson::toBool(parsed, YDollarRpc::Cosign::RESP_TRANSIENT);
        } else {
            // plain-text hex
            static const QRegularExpression hexRe("^[0-9a-fA-F]+$");
            QString t = QString::fromUtf8(body).trimmed();
            if (hexRe.match(t).hasMatch()) newHex = t;
            else error = tr("unexpected reply: ") % t.left(200);
        }
    } else {
        if (!parsed.is_discarded() && parsed.is_object()) {
            error = YDollarJson::toStr(parsed, YDollarRpc::Cosign::RESP_ERROR, reply->errorString());
            transient = YDollarJson::toBool(parsed, YDollarRpc::Cosign::RESP_TRANSIENT);
        } else {
            error = reply->errorString();
            // Network-level failures (timeout, connection refused, TLS) are worth one retry per block
            transient = reply->error() != QNetworkReply::NoError && httpStatus == 0;
        }
    }

    if (!newHex.isEmpty() && error.isEmpty()) {
        if (newHex.length() <= hex.length()) {
            op.failed = true;
            op.status = tr("refused: reply did not add a signature");
        } else {
            hex = newHex;
            signatures++;
            op.done = true;
            op.status = tr("signed");
        }
    } else {
        if (error.isEmpty()) error = tr("empty reply");
        if (transient || YDollarController::isTransientRefusal(error)) {
            op.retryAfterHeight = ctl->height();
            op.status = tr("transient refusal, retrying after the next block: ") % error;
        } else {
            op.failed = true;
            op.status = tr("refused: ") % error;   // the co-signer's reason, verbatim (RED-3, RED-6, RED-8...)
        }
    }
    refreshOperatorList();

    if (signatures >= rosterK) finishCollecting();
    else postNext();
}

void YDollarRedeemWizard::tick() {
    if (aborted || !redeemIssued) return;
    lblDeadline->setText(deadlineText());

    if (ctl->height() >= deadlineHeight && !didSubmit && !pgCollect->isComplete()) {
        timer->stop();
        collecting = false;
        lblCollectStatus->setText(tr("The deadline passed before enough signatures were collected. The redemption is being aborted; you can start over."));
        abortRedemption(false);
        return;
    }
    if (!posting && signatures < rosterK) postNext();
}

void YDollarRedeemWizard::finishCollecting() {
    if (pgCollect->isComplete()) return;
    collecting = false;
    lblCollectStatus->setText(tr("%1 co-signatures collected. Press Next to have your node verify and broadcast the transaction.").arg(signatures));
    refreshOperatorList();
    pgCollect->setOk(true);
}

// ── Page 3: submit ────────────────────────────────────────────────────────────────────────

void YDollarRedeemWizard::buildSubmitPage() {
    pgSubmit = new YDollarWizardPage(this);
    pgSubmit->setTitle(tr("3. Submit"));
    pgSubmit->setSubTitle(tr("Your node verifies every signature and broadcasts"));
    auto layout = new QVBoxLayout(pgSubmit);
    lblSubmit = new QLabel(tr("Submitting..."), pgSubmit);
    lblSubmit->setWordWrap(true);
    lblSubmit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    btnSubmit = new QPushButton(tr("Retry yd_submitredeem"), pgSubmit);
    btnSubmit->setVisible(false);
    QObject::connect(btnSubmit, &QPushButton::clicked, [=, this]() { doSubmit(); });
    layout->addWidget(lblSubmit);
    layout->addWidget(btnSubmit);
    layout->addStretch();
    setPage(SubmitPage, pgSubmit);
    pgSubmit->setFinalPage(true);
}

void YDollarRedeemWizard::doSubmit() {
    if (didSubmit) return;
    timer->stop();
    btnSubmit->setVisible(false);
    lblSubmit->setText(tr("Calling yd_submitredeem..."));
    ctl->submitRedeem(hex,
        [=, this](const json& r) {
            didSubmit = true;
            submittedTxid = YDollarJson::toStr(r, YDollarRpc::SendResult::TXID);
            ctl->removePendingRedemption(pos.vaultTxid);
            lblSubmit->setText(tr("Broadcast. txid: %1\n\n%2 of YDollar were burned; %3 of collateral returns to your wallet once the transaction confirms.")
                .arg(submittedTxid).arg(YDollarFormat::cents(requiredBurnCents)).arg(YDollarFormat::zec(pos.collateralZat)));
            pgSubmit->setOk(true);
        },
        [=, this](const QString& e) {
            lblSubmit->setText(tr("yd_submitredeem failed: %1\n\nThe co-signed transaction is still held by this wizard. "
                                  "You can retry, or close the wizard to abort (which releases the reserved YDollar).").arg(e));
            btnSubmit->setVisible(true);
            pgSubmit->setOk(false);
            timer->start();
            setOption(QWizard::NoCancelButtonOnLastPage, false);
        });
}

// ── Abort ─────────────────────────────────────────────────────────────────────────────────

void YDollarRedeemWizard::abortRedemption(bool silent) {
    aborted = true;
    timer->stop();
    if (!redeemIssued || didSubmit) return;
    ctl->abortRedeem(pos.vaultTxid,
        [=, this](const json&) {
            ctl->removePendingRedemption(pos.vaultTxid);
            redeemIssued = false;
            if (!silent)
                QMessageBox::information(this, tr("Redemption aborted"),
                    tr("The redemption of vault %1 was aborted; its YDollar is available again. Nothing was broadcast.").arg(pos.vaultTxid));
        },
        [=, this](const QString& e) {
            if (!silent)
                QMessageBox::warning(this, tr("yd_abortredeem failed"),
                    tr("%1\n\nThe pending redemption is still recorded on the node; abort it from the Vaults page.").arg(e));
        });
}

void YDollarRedeemWizard::reject() {
    if (redeemIssued && !didSubmit) {
        auto r = QMessageBox::question(this, tr("Abort redemption?"),
            tr("Abort this redemption? The co-signatures collected so far are discarded and the reserved YDollar is released (yd_abortredeem)."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        abortRedemption(true);
    }
    aborted = true;
    timer->stop();
    QWizard::reject();
}
