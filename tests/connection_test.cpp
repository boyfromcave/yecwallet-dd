// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
//
// Offline QTest for randomPassword(), the rpcpassword YecWallet writes into a freshly created
// ycash.conf (audit 06/W-1): the generator used to index the alphabet with sizeof(literal), which
// counts the terminating NUL, so ~15 % of passwords were truncated and ~1.6 % were empty.

#include <QtTest>
#include <QSet>

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
};

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
