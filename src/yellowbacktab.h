#ifndef YELLOWBACKTAB_H
#define YELLOWBACKTAB_H

#include "precompiled.h"

class MainWindow;
class YellowbackController;

namespace Ui {
    class YellowbackTab;
    class YellowbackOverview;
    class YellowbackReceive;
    class YellowbackSend;
    class YellowbackMint;
    class YellowbackPositions;
    class YellowbackTransactions;
    class YellowbackRedeem;
    class YellowbackSettings;
}

// The Yellowback tab: a status banner, the wallet.dat backup nag, and a QTabWidget of sub-pages
// in the order Overview, Receive, Send, Mint, Vaults, Transactions, Redeem, Settings
// (plan §4.7). Every action goes through YellowbackController; nothing here touches keys or
// the network.
//
// `main` may be null and the controller may never be set: the tab then renders with every
// action disabled. That is what the QTest target relies on.
class YellowbackTab : public QWidget {
    Q_OBJECT

public:
    explicit YellowbackTab(MainWindow* main, QWidget* parent = nullptr);
    ~YellowbackTab();

    void setController(YellowbackController* controller);
    YellowbackController* controller() { return ctl; }

    // Sub-page indices in subTabs
    enum Page { Overview = 0, Receive, Send, Mint, Vaults, Transactions, Redeem, Settings, PageCount };
    QWidget* page(Page p) { return pages[p]; }

    // Parses "12.34" / "12" / "$12.34" / "1,234.56" into cents; false on anything else
    static bool parseDollars(const QString& text, qint64* cents);

private:
    void setupPages();
    void setupOverview();
    void setupReceive();
    void setupSend();
    void setupMint();
    void setupPositions();
    void setupTransactions();
    void setupRedeem();
    void setupSettings();

    void updateBanner();
    void updateOverview();
    void updateBalances();
    void updateMintGate();
    QString fundingSource() const;          // "" = transparent total, else the chosen ys1... address (I2)
    void refreshFundingSources();           // rebuild cmbFundFrom from the DataModel, keeping the selection
    void updatePositions();
    void updateRedeemPage();
    void updateBackupNag();
    void updateSettingsPage();
    void setActionsEnabled(bool enabled);

    void requestEstimate();
    void doMint();
    void doSend();
    void newReceiveAddress();
    void startRedemption(const QString& vaultTxid);
    void explainVoid(const QString& vaultTxid);
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
    Ui::YellowbackTransactions* uiTx         = nullptr;
    Ui::YellowbackRedeem*       uiRedeem     = nullptr;
    Ui::YellowbackSettings*     uiSettings   = nullptr;
    QWidget*                 pages[PageCount] = {};

    bool     actionsEnabled   = false;
    QTimer*  estimateTimer    = nullptr;
    int      estimateSeq      = 0;
    qint64   estimateCents    = 0;      // the amount the last estimate was for
    int      estimateTier     = -1;
    qint64   estimateZat      = -1;     // last estimate; -1 = none
    QString  receiveAddress;
};

#endif // YELLOWBACKTAB_H
