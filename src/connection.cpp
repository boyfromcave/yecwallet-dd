#include <QRandomGenerator>
#include <QStorageInfo>
#include "connection.h"
#include "nodecompat.h"
#include "nodedatacheck.h"
#include "mainwindow.h"
#include "settings.h"
#include "ui_connection.h"
#include "ui_createzcashconfdialog.h"
#include "controller.h"

#include "precompiled.h"

using json = nlohmann::json;

ConnectionLoader::ConnectionLoader(MainWindow* main, Controller* rpc) {
    this->main = main;
    this->rpc  = rpc;

    d = new QDialog(main);
    connD = new Ui_ConnectionDialog();
    connD->setupUi(d);
    QPixmap logo(":/img/res/logobig.gif");
    connD->topIcon->setBasePixmap(logo.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

ConnectionLoader::~ConnectionLoader() {    
    delete d;
    delete connD;
}

void ConnectionLoader::loadConnection() {
    QTimer::singleShot(1, [=, this]() { this->doAutoConnect(); });
    if (!Settings::getInstance()->isHeadless())
        d->exec();
}

void ConnectionLoader::doAutoConnect(bool tryEzcashdStart) {
    // Priority 1: Ensure all params are present.
    if (!verifyParams()) {
        downloadParams([=, this]() { this->doAutoConnect(); });
        return;
    }

    // Priority 2: Try to connect to detect ycash.conf and connect to it.
    auto config = autoDetectZcashConf();
    main->logger->write(QObject::tr("Attempting autoconnect"));

    if (config.get() != nullptr) {
        auto connection = makeConnection(config);

        refreshZcashdState(connection, [=, this]() {
            // Refused connection. So try and start embedded ycashd
            if (Settings::getInstance()->useEmbedded()) {
                if (tryEzcashdStart) {
                    // Before the first start: warn if the bundled node would upgrade an older
                    // data directory one way (NodeDataCheck). Quit starts and changes nothing.
                    if (ezcashd == nullptr && !this->confirmNodeDataUpgrade()) {
                        main->logger->write("Quit at the data directory upgrade warning; ycashd was not started");
                        QApplication::quit();
                        return;
                    }
                    this->showInformation(QObject::tr("Starting embedded ycashd"));
                    if (this->startEmbeddedZcashd()) {
                        // Embedded ycashd started up. Wait a second and then refresh the connection
                        main->logger->write("Embedded ycashd started up, trying autoconnect in 1 sec");
                        QTimer::singleShot(1000, [=, this]() { doAutoConnect(); } );
                    } else {
                        if (config->zcashDaemon) {
                            // ycashd is configured to run as a daemon, so we must wait for a few seconds
                            // to let it start up. 
                            main->logger->write("ycashd is daemon=1. Waiting for it to start up");
                            this->showInformation(QObject::tr("ycashd is set to run as daemon"), QObject::tr("Waiting for ycashd"));
                            QTimer::singleShot(5000, [=, this]() { doAutoConnect(/* don't attempt to start ezcashd */ false); });
                        } else {
                            // Something is wrong. 
                            // We're going to attempt to connect to the one in the background one last time
                            // and see if that works, else throw an error
                            main->logger->write("Unknown problem while trying to start ycashd");
                            QTimer::singleShot(2000, [=, this]() { doAutoConnect(/* don't attempt to start ezcashd */ false); });
                        }
                    }
                } else {
                    // We tried to start ezcashd previously, and it didn't work. So, show the error. 
                    main->logger->write("Couldn't start embedded ycashd for unknown reason");
                    QString explanation;
                    if (config->zcashDaemon) {
                        explanation = QString() % QObject::tr("You have ycashd set to start as a daemon, which can cause problems "
                            "with YecWallet\n\n."
                            "Please remove the following line from your ycash.conf and restart YecWallet\n"
                            "daemon=1");
                    } else {
                        explanation = QString() % QObject::tr("Couldn't start the embedded ycashd.\n\n" 
                            "Please try restarting.\n\nIf you previously started ycashd with custom arguments, you might need to reset ycash.conf.\n\n" 
                            "If all else fails, please run ycashd manually.") %  
                            (ezcashd ? QObject::tr("The process returned") + ":\n\n" % ezcashd->errorString() : QString(""));
                    }
                    
                    this->showError(explanation);
                }                
            } else {
                // ycash.conf exists, there's no connection, and the user asked us not to start ycashd. Error!
                main->logger->write("Not using embedded and couldn't connect to ycashd");
                QString explanation = QString() % QObject::tr("Couldn't connect to ycashd configured in ycash.conf.\n\n" 
                                      "Not starting embedded ycashd because --no-embedded was passed");
                this->showError(explanation);
            }
        });
    } else {
        if (Settings::getInstance()->useEmbedded()) {
            // ycash.conf was not found, so create one
            createZcashConf();
        } else {
            // Fall back to manual connect
            doManualConnect();
        }
    } 
}

QString randomPassword() {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    const int passwordLength = 10;
    char* s = new char[passwordLength + 1];

    for (int i = 0; i < passwordLength; ++i) {
        s[i] = alphanum[QRandomGenerator::global()->bounded(quint32(sizeof(alphanum)))];
    }

    s[passwordLength] = 0;
    return QString::fromStdString(s);
}

/**
 * This will create a new ycash.conf, download Ycash parameters.
 */ 
void ConnectionLoader::createZcashConf() {
    main->logger->write("createZcashConf");

    auto confLocation = zcashConfWritableLocation();
    QFileInfo fi(confLocation);

    QDialog d(main);
    Ui_createZcashConf ui;
    ui.setupUi(&d);

    QPixmap logo(":/img/res/zcashdlogo.gif");
    ui.lblTopIcon->setBasePixmap(logo.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui.btnPickDir->setEnabled(false);

    ui.grpAdvanced->setVisible(false);
    QObject::connect(ui.btnAdvancedConfig, &QPushButton::toggled, [=, this](bool isVisible) {
        ui.grpAdvanced->setVisible(isVisible);
        ui.btnAdvancedConfig->setText(isVisible ? QObject::tr("Hide Advanced Config") : QObject::tr("Show Advanced Config"));
    });

    QObject::connect(ui.chkCustomDatadir, &QCheckBox::stateChanged, [=, this](int chked) {
        if (chked == Qt::Checked) {
            ui.btnPickDir->setEnabled(true);
        }
        else {
            ui.btnPickDir->setEnabled(false);
        }
    });

    QObject::connect(ui.btnPickDir, &QPushButton::clicked, [=, this]() {
        auto datadir = QFileDialog::getExistingDirectory(main, QObject::tr("Choose data directory"), ui.lblDirName->text(), QFileDialog::ShowDirsOnly);
        if (!datadir.isEmpty()) {
            ui.lblDirName->setText(QDir::toNativeSeparators(datadir));
        }
    });

    // Show the dialog
    QString datadir = "";
    bool useTor = false;
    if (d.exec() == QDialog::Accepted) {
        datadir = ui.lblDirName->text();
        useTor = ui.chkUseTor->isChecked();
        if (!ui.chkAllowInternet->isChecked()) {
            Settings::getInstance()->setAllowFetchPrices(false);
            Settings::getInstance()->setCheckForUpdates(false);
        }
    }

    main->logger->write("Creating file " + confLocation);
    QDir().mkdir(fi.dir().absolutePath());

    QFile file(confLocation);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        main->logger->write("Could not create ycash.conf, returning");
        return;
    }
        
    QTextStream out(&file); 
    
    // For the current ycash fork, make it follow the tesnet.
    out << "server=1\n";
    out << "addnode=mainnet.ycash.xyz\n";
    out << "rpcuser=ycash\n";
    out << "rpcpassword=" % randomPassword() << "\n";

    // Fast sync override. Written as ibdskiptxverification, which both node lines read
    // (6.20.0 dropped the fastsync alias; nodecompat.h).
    if (ui.chkFastSync->isChecked()) {
        out << NodeCompat::FASTSYNC_CONF_KEY << "=1\n";
    }

    // Datadir override 
    if (!datadir.isEmpty()) {
        out << "datadir=" % datadir % "\n";
    }

    // Tor override
    if (useTor) {
        out << "proxy=127.0.0.1:9050\n";
    }

    file.close();

    // Now that ycash.conf exists, try to autoconnect again
    this->doAutoConnect();
}


void ConnectionLoader::downloadParams(std::function<void(void)> cb) {    
    main->logger->write("Adding params to download queue");
    // Add all the files to the download queue
    downloadQueue = new QQueue<QUrl>();
    client = new QNetworkAccessManager(main);   
    
    downloadQueue->enqueue(QUrl("https://z.cash/downloads/sapling-output.params"));
    downloadQueue->enqueue(QUrl("https://z.cash/downloads/sapling-spend.params"));  
    downloadQueue->enqueue(QUrl("https://z.cash/downloads/sprout-groth16.params"));
    // Comment out downloading the Sprout keys because they were deprecated with https://github.com/zcash/zcash/pull/4060
    // downloadQueue->enqueue(QUrl("https://z.cash/downloads/sprout-proving.key"));
    // downloadQueue->enqueue(QUrl("https://z.cash/downloads/sprout-verifying.key"));

    doNextDownload(cb);    
}

void ConnectionLoader::doNextDownload(std::function<void(void)> cb) {
    auto fnSaveFileName = [&] (QUrl url) {
        QString path = url.path();
        QString basename = QFileInfo(path).fileName();

        return basename;
    };

    if (downloadQueue->isEmpty()) {
        delete downloadQueue;
        client->deleteLater();

        main->logger->write("All Downloads done");
        this->showInformation(QObject::tr("All Downloads Finished Successfully!"));
        cb();
        return;
    }

    QUrl url = downloadQueue->dequeue();
    int filesRemaining = downloadQueue->size();

    QString filename = fnSaveFileName(url);
    QString paramsDir = zcashParamsDir();

    if (QFile(QDir(paramsDir).filePath(filename)).exists()) {
        main->logger->write(filename + " already exists, skipping");
        doNextDownload(cb);

        return;
    }

    // The downloaded file is written to a new name, and then renamed when the operation completes.
    currentOutput = new QFile(QDir(paramsDir).filePath(filename + ".part"));   

    if (!currentOutput->open(QIODevice::WriteOnly)) {
        main->logger->write("Couldn't open " + currentOutput->fileName() + " for writing");
        this->showError(QObject::tr("Couldn't download params. Please check the help site for more info."));
    }
    main->logger->write("Downloading to " + filename);
    qDebug() << "Downloading " << url << " to " << filename;
    
    QNetworkRequest request(url);
    client->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    currentDownload = client->get(request);
    downloadTime.start();
    
    // Download Progress
    QObject::connect(currentDownload, &QNetworkReply::downloadProgress, [=, this](auto done, auto total) {
        // calculate the download speed
        double speed = done * 1000.0 / downloadTime.elapsed();
        QString unit;
        if (speed < 1024) {
            unit = "bytes/sec";
        } else if (speed < 1024*1024) {
            speed /= 1024;
            unit = "kB/s";
        } else {
            speed /= 1024*1024;
            unit = "MB/s";
        }

        this->showInformation(
            QObject::tr("Downloading ") % filename % (filesRemaining > 1 ? " ( +" % QString::number(filesRemaining)  % QObject::tr(" more remaining )") : QString("")),
            QString::number(done/1024/1024, 'f', 0) % QObject::tr("MB of ") % QString::number(total/1024/1024, 'f', 0) + QObject::tr("MB at ") % QString::number(speed, 'f', 2) % unit);
    });
    
    // Download Finished
    QObject::connect(currentDownload, &QNetworkReply::finished, [=, this]() {
        // Rename file
        main->logger->write("Finished downloading " + filename);
        currentOutput->rename(QDir(paramsDir).filePath(filename));

        currentOutput->close();
        currentDownload->deleteLater();
        currentOutput->deleteLater();

        if (currentDownload->error()) {
            main->logger->write("Downloading " + filename + " failed");
            this->showError(QObject::tr("Downloading ") + filename + QObject::tr(" failed. Please check the help site for more info"));                
        } else {
            doNextDownload(cb);
        }
    });

    // Download new data available. 
    QObject::connect(currentDownload, &QNetworkReply::readyRead, [=, this]() {
        currentOutput->write(currentDownload->readAll());
    });    
}

QString ConnectionLoader::embeddedZcashdProgram() {
    QDir appPath(QCoreApplication::applicationDirPath());
#ifdef Q_OS_LINUX
    auto zcashdProgram = appPath.absoluteFilePath("zqw-ycashd");
    if (!QFile(zcashdProgram).exists()) {
        zcashdProgram = appPath.absoluteFilePath("ycashd");
    }
#elif defined(Q_OS_DARWIN)
    auto zcashdProgram = appPath.absoluteFilePath("ycashd");
#else
    auto zcashdProgram = appPath.absoluteFilePath("ycashd.exe");
#endif
    return zcashdProgram;
}

/**
 * The one-way upgrade warning (owner decision 2026-10-01). Runs before the embedded ycashd is
 * first started, never with --no-embedded or when a node already answers (doAutoConnect only
 * gets here after the connection was refused). Returns false when the user chose Quit, or the
 * wallet.dat backup they asked for failed; nothing has been written then.
 *
 * Test hook: YECWALLET_UPGRADE_ANSWER=backup|continue|quit answers without showing the dialog,
 * honoured only under YECWALLET_TEST_ISOLATE (the test-isolation gate, main.cpp); in a normal
 * run the variable is ignored and logged, so a launcher or profile cannot pre-answer the
 * one-way upgrade (audit F-5).
 */
bool ConnectionLoader::confirmNodeDataUpgrade() {
    auto program = embeddedZcashdProgram();
    if (!QFile(program).exists())
        return true;    // startEmbeddedZcashd reports the missing binary

    // Which line is bundled: `ycashd --version` prints and exits before it reads any conf or
    // data directory. A package that bundles a v4.5.0 node never warns.
    int bundled = 0;
    {
        QProcess p;
        p.start(program, QStringList() << "--version");
        if (p.waitForFinished(5000))
            bundled = NodeDataCheck::versionFromText(QString::fromUtf8(p.readAllStandardOutput()));
        else
            p.kill();
    }
    if (bundled != 0 && bundled < NodeDataCheck::UPGRADING_MIN_VERSION)
        return true;
    if (bundled == 0)
        bundled = NodeDataCheck::UPGRADING_MIN_VERSION + 50;   // assume the 6.20.0 line
    QString bundledText = QString("%1.%2.%3").arg(bundled / 1000000)
                              .arg((bundled / 10000) % 100).arg((bundled / 100) % 100);

    auto confLocation = Settings::getInstance()->getZcashdConfLocation();
    auto dirs   = NodeDataCheck::resolve(confLocation);
    auto result = NodeDataCheck::inspect(dirs.netDir);
    main->logger->write("Data directory check (" + dirs.netDir + "): " + result.reason);
    if (!NodeDataCheck::needsWarning(result.state))
        return true;

    enum class Answer { Backup, Continue, Quit };
    Answer answer = Answer::Quit;
    const bool hasWallet = QFile::exists(dirs.walletPath);
    QString hook = qEnvironmentVariable("YECWALLET_UPGRADE_ANSWER").toLower();
    if (!hook.isEmpty() && !qEnvironmentVariableIsSet("YECWALLET_TEST_ISOLATE")) {
        main->logger->write("YECWALLET_UPGRADE_ANSWER ignored: not a test run (YECWALLET_TEST_ISOLATE is not set)");
        hook.clear();
    }
    const bool hooked = !hook.isEmpty();

    if (hooked) {
        answer = hook == "backup" ? Answer::Backup : hook == "continue" ? Answer::Continue : Answer::Quit;
        main->logger->write("YECWALLET_UPGRADE_ANSWER=" + hook);
    } else {
        QLocale locale;
        QStorageInfo storage(dirs.netDir);
        QString text = QObject::tr(
            "This version of YecWallet runs ycashd %1, which upgrades the node data in\n%2\n\n"
            "Older versions of YecWallet and ycashd (4.5.0 and earlier) will not be able to use this "
            "data directory afterwards without a full reindex (re-sync). The upgrade itself is also a "
            "reindex. On mainnet each reindex takes a long time, typically several hours.\n\n"
            "Your wallet.dat and the keys in it are kept.")
            .arg(bundledText, QDir::toNativeSeparators(dirs.netDir));
        if (result.state == NodeDataCheck::State::Unknown)
            text += "\n\n" + QObject::tr("YecWallet could not tell which ycashd version last used this "
                "data directory. If it was %1 or later, nothing is upgraded.").arg(bundledText);
        if (result.marked)
            text += "\n\n" + QObject::tr("You accepted this upgrade before, but the data directory still shows an "
                "older ycashd as the last to use it: the upgrade did not complete. It starts again now.");
        QString detail = QObject::tr(
            "To keep a copy that an older version can open without a reindex, quit now and copy the "
            "whole data directory (%1, about %2; %3 free on that disk). YecWallet does not make that copy.")
            .arg(QDir::toNativeSeparators(dirs.netDir),
                 locale.formattedDataSize(NodeDataCheck::directorySize(dirs.netDir)),
                 locale.formattedDataSize(storage.bytesAvailable()));

        QMessageBox box(QMessageBox::Warning, QObject::tr("Upgrade the node data?"), text,
                        QMessageBox::NoButton, main);
        box.setInformativeText(detail);
        QPushButton* backupBtn = hasWallet
            ? box.addButton(QObject::tr("Back up wallet.dat and continue"), QMessageBox::AcceptRole)
            : nullptr;
        QPushButton* continueBtn = box.addButton(QObject::tr("Continue without backup"), QMessageBox::DestructiveRole);
        QPushButton* quitBtn = box.addButton(QObject::tr("Quit"), QMessageBox::RejectRole);
        box.setDefaultButton(backupBtn ? backupBtn : quitBtn);
        box.setEscapeButton(quitBtn);
        box.exec();
        auto clicked = box.clickedButton();
        answer = (backupBtn && clicked == backupBtn) ? Answer::Backup
               : clicked == continueBtn              ? Answer::Continue
               :                                       Answer::Quit;
    }

    if (answer == Answer::Quit)
        return false;

    if (answer == Answer::Backup && hasWallet) {
        QString error;
        auto backup = NodeDataCheck::backupWallet(dirs.walletPath, QDateTime::currentDateTime(), &error);
        if (backup.isEmpty()) {
            main->logger->write("wallet.dat backup failed: " + error);
            if (!hooked)
                QMessageBox::critical(main, QObject::tr("Backup failed"),
                    QObject::tr("Could not back up %1:\n%2\n\nycashd was not started.")
                        .arg(QDir::toNativeSeparators(dirs.walletPath), error));
            return false;
        }
        main->logger->write("wallet.dat backed up to " + backup);
        if (!hooked)
            QMessageBox::information(main, QObject::tr("wallet.dat backed up"),
                QObject::tr("wallet.dat was copied to\n%1").arg(QDir::toNativeSeparators(backup)));
    }

    QString error;
    if (!NodeDataCheck::writeMarker(dirs.netDir, bundledText, &error))
        main->logger->write("Could not write the data directory marker: " + error);

    // A block index the log shows an older node wrote cannot be read by this node: start the
    // reindex now instead of letting the first start fail ("restart with -reindex", the fallback
    // in startEmbeddedZcashd, which still covers the Unknown case). Controller::setConnection
    // removes reindex=1 again once the node answers.
    if (result.state == NodeDataCheck::State::Older)
        Settings::addToZcashConf(confLocation, "reindex=1");
    return true;
}

bool ConnectionLoader::startEmbeddedZcashd() {
    if (!Settings::getInstance()->useEmbedded()) 
        return false;
    
    main->logger->write("Trying to start embedded ycashd");

    // Static because it needs to survive even after this method returns.
    static QString processStdErrOutput;

    if (ezcashd != nullptr) {
        if (ezcashd->state() == QProcess::NotRunning) {
            if (!processStdErrOutput.isEmpty()) {
                QMessageBox::critical(main, QObject::tr("ycashd error"), "ycashd said: " + processStdErrOutput, 
                                      QMessageBox::Ok);
                if (processStdErrOutput.toLower().contains("-reindex")) {
                    Settings::addToZcashConf(Settings::getInstance()->getZcashdConfLocation(), "reindex=1");
                }                                    
            }
            return false;
        } else {
            return true;
        }        
    }

    // Finally, start ycashd    
    QDir appPath(QCoreApplication::applicationDirPath());
    auto zcashdProgram = embeddedZcashdProgram();
    
    if (!QFile(zcashdProgram).exists()) {
        qDebug() << "Can't find ycashd at " << zcashdProgram;
        main->logger->write("Can't find ycashd at " + zcashdProgram); 
        return false;
    }

    ezcashd = new QProcess(main);    

    QObject::connect(ezcashd, &QProcess::errorOccurred, [&] (auto error) {
        qDebug() << "Couldn't start ycashd: " << error;
    });

    QObject::connect(ezcashd, &QProcess::readyReadStandardError, [&]() {
        auto output = ezcashd->readAllStandardError();
        main->logger->write("ycashd stderr:" + output);
        processStdErrOutput += output;
    });


    // --conf PATH names a file other than the default: the node reads that one too, so the
    // directory the data check inspected and the reindex=1/rescan=1 the wallet appends are the
    // ones the node acts on (audit F-10). With the default conf nothing is passed, as before.
    QStringList args;
    const QString confLocation = Settings::getInstance()->getZcashdConfLocation();
    if (!confLocation.isEmpty() &&
        QFileInfo(confLocation).absoluteFilePath() != QFileInfo(zcashConfWritableLocation()).absoluteFilePath()) {
        args << "-conf=" + QFileInfo(confLocation).absoluteFilePath();
        main->logger->write("Starting ycashd with " + args.join(' '));
    }

#ifdef Q_OS_LINUX
    ezcashd->start(zcashdProgram, args);
#elif defined(Q_OS_DARWIN)
    ezcashd->start(zcashdProgram, args);
#else
    ezcashd->setWorkingDirectory(appPath.absolutePath());
    ezcashd->start("ycashd.exe", args);
#endif // Q_OS_LINUX


    return true;
}

void ConnectionLoader::doManualConnect() {
    auto config = loadFromSettings();

    if (!config) {
        // Nothing configured, show an error
        QString explanation = QString()
                % QObject::tr("A manual connection was requested, but the settings are not configured.\n\n"
                "Please set the host/port and user/password in the Edit->Settings menu.");

        showError(explanation);
        doRPCSetConnection(nullptr);

        return;
    }

    auto connection = makeConnection(config);
    refreshZcashdState(connection, [=, this]() {
        QString explanation = QString()
                % QObject::tr("Could not connect to ycashd configured in settings.\n\n" 
                "Please set the host/port and user/password in the Edit->Settings menu.");

        showError(explanation);
        doRPCSetConnection(nullptr);

        return;
    });
}

void ConnectionLoader::doRPCSetConnection(Connection* conn) {
    // Before passing on the ezcashd to the rpc controller, disconnect all connections first.
    // With --no-embedded there is no embedded node and ezcashd is null (a member call on a null
    // pointer, which Qt caught as "QObject::disconnect: Unexpected nullptr parameter").
    if (ezcashd != nullptr) ezcashd->disconnect();

    rpc->setEZcashd(ezcashd);
    rpc->setConnection(conn);
    
    d->accept();

    delete this;
}

Connection* ConnectionLoader::makeConnection(std::shared_ptr<ConnectionConfig> config) {
    QNetworkAccessManager* client = new QNetworkAccessManager(main);
         
    QUrl myurl;
    myurl.setScheme("http");
    myurl.setHost(config.get()->host);
    myurl.setPort(config.get()->port.toInt());

    QNetworkRequest* request = new QNetworkRequest();
    request->setUrl(myurl);
    request->setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    
    QString userpass = config.get()->rpcuser % ":" % config.get()->rpcpassword;
    QString headerData = "Basic " + userpass.toLocal8Bit().toBase64();
    request->setRawHeader("Authorization", headerData.toLocal8Bit());    

    return new Connection(main, client, request, config);
}

void ConnectionLoader::refreshZcashdState(Connection* connection, std::function<void(void)> refused) {
    // We'll first try getrescaninfo, since a rescan might block all other RPC calls. However, the ycashd
    // might be old and not support the RPC call, so a failure there means we fall back to getinfo

    auto fnSuccess = [=, this]() {
        // Success, hide the dialog if it was shown. 
        d->hide();
        main->logger->write("ycashd is online.");
        this->doRPCSetConnection(connection);
    };

    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "getrescaninfo"}
    };
    connection->doRPCSafe(payload,
        [=, this](auto) {
            // Success
            fnSuccess();
        },
        [=, this](auto, auto) {
            // If the rescan failed, this ycashd might not support the new RPC,
            // so fall back to getinfo
            json payload = {
                {"jsonrpc", "1.0"},
                {"id", "someid"},
                {"method", "getinfo"}
            };
            connection->doRPCSafe(payload,
                [=, this](auto reply) {
                    // Success. A node without getrescaninfo may be a Ycash 6.20.0 node: keep
                    // its version, which selects the 6.20.0 call shapes (nodecompat.h).
                    connection->nodeVersion = NodeCompat::versionFromGetinfo(reply);
                    if (NodeCompat::isYcash6(connection->nodeVersion))
                        main->logger->write("ycashd " + QString::number(connection->nodeVersion) +
                                            ": using the Ycash 6.20.0 RPC shapes");
                    else if (connection->nodeVersion == 0)
                        main->logger->write("Warning: getinfo carries no numeric version; the node is driven "
                                            "with the v4.5.0 RPC shapes (audit F-11)");
                    fnSuccess();
                },
                [=, this](auto reply, auto res) {            
                    // Failed, see what it is. 
                    auto err = reply->error();
                    //qDebug() << err << ":" << QString::fromStdString(res.dump());

                    if (err == QNetworkReply::NetworkError::ConnectionRefusedError) {   
                        refused();
                    } else if (err == QNetworkReply::NetworkError::AuthenticationRequiredError) {
                        main->logger->write("Authentication failed");
                        QString explanation = QString() % 
                                QObject::tr("Authentication failed. The username / password you specified was "
                                "not accepted by ycashd. Try changing it in the Edit->Settings menu");

                        this->showError(explanation);
                    } else if (err == QNetworkReply::NetworkError::InternalServerError && 
                            !res.is_discarded()) {
                        // The server is loading, so just poll until it succeeds
                        QString status      = QString::fromStdString(res["error"]["message"]);
                        {
                            static int dots = 0;
                            status = status.left(status.length() - 3) + QString(".").repeated(dots);
                            dots++;
                            if (dots > 3)
                                dots = 0;
                        }
                        this->showInformation(QObject::tr("Your ycashd is starting up. Please wait."), status);
                        main->logger->write("Waiting for ycashd to come online.");
                        // Refresh after one second
                        QTimer::singleShot(1000, [=, this]() { this->refreshZcashdState(connection, refused); });
                    }
                }
            );
        }
    );

    
}

