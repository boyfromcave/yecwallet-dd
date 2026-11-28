// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
//
// Offline QTest for randomPassword(), the rpcpassword YecWallet writes into a freshly created
// ycash.conf (audit 06/W-1): the generator used to index the alphabet with sizeof(literal), which
// counts the terminating NUL, so ~15 % of passwords were truncated and ~1.6 % were empty.
// And yugenHelperArgs(), the -yugen* flags the embedded ycashd gets for the bundled Yugen helpers.

#include <QtTest>
#include <QSet>
#include <QTemporaryDir>

#include "connection.h"

static const QString ALPHABET = QStringLiteral(
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz");

class ConnectionTest : public QObject {
    Q_OBJECT
private slots:
    void lengthAndAlphabet();
    void firstCharacterCoversAlphabet();
    void yugenHelpersAbsent();
    void yugenHelpersPresent();
    void yugenHelpersConfWins();
};

static void makeExecutable(const QString& path, bool exec = true) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\n");
    f.close();
    auto perms = QFile::ReadOwner | QFile::WriteOwner;
    if (exec) perms |= QFile::ExeOwner;
    QVERIFY(f.setPermissions(perms));
}

#ifdef Q_OS_WIN
static const QString EXE = QStringLiteral(".exe");
#else
static const QString EXE;
#endif

void ConnectionTest::yugenHelpersAbsent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeExecutable(dir.filePath("ycashd" + EXE));
    QVERIFY(yugenHelperArgs(dir.path(), {}).isEmpty());
}

void ConnectionTest::yugenHelpersPresent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeExecutable(dir.filePath("yugen-leafverify" + EXE));
    makeExecutable(dir.filePath("yugen-sendbuild" + EXE));
    makeExecutable(dir.filePath("yewd-yugen" + EXE));
    const QString d = QDir(dir.path()).absolutePath();
    const QStringList args = yugenHelperArgs(dir.path(), {});
    QCOMPARE(args, QStringList()
        << "-yugenverifier=" + d + "/yugen-leafverify" + EXE
        << "-yugenbuilder=" + d + "/yugen-sendbuild" + EXE
        << "-yugenprover=" + d + "/yewd-yugen" + EXE);
#ifndef Q_OS_WIN
    // A file that is not executable is not a helper.
    QTemporaryDir dir2;
    makeExecutable(dir2.filePath("yewd-yugen"), false);
    QVERIFY(yugenHelperArgs(dir2.path(), {}).isEmpty());
#endif
}

void ConnectionTest::yugenHelpersConfWins() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeExecutable(dir.filePath("yugen-leafverify" + EXE));
    makeExecutable(dir.filePath("yugen-sendbuild" + EXE));
    makeExecutable(dir.filePath("yewd-yugen" + EXE));
    const QString conf = dir.filePath("ycash.conf");
    QFile f(conf);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("rpcuser=u\n# yugenbuilder=/commented/out\nYugenProver = /my/own/yewd-yugen\n");
    f.close();
    const QSet<QString> keys = confOptionNames(conf);
    QVERIFY(keys.contains("yugenprover"));
    QVERIFY(!keys.contains("yugenbuilder"));
    const QString d = QDir(dir.path()).absolutePath();
    QCOMPARE(yugenHelperArgs(dir.path(), keys), QStringList()
        << "-yugenverifier=" + d + "/yugen-leafverify" + EXE
        << "-yugenbuilder=" + d + "/yugen-sendbuild" + EXE);
    QVERIFY(confOptionNames(dir.filePath("missing.conf")).isEmpty());
}

void ConnectionTest::lengthAndAlphabet() {
    QCOMPARE(ALPHABET.size(), 62);
    for (int n = 0; n < 10000; ++n) {
        const QString p = randomPassword();
        QCOMPARE(p.size(), 24);
        QVERIFY(!p.contains(QChar(0)));
        for (const QChar c : p)
            QVERIFY2(ALPHABET.contains(c), qPrintable(QStringLiteral("unexpected character in %1").arg(p)));
    }
}

// 10,000 draws over 62 symbols: P(some symbol never appears first) < 62 * (61/62)^10000 < 1e-60.
void ConnectionTest::firstCharacterCoversAlphabet() {
    QSet<QChar> seen;
    for (int n = 0; n < 10000; ++n)
        seen.insert(randomPassword().at(0));
    QCOMPARE(seen.size(), 62);
}

QTEST_MAIN(ConnectionTest)
#include "connection_test.moc"
