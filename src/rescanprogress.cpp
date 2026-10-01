#include "rescanprogress.h"

#include "precompiled.h"

RescanProgress::RescanProgress(MainWindow* _main) 
    : main(_main) {
    // Set up the progress dialog
    progress = new QProgressDialog(
                    QObject::tr("Your wallet is rescanning. This will take a long time. Please wait..."), 
                    QString(), 0, 100, main);
    progress->setWindowTitle(QObject::tr("Rescanning"));
    progress->setWindowModality(Qt::WindowModal);
    progress->open();
}

RescanProgress::~RescanProgress() {
    delete ticker;
    delete progress;
}

void RescanProgress::setBusy(const QString& what) {
    progress->setRange(0, 0);       // an indeterminate ("busy") bar
    elapsed.start();
    auto show = [=, this]() {
        qint64 s = elapsed.elapsed() / 1000;
        progress->setLabelText(what % "\n\n" %
            QObject::tr("ycashd is rescanning the block chain for the imported key. It reports no progress "
                        "while it does; on mainnet this can take a long time. YecWallet resumes when it finishes.") %
            "\n\n" % QObject::tr("Elapsed: %1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0')));
    };
    show();
    ticker = new QTimer();
    QObject::connect(ticker, &QTimer::timeout, show);
    ticker->start(1000);
}

void RescanProgress::updateProgress(int tick) {
    if (tick >= 0 && tick < 100)
        progress->setValue(tick);
}

void RescanProgress::closeProgress() {
    if (ticker != nullptr) ticker->stop();
    if (progress->maximum() == 0) progress->setRange(0, 100);
    progress->setValue(100);
}
