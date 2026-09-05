#ifndef YDOLLARREDEEMWIZARD_H
#define YDOLLARREDEEMWIZARD_H

#include "precompiled.h"
#include "ydollarmodels.h"
#include <QWizard>
#include <QWizardPage>
#include <QListWidget>
#include <QProgressBar>

class YDollarController;

// A QWizardPage whose "Next" is gated by an explicit flag.
class YDollarWizardPage : public QWizardPage {
    Q_OBJECT
public:
    explicit YDollarWizardPage(QWidget* parent = nullptr) : QWizardPage(parent) {}
    bool isComplete() const override { return ok; }
    void setOk(bool v) { if (ok != v) { ok = v; emit completeChanged(); } }
private:
    bool ok = false;
};

// The redemption wizard (plan §4.7 Redeem row, §5 "Redemption client", D16):
//
//   1 Review     burn, collateral returned, roster k-of-n, configured operators
//   2 Collect    yd_redeem on the local node -> POST the hex to each operator's /cosign endpoint
//                (HTTPS, through the app's existing QNetworkAccessManager) until k signatures;
//                transient refusals (RED-0, RED-2) retried after the next block; a countdown
//                to the 36-block deadline; abort at any time via yd_abortredeem
//   3 Submit     yd_submitredeem on the local node, which re-verifies every signature (SUB-1)
//
// This is the only code in the wallet that talks to anything other than the local node. It
// never sees a private key: the node signs, the operators add their signatures, the node
// verifies the result before broadcasting.
class YDollarRedeemWizard : public QWizard {
    Q_OBJECT

public:
    YDollarRedeemWizard(YDollarController* ctl, const YDollarPosition& position, QWidget* parent = nullptr);
    ~YDollarRedeemWizard();

    void reject() override;
    bool submitted() const { return didSubmit; }
    QString txid() const { return submittedTxid; }

private:
    enum PageId { ReviewPage = 0, CollectPage, SubmitPage };

    struct Operator {
        QString url;              // base URL from Settings
        QString status;           // shown in the list
        bool    done      = false;   // signature obtained
        bool    failed    = false;   // permanent refusal / network error
        int     retryAfterHeight = -1;  // transient: try again once height > this
    };

    void buildReviewPage();
    void buildCollectPage();
    void buildSubmitPage();

    void startRedeem();            // yd_redeem
    void postNext();               // find the next operator to try and POST
    void handleCosignReply(int opIndex, QNetworkReply* reply);
    void tick();                   // 1-second countdown / retry scheduler
    void finishCollecting();
    void doSubmit();               // yd_submitredeem
    void abortRedemption(bool silent);
    void refreshOperatorList();
    QString deadlineText() const;

    YDollarController*  ctl;
    YDollarPosition     pos;

    // Review
    YDollarWizardPage*  pgReview   = nullptr;
    QLabel*             lblReview  = nullptr;
    int                 rosterK    = 0;
    int                 rosterN    = 0;

    // Collect
    YDollarWizardPage*  pgCollect  = nullptr;
    QLabel*             lblProgress = nullptr;
    QLabel*             lblDeadline = nullptr;
    QLabel*             lblCollectStatus = nullptr;
    QProgressBar*       progress   = nullptr;
    QListWidget*        lstOperators = nullptr;
    QPlainTextEdit*     txtHex     = nullptr;   // advanced only
    QTimer*             timer      = nullptr;
    QList<Operator>     operators;
    QString             hex;                    // current transaction hex (grows a signature at a time)
    int                 signatures = 0;         // co-signatures obtained
    int                 expiryHeight = 0;
    int                 deadlineHeight = 0;
    qint64              requiredBurnCents = 0;
    bool                redeemIssued = false;   // yd_redeem succeeded (pending record exists)
    bool                posting      = false;   // a POST is in flight
    bool                collecting   = false;
    bool                aborted      = false;

    // Submit
    YDollarWizardPage*  pgSubmit   = nullptr;
    QLabel*             lblSubmit  = nullptr;
    QPushButton*        btnSubmit  = nullptr;
    bool                didSubmit  = false;
    QString             submittedTxid;
};

#endif // YDOLLARREDEEMWIZARD_H