// Update the UI with the status
void ConnectionLoader::showInformation(QString info, QString detail) {
    static int rescanCount = 0;
    if (detail.toLower().startsWith("rescan")) {
        rescanCount++;
    }
    
    if (rescanCount > 10) {
        detail = detail + "\n" + QObject::tr("This may take several hours");
    }

    connD->status->setText(info);
    connD->statusDetail->setText(detail);

    if (rescanCount < 10)
        main->logger->write(info + ":" + detail);
}

/**
 * Show error will close the loading dialog and show an error. 
*/
void ConnectionLoader::showError(QString explanation) {    
    rpc->setEZcashd(nullptr);
    rpc->noConnection();

    QMessageBox::critical(main, QObject::tr("Connection Error"), explanation, QMessageBox::Ok);
    d->close();
}

QString ConnectionLoader::locateZcashConfFile() {
#ifdef Q_OS_LINUX
    auto confLocation = QStandardPaths::locate(QStandardPaths::HomeLocation, ".ycash/ycash.conf");
#elif defined(Q_OS_DARWIN)
    auto confLocation = QStandardPaths::locate(QStandardPaths::HomeLocation, "Library/Application Support/Ycash/ycash.conf");
#else
    auto confLocation = QStandardPaths::locate(QStandardPaths::AppDataLocation, "../../Ycash/ycash.conf");
#endif

    main->logger->write("Found zcashconf at " + QDir::cleanPath(confLocation));
    return QDir::cleanPath(confLocation);
}

