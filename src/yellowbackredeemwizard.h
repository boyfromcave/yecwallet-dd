#ifndef YELLOWBACKREDEEMWIZARD_H
#define YELLOWBACKREDEEMWIZARD_H

#include "precompiled.h"
#include "yellowbackmodels.h"
#include <QWizard>
#include <QWizardPage>
#include <QListWidget>
#include <QProgressBar>

class YellowbackController;

// A QWizardPage whose "Next" is gated by an explicit flag.
class YellowbackWizardPage : public QWizardPage {
    Q_OBJECT
public:
    explicit YellowbackWizardPage(QWidget* parent = nullptr) : QWizardPage(parent) {}
    bool isComplete() const override { return ok; }
    void setOk(bool v) { if (ok != v) { ok = v; emit completeChanged(); } }
private:
    bool ok = false;
};

// The redemption wizard (plan §4.7 Redeem row, §5 "Redemption client", D16):
//
//   1 Review     burn, collateral returned, roster k-of-n, configured operators
//   2 Collect    yed_redeem on the local node -> POST the hex to each operator's /cosign endpoint
//                (HTTPS, through the app's existing QNetworkAccessManager) until k signatures;
//                transient refusals ("RED-n: ... (transient)", or transient: true) retried after
//                the next block; a countdown to yed_redeem.deadlineHeight; abort at any time
//                via yed_abortredeem
//   3 Submit     yed_submitredeem on the local node, which re-verifies every signature (SUB-1)
//
// This is the only code in the wallet that talks to anything other than the local node. It
// never sees a private key: the node signs, the operators add their signatures, the node
// verifies the result before broadcasting.
class YellowbackRedeemWizard : public QWizard {
    Q_OBJECT

public:
    YellowbackRedeemWizard(YellowbackController* ctl, const YellowbackPosition& position, QWidget* parent = nullptr);
    ~YellowbackRedeemWizard();

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

    void startRedeem();            // yed_redeem
    void postNext();               // find the next operator to try and POST
    void handleCosignReply(int opIndex, QNetworkReply* reply);
    void tick();                   // 1-second countdown / retry scheduler
    void finishCollecting();
    void doSubmit();               // yed_submitredeem
    void abortRedemption(bool silent);
    void refreshOperatorList();
    QString deadlineText() const;

    YellowbackController*  ctl;
    YellowbackPosition     pos;

    // Review
    YellowbackWizardPage*  pgReview   = nullptr;
    QLabel*             lblReview  = nullptr;
    int                 rosterK    = 0;
    int                 rosterN    = 0;

    // Collect
    YellowbackWizardPage*  pgCollect  = nullptr;
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
    int                 deadlineHeight = 0;         // yed_redeem.deadlineHeight (inclusive)
    qint64              requiredBurnCents = 0;
    qint64              burnCents = 0;              // what the transaction actually burns
    bool                redeemIssued = false;   // yed_redeem succeeded (pending record exists)
    bool                posting      = false;   // a POST is in flight
    bool                collecting   = false;
    bool                aborted      = false;

    // Submit
    YellowbackWizardPage*  pgSubmit   = nullptr;
    QLabel*             lblSubmit  = nullptr;
    QPushButton*        btnSubmit  = nullptr;
    bool                didSubmit  = false;
    QString             submittedTxid;
};

#endif // YELLOWBACKREDEEMWIZARD_H
