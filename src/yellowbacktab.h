// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

#ifndef YELLOWBACKTAB_H
#define YELLOWBACKTAB_H

#include "precompiled.h"

class MainWindow;
class YellowbackController;
struct YellowbackPosition;
class YellowbackPositionsFilter;
struct YellowbackClaimable;
struct YellowbackAttestor;
struct YellowbackPendingClaim;
struct YellowbackClaimOutcome;

namespace Ui {
    class YellowbackTab;
    class YellowbackOverview;
    class YellowbackReceive;
    class YellowbackSend;
    class YellowbackMint;
    class YellowbackPositions;
    class YellowbackClaim;
    class YellowbackTransactions;
    class YellowbackRedeem;
    class YellowbackSettings;
    class YellowbackAttestors;
}

// The Yellowback tab: a status banner, the wallet.dat backup nag, and a QTabWidget of sub-pages
// in the order Overview, Receive, Send, Mint, Vaults, Claim, Pending claims, Transactions, Redeem,
// Attestors, Settings (plan §4.8; Pending claims is rpcversion 5's). Every action goes through
// YellowbackController; nothing here touches keys or the network.
//
// The spending actions — Mint, Send, Redeem/Release, Renew, Claim — are each one confirmation
// dialog and one yed_* call (plan §4.8, V24, L14); the node builds, checks, signs and commits.
// rpcversion 5 (the vault upgrade): a claim moves the collateral into a claim intent; the Pending
// claims page releases it after CLAIM_DELAY (vault_release) and, on an attestor's node, cancels a
// wrong-price claim before then (vault_buildcancel, set_signcancel, vault_send). Modal prompts go
// through confirmFn / noticeFn / inputFn so the offline QTest can answer them.
//
// v3 (A5-b): Mint, Claim, the claim notice and the equivocation report are two-step (W7): the
// call returns after the carrier broadcast with `pending`, the page says "preparing price proof
// (1 block)", and YellowbackController::awaitPending follows the main transaction into
// yed_listtransactions / yed_gettxinfo before the result dialog. The Settings page launches
// the attestation subscriber (yellowback-attest subscribe) beside the bundled node; the
// Attestors page carries the four attestor actions.
//
// `main` may be null and the controller may be a bare YellowbackController(nullptr, nullptr)
// fed canned JSON: that is what the offline QTest relies on.
class YellowbackTab : public QWidget {
    Q_OBJECT

public:
    explicit YellowbackTab(MainWindow* main, QWidget* parent = nullptr);
    ~YellowbackTab();

    void setController(YellowbackController* controller);
    YellowbackController* controller() { return ctl; }

    // Sub-page indices in subTabs
    enum Page { Overview = 0, Receive, Send, Mint, Vaults, Claim, PendingClaims, Transactions, Redeem, Attestors, Settings, PageCount };
    QWidget* page(Page p) { return pages[p]; }

    // Parses "12.34" / "12" / "$12.34" / "1,234.56" into cents; false on anything else
    static bool parseDollars(const QString& text, qint64* cents);

    // What the Vaults page says about the selected row's actions (plan §4.8 Vaults row):
    // which of Release / Redeem / Renew the row offers and why the others are not offered.
    // Pure function of the row and the index height (offline-testable).
    struct VaultActions {
        bool    release = false;   // VOID at or past lockHeight (yed_redeem, no burn, no fee; L14)
        bool    redeem  = false;   // ACTIVE at or past lockHeight (yed_redeem)
        bool    renew   = false;   // H-9.2: ACTIVE at or past lockHeight (yed_redeem, then yed_mint)
        QString text;              // the sentence shown under the table
    };
    static VaultActions vaultActions(const YellowbackPosition& p, int height);

    // Modal prompts (defaults: QMessageBox / QInputDialog). The QTest replaces them to read the copy and answer.
    std::function<bool(const QString& title, const QString& text)>              confirmFn;
    std::function<void(const QString& title, const QString& text, bool isError)> noticeFn;
    std::function<bool(const QString& title, const QString& label, QString* value)> inputFn;   // false = cancelled