QString ConnectionLoader::zcashConfWritableLocation() {
#ifdef Q_OS_LINUX
    auto confLocation = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath(".ycash/ycash.conf");
#elif defined(Q_OS_DARWIN)
    auto confLocation = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath("Library/Application Support/Ycash/ycash.conf");
#else
    auto confLocation = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("../../Ycash/ycash.conf");
#endif

    main->logger->write("Found zcashconf at " + QDir::cleanPath(confLocation));
    return QDir::cleanPath(confLocation);
}

QString ConnectionLoader::zcashParamsDir() {
    #ifdef Q_OS_LINUX
    auto paramsLocation = QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath(".zcash-params"));
#elif defined(Q_OS_DARWIN)
    auto paramsLocation = QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath("Library/Application Support/ZcashParams"));
#else
    auto paramsLocation = QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("../../ZcashParams"));
#endif

    if (!paramsLocation.exists()) {
        main->logger->write("Creating params location at " + paramsLocation.absolutePath());
        QDir().mkpath(paramsLocation.absolutePath());
    }

    main->logger->write("Found Ycash params directory at " + paramsLocation.absolutePath());
    return paramsLocation.absolutePath();
}

bool ConnectionLoader::verifyParams() {
    QDir paramsDir(zcashParamsDir());

    if (!QFile(paramsDir.filePath("sapling-output.params")).exists()) return false;
    if (!QFile(paramsDir.filePath("sapling-spend.params")).exists()) return false;
    if (!QFile(paramsDir.filePath("sprout-groth16.params")).exists()) return false;
    // Comment out downloading the Sprout keys because they were deprecated with https://github.com/zcash/zcash/pull/4060
    // if (!QFile(paramsDir.filePath("sprout-proving.key")).exists()) return false;
    // if (!QFile(paramsDir.filePath("sprout-verifying.key")).exists()) return false;

    return true;
}

