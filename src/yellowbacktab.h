#ifndef YELLOWBACKTAB_H
#define YELLOWBACKTAB_H

#include "precompiled.h"

class MainWindow;
class YellowbackController;
struct YellowbackPosition;
struct YellowbackClaimable;

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
// in the order Overview, Receive, Send, Mint, Vaults, Claim, Transactions, Redeem, Attestors,
// Settings (plan §4.8; Attestors is the read-only v3 view). Every action goes through YellowbackController; nothing here touches keys or
// the network.
//
// The spending actions — Mint, Send, Redeem/Release, Claim, Sweep — are each one confirmation
// dialog and one yed_* call (plan §4.8, V24, L10, L14); the node builds, checks, signs and
// commits. Modal prompts go through confirmFn / noticeFn so the offline QTest can answer them.
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
    enum Page { Overview = 0, Receive, Send, Mint, Vaults, Claim, Transactions, Redeem, Attestors, Settings, PageCount };
    QWidget* page(Page p) { return pages[p]; }

    // Parses "12.34" / "12" / "$12.34" / "1,234.56" into cents; false on anything else
    static bool parseDollars(const QString& text, qint64* cents);

    // What the Vaults page says about the selected row's actions (plan §4.8 Vaults row):
    // which of Release / Redeem / Sweep the row offers and why the others are not offered.
    // Pure function of the row, the index height and the abandonment flag (offline-testable).
    struct VaultActions {
        bool    release = false;   // VOID at or past lockHeight (yed_redeem, no burn, no fee; L14)
        bool    redeem  = false;   // ACTIVE at or past lockHeight (yed_redeem)
        bool    sweep   = false;   // ACTIVE while abandoned (yed_sweep, L10)
        QString text;              // the sentence shown under the table
    };
    static VaultActions vaultActions(const YellowbackPosition& p, int height, bool abandoned);

    // Modal prompts (defaults: QMessageBox). The QTest replaces them to read the copy and answer.
    std::function<bool(const QString& title, const QString& text)>              confirmFn;
    std::function<void(const QString& title, const QString& text, bool isError)> noticeFn;

    // The actions, callable without a table selection (the QTest drives them directly)
    void doMint();
    void doSend();
    void redeemVault(const YellowbackPosition& p, const QString& to = QString());   // ACTIVE: Redeem; VOID: Release (L14)
    static QString extraBurnLine(const nlohmann::json& r);      // H4: the extraBurnCents clause, empty when there is none
    void claimVault(const YellowbackClaimable& c, const QString& to = QString());
    void sweepVault(const YellowbackPosition& p, const QString& to = QString());    // L10: carries the acknowledgement
    QString redeemDestination() const;   // the Redeem page's choice: "" = a fresh own transparent address

    // v3 Settings: the subscriber process, when the wallet launched one (A5-b adds the launcher;
    // until then it stays null and the status line reads "not running").
    void setSubscriberProcess(QProcess* p) { subscriber = p; updateSettingsPage(); }
    static QString subscriberStatus(const QProcess* p);   // "running (pid N)" | "not running" | "starting"

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

    void updateBanner();
    void updateOverview();
    void updateBalances();
    void updateMintGate();
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
    void updateMintAttest();                // v3: the selection line under the estimate
    void setActionsEnabled(bool enabled);
    void refreshDestinations();             // the Redeem page's destination combo (I2)
    bool confirm(const QString& title, const QString& text);
    void notice(const QString& title, const QString& text, bool isError = false);
    void failed(const QString& what, const QString& e);   // "yed_x failed: <verbatim>" + explainError

    void requestEstimate();
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
    QProcess*                   subscriber   = nullptr;
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