    // The actions, callable without a table selection (the QTest drives them directly)
    void doMint();
    void doSend();
    void redeemVault(const YellowbackPosition& p, const QString& to = QString());   // ACTIVE: Redeem; VOID: Release (L14)
    static QString extraBurnLine(const nlohmann::json& r);      // H4: the extraBurnCents clause, empty when there is none
    void claimVault(const YellowbackClaimable& c, const QString& to = QString());   // to: "" or a transparent address (D-U1)
    // rpcversion 5, the Pending claims page: release a matured claim intent (vault_release), or,
    // on a node whose wallet holds a current member key of the YED attestor set, cancel a claim
    // made at a wrong price before it matures (vault_buildcancel + set_signcancel + vault_send).
    // A cancel is signed at most once per intent: the signed transaction is kept and only ever
    // re-sent, because two different signed spends of one intent are a provable equivocation.
    void releaseClaim(const YellowbackPendingClaim& c);
    void cancelClaim(const YellowbackPendingClaim& c);
    static QString describeCancelledClaims(const QList<YellowbackClaimOutcome>& outcomes);
    // Hardening H-9.2: renew = redeem the vault and re-mint its debt in a new vault, in one flow.
    // One confirmation covers both legs; the redeem is sent at once, and the mint follows when the
    // redeem has closed the vault (continueRenew, on each yed_listpositions), held to the
    // maxCollateralZat the dialog showed. The new vault is funded from the transparent balance the
    // redeem paid the collateral back to.
    void renewVault(const YellowbackPosition& p);
    bool renewPending() const { return renew.active; }
    QString redeemDestination() const;   // the Redeem page's choice: "" = a fresh own transparent address
    // v3: the claim notice (NOT-1) on a vault whose canNotice is true; two-step like Mint
    void noticeVault(const YellowbackPosition& p);
    // v3: what the Claim page says about a row's clause and residual before confirming
    static QString describeClaimPath(const YellowbackClaimable& c);
    // v3: the attestor actions (Attestors page). Each is one confirmation and one call; the
    // buttons gather their inputs through inputFn and call these. P4-b: registering is joining
    // the YED attestor set (yed_registerattestor = set_join), staying live is a SET_HEARTBEAT.
    void registerAttestor(const QString& bondYec, int lockBlocks);   // bondYec: a decimal string, sent as typed
    void withdrawBond(const YellowbackAttestor& a, const QString& to = QString());
    void heartbeatMember(const QString& memberKey);                   // set_heartbeat <attestorSetId> <key>
    void reportEquivocation(const QString& hexA, const QString& hexB);          // two-step
    void sweepCarriers();                                                        // yed_sweepcarriers (Settings page)

    // v3 Settings: the subscriber process. The wallet launches `yellowback-attest subscribe
    // --conf <generated toml>` beside the bundled node (startSubscriber); a missing binary
    // disables the launcher with a message and nothing else.
    void setSubscriberProcess(QProcess* p) { subscriber = p; updateSettingsPage(); }
    static QString subscriberStatus(const QProcess* p);   // "running (pid N)" | "not running" | "starting"
    void startSubscriber();
    void stopSubscriber();
    void setSubscriberBinary(const QString& path) { subscriberBinary = path; updateSettingsPage(); }   // "" = beside the wallet
    QString subscriberBinaryPath() const;                 // the path the launcher would run
    static QString defaultSubscriberBinaryPath();         // <applicationDirPath>/yellowback-attest[.exe]
    // The attest.toml the launcher writes: [node] from the connection (the cookie file when it
    // exists, else rpcuser/rpcpassword), [transport] from the persisted Settings. Pure.
    static QString subscriberConfigToml(const QString& kind, const QString& path, const QString& relays, const QString& peers,
                                        const QString& rpcUrl, const QString& cookieFile, const QString& rpcUser, const QString& rpcPassword);
    static QString cookiePathFor(const QString& zcashDir, const QString& network);   // <datadir>[/testnet3|/regtest]/.cookie
    static bool writePrivateFile(const QString& path, const QByteArray& bytes, QString* error);   // 0600 before the first byte
    QString subscriberConfigPath() const { return subscriberConfPath; }

private:
    void setupPages();
    void setupOverview();
    void setupReceive();
    void setupSend();
    void setupMint();
    void setupPositions();
    void setupClaim();
    void setupTransactions();
    void setupRedeem();
    void setupSettings();
    void setupAttestors();
    void setupPendingClaims();