/**
 * Try to automatically detect a ycash.conf file in the correct location and load parameters
 */ 
std::shared_ptr<ConnectionConfig> ConnectionLoader::autoDetectZcashConf() {    
    auto confLocation = Settings::getInstance()->getZcashdConfLocation();

    if (confLocation.isEmpty()) {
        confLocation = locateZcashConfFile();
    }

    if (confLocation.isNull()) {
        // No Ycash file, just return with nothing
        return nullptr;
    }

    QFile file(confLocation);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << file.errorString();
        return nullptr;
    }

    QTextStream in(&file);

    auto zcashconf = new ConnectionConfig();
    zcashconf->host     = "127.0.0.1";
    zcashconf->connType = ConnectionType::DetectedConfExternalZcashD;
    zcashconf->usingZcashConf = true;
    zcashconf->zcashDir = QFileInfo(confLocation).absoluteDir().absolutePath();
    zcashconf->zcashDaemon = false;

    Settings::getInstance()->setUsingZcashConf(confLocation);

    while (!in.atEnd()) {
        QString line = in.readLine();
        auto s = line.indexOf("=");
        QString name  = line.left(s).trimmed().toLower();
        QString value = line.right(line.length() - s - 1).trimmed();

        if (name == "rpcuser") {
            zcashconf->rpcuser = value;
        }
        if (name == "rpcpassword") {
            zcashconf->rpcpassword = value;
        }
        if (name == "rpcport") {
            zcashconf->port = value;
        }
        if (name == "daemon" && value == "1") {
            zcashconf->zcashDaemon = true;
        }
        if (name == "proxy") {
            zcashconf->proxy = value;
        }
        if (name == "testnet" &&
            value == "1"  &&
            zcashconf->port.isEmpty()) {
                zcashconf->port = "18832";
        }
        if (NodeCompat::isFastSyncConfKey(name) && value == "1") {
            zcashconf->fastsync = true;
        }
    }

    // If rpcport is not in the file, and it was not set by the testnet=1 flag, then go to default
    if (zcashconf->port.isEmpty()) zcashconf->port = "8832";
    file.close();

    // In addition to the ycash.conf file, also double check the params. 

    return std::shared_ptr<ConnectionConfig>(zcashconf);
}

