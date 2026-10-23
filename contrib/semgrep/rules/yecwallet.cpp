// Test cases for yecwallet.yaml (`semgrep --test contrib/semgrep/rules`). Never compiled.

void logging(Logger* logger, Config* conf, QString rpcPassword, QString host) {
    // ruleid: yecwallet.secrets.secret-logged
    logger->write("password: " + rpcPassword);
    // ruleid: yecwallet.secrets.secret-logged
    qDebug() << "connecting with" << conf->rpcpassword;
    // ruleid: yecwallet.secrets.secret-logged
    logger->write(QString("seed %1").arg(seedPhrase));
    // ok: yecwallet.secrets.secret-logged
    logger->write("Wrong rpcpassword in ycash.conf");
    // ok: yecwallet.secrets.secret-logged
    qDebug() << "Connecting to" << host;
}

void settings(QSettings& s, QSettings* p, QString password, QString host) {
    // ruleid: yecwallet.secrets.secret-in-qsettings
    s.setValue("connection/rpcpassword", password);
    // ruleid: yecwallet.secrets.secret-in-qsettings
    p->setValue("wallet/seedphrase", words);
    // ok: yecwallet.secrets.secret-in-qsettings
    s.setValue("connection/host", host);
}

void launch(QProcess* p, QProcess& q, QString program, QStringList args, QString cmdline) {
    // ruleid: yecwallet.process.shell-or-command-string
    system(cmdline.toStdString().c_str());
    // ruleid: yecwallet.process.shell-or-command-string
    p->start("sh", QStringList() << "-c" << cmdline);
    // ruleid: yecwallet.process.shell-or-command-string
    q.startCommand(cmdline);
    // ruleid: yecwallet.process.shell-or-command-string
    QProcess::startDetached(cmdline);
    // ok: yecwallet.process.shell-or-command-string
    p->start(program, args);
    // ok: yecwallet.process.shell-or-command-string
    QProcess::startDetached(program, args);
}

void tls(QSslSocket* sock, QNetworkReply* reply, QSslConfiguration conf, QList<QSslError> expected) {
    // ruleid: yecwallet.tls.verification-disabled
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    // ruleid: yecwallet.tls.verification-disabled
    reply->ignoreSslErrors();
    // ok: yecwallet.tls.verification-disabled
    reply->ignoreSslErrors(expected);
    // ok: yecwallet.tls.verification-disabled
    conf.setPeerVerifyMode(QSslSocket::VerifyPeer);
}
