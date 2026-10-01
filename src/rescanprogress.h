#ifndef RESCANPROGRESS_H
#define RESCANPROGRESS_H

#include "mainwindow.h"
#include "precompiled.h"

class RescanProgress {
public:
    RescanProgress(MainWindow* _main);
    ~RescanProgress();

    void updateProgress(int value);
    void closeProgress();

    // Ycash 6.20.0: the rescan runs inside the import RPC, and the node reports no progress.
    // Show a busy bar and the elapsed time instead, so a long rescan does not look like a hang.
    void setBusy(const QString& what);

private:
    MainWindow* main;
    QProgressDialog* progress;
    QTimer*          ticker = nullptr;
    QElapsedTimer    elapsed;
};


#endif