/**
 * Load connection settings from the UI, which indicates an unknown, external ycashd
 */ 
std::shared_ptr<ConnectionConfig> ConnectionLoader::loadFromSettings() {
    // Load from the QT Settings. 
    QSettings s;
    
    auto host        = s.value("connection/host").toString();
    auto port        = s.value("connection/port").toString();
    auto username    = s.value("connection/rpcuser").toString();
    auto password    = s.value("connection/rpcpassword").toString();    

    if (username.isEmpty() || password.isEmpty())
        return nullptr;

    auto uiConfig = new ConnectionConfig{ host, port, username, password, false, false, false, "", "", ConnectionType::UISettingsZCashD};

    return std::shared_ptr<ConnectionConfig>(uiConfig);
}





/***********************************************************************************
 *  Connection Class
 ************************************************************************************/ 
Connection::Connection(MainWindow* m, QNetworkAccessManager* c, QNetworkRequest* r, 
                        std::shared_ptr<ConnectionConfig> conf) {
    this->restclient  = c;
    this->request     = r;
    this->config      = conf;
    this->main        = m;
}

Connection::~Connection() {
    delete restclient;
    delete request;
}

void Connection::doRPCDirect(const json& payload, const std::function<void(json)>& cb, 
                                const std::function<void(QNetworkReply*, const json&)>& ne) {
    if (shutdownInProgress) {
        // Ignoring RPC because shutdown in progress
        return;
    }

    QNetworkReply *reply = restclient->post(*request, QByteArray::fromStdString(payload.dump()));

    QObject::connect(reply, &QNetworkReply::finished, [=, this] {
        reply->deleteLater();
        if (shutdownInProgress) {
            // Ignoring callback because shutdown in progress
            return;
        }
        
        if (reply->error() != QNetworkReply::NoError) {
            auto parsed = json::parse(reply->readAll(), nullptr, false);
            ne(reply, parsed);
            
            return;
        } 
        
        auto parsed = json::parse(reply->readAll(), nullptr, false);
        if (parsed.is_discarded()) {
            ne(reply, "Unknown error");
        }
        
        cb(parsed["result"]);        
    });
}