    void updateBanner();
    void updateOverview();
    void updateBalances();
    void updateMintGate();
    void applyMintableClasses();          // W16: grey out the classes yed_getstats says cannot mint now
    int  mintedStatusHeight = -1;
    YellowbackPositionsFilter* positionsFilter = nullptr;   // the Positions table's status filter (default: open vaults)
public:
    /** The Positions table's current row, mapped through the filter to the model. */
    const YellowbackPosition* selectedPosition() const;
private:         // the height the last "Minted. txid" status was shown at; cleared two blocks on
    void updateMintClasses();
    QString fundingSource() const;          // "" = transparent total, else the chosen ys1... address (I2)
    void refreshFundingSources();           // rebuild cmbFundFrom from the DataModel, keeping the selection
    void updatePositions();
    void updateVaultButtons();
    void updateClaimPage();
    void updateRedeemPage();
    void updateBackupNag();
    void updateSettingsPage();
    void updateAttestors();                 // v3: the arming banner and the table
    void updateAttestorButtons();           // v3: which attestor action the selected row offers
    void updateMembership();                // rpcversion 5: this wallet's members of the YED attestor set
    void updatePendingClaims();             // rpcversion 5: the Pending claims page
    void updateMintAttest();                // v3: the selection line under the estimate
    // v3 two-step: the "preparing price proof" status, then the follow-up (W7)
    void followPending(const QString& type, const nlohmann::json& pendingReply, QLabel* status,
                       std::function<void(const nlohmann::json& txinfo)> done, const QString& vaultTxid = QString());
    // confirmedZat: the collateral the confirmation dialog showed (-1 = none); the summary
    // opens with a prominent note when the vault locked a different amount (audit F-1).
    QString mintSummary(qint64 cents, const nlohmann::json& r, qint64 confirmedZat = -1) const;
    // The slack the dialog's caps allow over/under the shown figure, in basis points (1 %):
    // yed_mint maxCollateralZat and yed_claim minOutZat (audit F-1).
    static constexpr qint64 CAP_SLACK_BPS = 100;
    static qint64 capAbove(qint64 zat) { return zat + zat * CAP_SLACK_BPS / 10000; }
    static qint64 capBelow(qint64 zat) { return zat - zat * CAP_SLACK_BPS / 10000; }
    QString claimSummary(const nlohmann::json& r) const;
    bool    bundleInsufficientRetry(const QString& what, const QString& e);   // true when it was that error
    void setActionsEnabled(bool enabled);
    void refreshDestinations();             // the Redeem page's destination combo (I2)
    bool confirm(const QString& title, const QString& text);
    void notice(const QString& title, const QString& text, bool isError = false);
    void failed(const QString& what, const QString& e);   // "yed_x failed: <verbatim>" + explainError

    void requestEstimate();
    void continueRenew();                   // H-9.2: the mint leg once the redeem has closed the vault
    struct RenewState {
        bool    active = false;
        bool    minting = false;            // the mint leg has been sent
        QString vaultTxid;                  // the vault being renewed
        QString redeemTxid;
        qint64  cents = 0;                  // its debt, re-minted
        int     lockBlocks = 0;
        qint64  maxCollateralZat = 0;       // the cap the dialog showed for the new vault
        qint64  shownCollateralZat = 0;
    } renew;
    void newReceiveAddress();
    void explainVoid(const YellowbackPosition& p);
    void saveSettings();
    void showTxContextMenu(QTableView* table, const QPoint& pos);

    MainWindow*          main = nullptr;
    YellowbackController*   ctl  = nullptr;

    Ui::YellowbackTab*          ui           = nullptr;
    Ui::YellowbackOverview*     uiOverview   = nullptr;
    Ui::YellowbackReceive*      uiReceive    = nullptr;
    Ui::YellowbackSend*         uiSend       = nullptr;
    Ui::YellowbackMint*         uiMint       = nullptr;
    Ui::YellowbackPositions*    uiPositions  = nullptr;
    Ui::YellowbackClaim*        uiClaim      = nullptr;
    Ui::YellowbackTransactions* uiTx         = nullptr;
    Ui::YellowbackRedeem*       uiRedeem     = nullptr;
    Ui::YellowbackSettings*     uiSettings   = nullptr;
    Ui::YellowbackAttestors*    uiAttestors  = nullptr;
    // rpcversion 5: the Pending claims page, built in code (no .ui)
    QTableView*                 tblPendingClaims   = nullptr;
    QLabel*                     lblPendingIntro    = nullptr;
    QLabel*                     lblPendingAction   = nullptr;
    QLabel*                     lblCancelledClaims = nullptr;
    QPushButton*                btnReleaseClaim    = nullptr;
    QPushButton*                btnCancelClaim     = nullptr;
public:
    const YellowbackPendingClaim* selectedPendingClaim() const;
private:
    QProcess*                   subscriber   = nullptr;
    QString                     subscriberBinary;      // override; "" = defaultSubscriberBinaryPath()
    QString                     subscriberConfPath;    // the toml the launcher last wrote
    QWidget*                 pages[PageCount] = {};

    bool     actionsEnabled   = false;
    QTimer*  estimateTimer    = nullptr;
    int      estimateSeq      = 0;
    qint64   estimateCents    = 0;      // the amount the last estimate was for
    int      estimateLockBlocks = -1;
    qint64   estimateZat      = -1;     // last estimate; -1 = none
    QString  receiveAddress;
};

#endif // YELLOWBACKTAB_H