/**
 * Do a safe RPC call so that it doesn't accidentally block. If the ycashd is doing a 
 * rescan, other RPC calls are likely to block till the rescan is finished, which might
 * be several hours. So, check to see if there is a rescan, and if there isn't, then
 * call the RPC.
 */
void Connection::doRPCSafe(const json& payload, const std::function<void(json)>& cb, 
                            const std::function<void(QNetworkReply*, const json&)>& ne) {
    // Allow the rescan request to go through
    QString payloadMethod = QString::fromStdString(payload["method"].get<json::string_t>());
    if (payloadMethod == "getrescaninfo" || payloadMethod == "stop") {
        doRPCDirect(payload, cb, ne);
        return;
    }

    // Ycash 6.20.0 has no getrescaninfo: a rescan runs only inside an import RPC (or at
    // startup), so there is nothing to ask. Calls go straight through, except while the
    // wallet's own import is rescanning, when they are dropped as below.
    if (NodeCompat::isYcash6(nodeVersion)) {
        if (syncRescanInFlight)
            return;
        doRPCDirect(payload, cb, ne);
        return;
    }

    // Check rescaninfo first
    json rescanPayload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "getrescaninfo"}
    };
    doRPCDirect(rescanPayload, 
        [=, this](const json& reply) {
            // (main is null only in the offline QTest's Connection, nodecompat_test)
            if (reply["rescanning"].get<json::boolean_t>()) {
                if (this->main != nullptr) this->main->getRPC()->refreshRescanStatus();
                return;
            } else {
                if (this->main != nullptr) this->main->getRPC()->closeRefreshStatusIfAlive();
                this->doRPCDirect(payload, cb, ne);
            }
        },
        [=, this](auto, auto) {
            // If it errors out, then the ycashd probably doesn't support it yet,
            // so just do the original thing
            this->doRPCDirect(payload, cb, ne);
        }
    );
}

void Connection::doRPCWithDefaultErrorHandling(const json& payload, const std::function<void(json)>& cb) {
    doRPCSafe(payload, cb, [=, this](auto reply, auto parsed) {
        if (!parsed.is_discarded() && !parsed["error"]["message"].is_null()) {
            this->showTxError(QString::fromStdString(parsed["error"]["message"]));    
        } else {
            this->showTxError(reply->errorString());
        }
    });
}

void Connection::doRPCIgnoreError(const json& payload, const std::function<void(json)>& cb) {
    doRPCSafe(payload, cb, [=, this](auto, auto) {
        // Ignored error handling
    });
}

void Connection::showTxError(const QString& error) {
    if (error.isNull()) return;

    // Prevent multiple dialog boxes from showing, because they're all called async
    static bool shown = false;
    if (shown)
        return;

    shown = true;
    QMessageBox::critical(main, QObject::tr("Transaction Error"), QObject::tr("There was an error sending the transaction. The error was:") + "\n\n"
        + error, QMessageBox::StandardButton::Ok);
    shown = false;
}

/**
 * Prevent all future calls from going through
 */ 
void Connection::shutdown() {
    shutdownInProgress = true;
}
