// Offline QTest for the version-aware node calls (ycash6 plan Phase 7, owner decision: one wallet
// for both ycashd lines). Runs under QT_QPA_PLATFORM=offscreen with no node.
//
// The pure cases check NodeCompat's call shapes for v4.5.0 (unchanged, byte for byte against the
// literal JSON the wallet sent before) and for 6.20.0. The wire cases put a real Connection and
// ZcashdRPC in front of a loopback mock ycashd (MockNode: an HTTP/1.1 JSON-RPC server that
// answers from a table and records every request) and check what actually goes over the wire on
// each line: the getrescaninfo pre-call, the z_importivk order, the importprivkey arity, the
// held-back calls while a 6.20.0 import is rescanning, and the import error path.

#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>

#include "nodecompat.h"
#include "connection.h"
#include "zcashdrpc.h"
#include "settings.h"

using json = nlohmann::json;

static const int V450 = 4050050;    // ref/ycash/configure.ac:3-6
static const int V620 = 6200050;    // ref/ycash6/src/clientversion.h:18-21

// ── The mock ycashd ───────────────────────────────────────────────────────────────────────
struct MockNode {
    struct Call { std::string method; json params; };
    QTcpServer                 server;
    QList<Call>                calls;
    QMap<QString, json>        results;     // method -> result
    QMap<QString, QString>     errors;      // method -> error message (HTTP 500, code -4)
    QMap<QTcpSocket*, QByteArray> buffers;

    MockNode() {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server, &QTcpServer::newConnection, [this]() {
            while (auto sock = server.nextPendingConnection()) {
                QObject::connect(sock, &QTcpSocket::readyRead, [this, sock]() { onRead(sock); });
                QObject::connect(sock, &QTcpSocket::disconnected, [this, sock]() {
                    buffers.remove(sock);
                    sock->deleteLater();
                });
            }
        });
    }

    ~MockNode() {
        // The sockets are the server's children and outlive `buffers`: unhook them first
        for (auto sock : server.findChildren<QTcpSocket*>())
            QObject::disconnect(sock, nullptr, nullptr, nullptr);
    }

    void onRead(QTcpSocket* sock) {
        QByteArray& buf = buffers[sock];
        buf += sock->readAll();
        for (;;) {
            int end = buf.indexOf("\r\n\r\n");
            if (end < 0) return;
            int length = 0;
            for (const QByteArray& line : buf.left(end).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toInt();
            }
            if (buf.size() < end + 4 + length) return;
            QByteArray body = buf.mid(end + 4, length);
            buf.remove(0, end + 4 + length);
            answer(sock, body);
        }
    }

    void answer(QTcpSocket* sock, const QByteArray& body) {
        json req = json::parse(body.toStdString(), nullptr, false);
        std::string method = req.is_object() ? req.value("method", std::string()) : std::string();
        json params = (req.is_object() && req.find("params") != req.end()) ? req["params"] : json(nullptr);
        calls.append({method, params});

        QString m = QString::fromStdString(method);
        int status = 200;
        json reply;
        if (errors.contains(m)) {
            status = 500;
            reply = {{"result", nullptr}, {"error", {{"code", -4}, {"message", errors[m].toStdString()}}}, {"id", "someid"}};
        } else if (results.contains(m)) {
            reply = {{"result", results[m]}, {"error", nullptr}, {"id", "someid"}};
        } else {
            // What ycashd answers for an unknown method (src/httprpc.cpp: RPC_METHOD_NOT_FOUND -> 404)
            status = 404;
            reply = {{"result", nullptr}, {"error", {{"code", -32601}, {"message", "Method not found"}}}, {"id", "someid"}};
        }
        QByteArray out = QByteArray::fromStdString(reply.dump()) + "\n";
        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) +
            (status == 200 ? " OK" : status == 404 ? " Not Found" : " Internal Server Error") +
            "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size()) + "\r\n\r\n";
        sock->write(head + out);
    }

    QStringList methods() const {
        QStringList l;
        for (auto& c : calls) l << QString::fromStdString(c.method);
        return l;
    }
    json lastParams(const char* method) const {
        json p;
        for (auto& c : calls) if (c.method == method) p = c.params;
        return p;
    }
};

// A ZcashdRPC whose Connection posts to the mock (main is null; Connection::doRPCSafe skips the
// rescan-dialog calls then). The mock answers getrescaninfo as a v4.5.0 node does when no rescan
// runs, and does not know it on 6.20.0 (404, "Method not found").
struct Wire {
    MockNode   node;
    ZcashdRPC  rpc;
    Connection* conn;
    explicit Wire(int nodeVersion) {
        auto nam = new QNetworkAccessManager();
        auto req = new QNetworkRequest();
        req->setUrl(QUrl(QString("http://127.0.0.1:%1/").arg(node.server.serverPort())));
        req->setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
        auto cfg = std::make_shared<ConnectionConfig>();
        conn = new Connection(nullptr, nam, req, cfg);
        conn->nodeVersion = nodeVersion;
        rpc.setConnection(conn);    // rpc owns it
        node.results["z_importivk"]    = json{{"address", "ys1addr"}};
        node.results["importprivkey"]  = "s1taddr";
        node.results["z_importkey"]    = json{{"type", "sapling"}, {"address", "ys1addr"}};
        node.results["z_importviewingkey"] = json{{"type", "sapling"}, {"address", "ys1addr"}};
        node.results["getinfo"]        = json{{"version", nodeVersion}, {"blocks", 10}, {"connections", 1}};
        if (!NodeCompat::isYcash6(nodeVersion))
            node.results["getrescaninfo"] = json{{"rescanning", false}};
    }
};

// ── The devnet case ───────────────────────────────────────────────────────────────────────
// A synchronous JSON-RPC client for a devnet node's ycash.conf (rpcport, rpcuser, rpcpassword).
struct DevnetNode {
    QNetworkAccessManager nam;
    QNetworkRequest       request;
    bool attach(const QString& dir, int node, QString* why) {
        QFile f(dir + QString("/node%1/ycash.conf").arg(node));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { *why = "cannot read " + f.fileName(); return false; }
        QMap<QString, QString> kv;
        for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
            int eq = line.indexOf('=');
            if (eq > 0) kv[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
        }
        if (!kv.contains("rpcport") || !kv.contains("rpcuser") || !kv.contains("rpcpassword")) {
            *why = "ycash.conf lacks rpcport/rpcuser/rpcpassword"; return false;
        }
        request.setUrl(QUrl("http://127.0.0.1:" + kv["rpcport"] + "/"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
        request.setRawHeader("Authorization", "Basic " + (kv["rpcuser"] + ":" + kv["rpcpassword"]).toUtf8().toBase64());
        return true;
    }
    // result, or null with *error set
    json call(const char* method, const json& params = json::array(), QString* error = nullptr) {
        json payload = {{"jsonrpc", "1.0"}, {"id", "nodecompat_test"}, {"method", method}, {"params", params}};
        QNetworkReply* reply = nam.post(request, QByteArray::fromStdString(payload.dump()));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        json parsed = json::parse(reply->readAll().toStdString(), nullptr, false);
        reply->deleteLater();
        if (parsed.is_discarded()) { if (error) *error = "no JSON in the reply"; return json(nullptr); }
        if (parsed["error"].is_object()) {
            if (error) *error = QString::fromStdString(parsed["error"].value("message", std::string("?")));
            return json(nullptr);
        }
        return parsed["result"];
    }
};

class NodeCompatTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ycash-foundation");
        QCoreApplication::setApplicationName("yecwallet-nodecompat-test");
        Settings::init();
        Settings::getInstance()->setHeadless(true);
    }

    // ── Pure call shapes ──────────────────────────────────────────────────────────────────

    void versionThreshold() {
        QVERIFY(!NodeCompat::isYcash6(0));          // not read: the node answered getrescaninfo
        QVERIFY(!NodeCompat::isYcash6(V450));
        QVERIFY(!NodeCompat::isYcash6(6199999));
        QVERIFY(NodeCompat::isYcash6(V620));
        QVERIFY(NodeCompat::hasRescanRpcs(V450));
        QVERIFY(!NodeCompat::hasRescanRpcs(V620));
        QVERIFY(NodeCompat::canCreateSprout(V450));
        QVERIFY(!NodeCompat::canCreateSprout(V620));
    }

    void versionFromGetinfo() {
        QCOMPARE(NodeCompat::versionFromGetinfo(json{{"version", V620}, {"blocks", 3}}), V620);
        QCOMPARE(NodeCompat::versionFromGetinfo(json{{"version", V450}}), V450);
        QCOMPARE(NodeCompat::versionFromGetinfo(json{{"blocks", 3}}), 0);
        QCOMPARE(NodeCompat::versionFromGetinfo(json{{"version", "6.20.0"}}), 0);
        QCOMPARE(NodeCompat::versionFromGetinfo(json(nullptr)), 0);
    }

    // v4.5.0 (and "unknown"): exactly the JSON the wallet sent before (zcashdrpc.cpp, v4.5.0 tag)
    void legacyShapesUnchanged() {
        for (int v : {0, V450}) {
            json before = { std::string("zivks1key"), "yes", 120, std::string("ys1addr") };
            QCOMPARE(NodeCompat::importIvkParams(v, "zivks1key", true, 120, "ys1addr").dump(), before.dump());
            QCOMPARE(NodeCompat::importIvkParams(v, "zivks1key", false, 0, "ys1addr").dump(),
                     std::string(R"(["zivks1key","no",0,"ys1addr"])"));
            json beforePk = { std::string("KwifKey"), "", true, 120 };
            QCOMPARE(NodeCompat::importPrivKeyParams(v, "KwifKey", true, 120).dump(), beforePk.dump());
            QCOMPARE(NodeCompat::importPrivKeyParams(v, "KwifKey", false, 7).dump(),
                     std::string(R"(["KwifKey","",false,7])"));
        }
    }

    // 6.20.0: z_importivk ivk, zaddr, rescan, startHeight; importprivkey key, label, rescan
    void ycash6Shapes() {
        QCOMPARE(NodeCompat::importIvkParams(V620, "zivks1key", true, 120, "ys1addr").dump(),
                 std::string(R"(["zivks1key","ys1addr","yes",120])"));
        QCOMPARE(NodeCompat::importIvkParams(V620, "zivks1key", false, 0, "ys1addr").dump(),
                 std::string(R"(["zivks1key","ys1addr","no",0])"));
        QCOMPARE(NodeCompat::importPrivKeyParams(V620, "KwifKey", true, 120).dump(),
                 std::string(R"(["KwifKey","",true])"));
        QCOMPARE(NodeCompat::importPrivKeyParams(V620, "KwifKey", false, 0).dump(),
                 std::string(R"(["KwifKey","",false])"));
    }

    void fastSyncConfKey() {
        // written as the key both lines read; read back under either name
        QCOMPARE(QString(NodeCompat::FASTSYNC_CONF_KEY), QString("ibdskiptxverification"));
        QVERIFY(NodeCompat::isFastSyncConfKey("ibdskiptxverification"));
        QVERIFY(NodeCompat::isFastSyncConfKey("fastsync"));
        QVERIFY(!NodeCompat::isFastSyncConfKey("rescan"));
    }

    // ── On the wire ───────────────────────────────────────────────────────────────────────

    // v4.5.0: every call is preceded by getrescaninfo; the import shapes are the old ones
    void wireLegacy() {
        Wire w(V450);
        bool ivkDone = false, pkDone = false;
        w.rpc.importZIVK("zivks1key", true, 120, "ys1addr", [&](json) { ivkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(ivkDone, 5000);
        w.rpc.importTPrivKey("KwifKey", true, 120, [&](json) { pkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(pkDone, 5000);
        QCOMPARE(w.node.methods(), QStringList({"getrescaninfo", "z_importivk", "getrescaninfo", "importprivkey"}));
        QCOMPARE(w.node.lastParams("z_importivk").dump(), std::string(R"(["zivks1key","yes",120,"ys1addr"])"));
        QCOMPARE(w.node.lastParams("importprivkey").dump(), std::string(R"(["KwifKey","",true,120])"));

        // the viewing-key import path (MainWindow::doImport's zivk branch) uses the same shape
        bool vkDone = false;
        w.rpc.importZViewingKey("zivks1key", false, 0, "ys1addr", [&](json) { vkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(vkDone, 5000);
        QCOMPARE(w.node.lastParams("z_importivk").dump(), std::string(R"(["zivks1key","no",0,"ys1addr"])"));

        // the rescan-status poll still asks getrescaninfo
        int before = w.node.calls.size();
        w.rpc.refreshRescanStatus([](json) {});
        QTRY_VERIFY_WITH_TIMEOUT(w.node.calls.size() > before, 5000);
        QCOMPARE(QString::fromStdString(w.node.calls.last().method), QString("getrescaninfo"));
    }

    // 6.20.0: no getrescaninfo, the reordered z_importivk, a 3-argument importprivkey
    void wireYcash6() {
        Wire w(V620);
        bool ivkDone = false, vkDone = false, pkDone = false, zkDone = false;
        w.rpc.importZIVK("zivks1key", true, 120, "ys1addr", [&](json) { ivkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(ivkDone, 5000);
        QCOMPARE(w.node.lastParams("z_importivk").dump(), std::string(R"(["zivks1key","ys1addr","yes",120])"));
        w.rpc.importZViewingKey("zivks1key", false, 0, "ys1addr", [&](json) { vkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(vkDone, 5000);
        QCOMPARE(w.node.lastParams("z_importivk").dump(), std::string(R"(["zivks1key","ys1addr","no",0])"));
        w.rpc.importTPrivKey("KwifKey", true, 120, [&](json) { pkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(pkDone, 5000);
        QCOMPARE(w.node.lastParams("importprivkey").dump(), std::string(R"(["KwifKey","",true])"));
        // z_importkey is the same on both lines
        w.rpc.importZPrivKey("secret-extended-key", true, 120, [&](json) { zkDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(zkDone, 5000);
        QCOMPARE(w.node.lastParams("z_importkey").dump(), std::string(R"(["secret-extended-key","yes",120])"));

        QCOMPARE(w.node.methods(), QStringList({"z_importivk", "z_importivk", "importprivkey", "z_importkey"}));

        // no rescan-status poll: nothing goes out
        int before = w.node.calls.size();
        w.rpc.refreshRescanStatus([](json) { QFAIL("no reply expected"); });
        QTest::qWait(300);
        QCOMPARE(w.node.calls.size(), before);
    }

    // 6.20.0: while an import is rescanning inside the node, the wallet's other calls are held
    // back (they would queue behind the node's wallet lock); afterwards they go out again
    void ycash6HoldsCallsDuringSyncRescan() {
        Wire w(V620);
        w.conn->syncRescanInFlight = true;
        bool got = false;
        w.rpc.fetchInfo([&](json) { got = true; }, [](QNetworkReply*, const json&) {});
        QTest::qWait(300);
        QVERIFY(!got);
        QCOMPARE(w.node.calls.size(), 0);

        w.conn->syncRescanInFlight = false;
        w.rpc.fetchInfo([&](json) { got = true; }, [](QNetworkReply*, const json&) {});
        QTRY_VERIFY_WITH_TIMEOUT(got, 5000);
        QCOMPARE(w.node.methods(), QStringList({"getinfo"}));
    }

    // v4.5.0, unchanged: while getrescaninfo reports a rescan, calls are held back too
    void legacyHoldsCallsWhileNodeRescans() {
        Wire w(V450);
        w.node.results["getrescaninfo"] = json{{"rescanning", true}, {"rescanprogress", "0.42"}};
        bool got = false;
        w.rpc.fetchInfo([&](json) { got = true; }, [](QNetworkReply*, const json&) {});
        QTRY_VERIFY_WITH_TIMEOUT(w.node.calls.size() == 1, 5000);
        QTest::qWait(300);
        QVERIFY(!got);
        QCOMPARE(w.node.methods(), QStringList({"getrescaninfo"}));
    }

    // The import-rescan's own error path (6.20.0 uses it so the busy state always ends)
    void importErrorReachesCaller() {
        for (int v : {V450, V620}) {
            Wire w(v);
            w.node.errors["importprivkey"] = "Invalid private key encoding";
            QString error;
            bool ok = false;
            w.rpc.importTPrivKey("bad", true, 0, [&](json) { ok = true; }, [&](QString e) { error = e; });
            QTRY_VERIFY_WITH_TIMEOUT(!error.isEmpty(), 5000);
            QVERIFY(!ok);
            QCOMPARE(error, QString("Invalid private key encoding"));
        }
    }

    // Against a running devnet (either line): YELLOWBACK_DEVNET_DIR is the devnet's --dir. A WIF
    // and a Sapling ivk made on node 1 are imported into node 0 through the wallet's own
    // Connection/ZcashdRPC, with the version read the way ConnectionLoader::refreshZcashdState
    // reads it (getrescaninfo answers: v4.5.0 line, version left 0; else getinfo.version).
    void devnetImports() {
        QString dir = qEnvironmentVariable("YELLOWBACK_DEVNET_DIR");
        if (dir.isEmpty())
            QSKIP("YELLOWBACK_DEVNET_DIR is unset: the devnet case needs a running node");
        DevnetNode n0, n1;
        QString why;
        QVERIFY2(n0.attach(dir, 0, &why), qPrintable(why));
        QVERIFY2(n1.attach(dir, 1, &why), qPrintable(why));

        QString e;
        int version = 0;
        n0.call("getrescaninfo", json::array(), &e);
        if (!e.isEmpty()) version = NodeCompat::versionFromGetinfo(n0.call("getinfo"));
        int reported = NodeCompat::versionFromGetinfo(n0.call("getinfo"));
        qInfo("node 0 getinfo.version %d; getrescaninfo %s; wallet uses the %s shapes", reported,
              e.isEmpty() ? "present" : qPrintable("absent (" + e + ")"),
              NodeCompat::isYcash6(version) ? "6.20.0" : "v4.5.0");
        QCOMPARE(NodeCompat::isYcash6(version), NodeCompat::isYcash6(reported));
        if (NodeCompat::isYcash6(version)) {
            // the RPCs the 6.20.0 branch stops calling are really gone
            QString e2;
            n0.call("rescanblockchain", json::array({0}), &e2);
            QCOMPARE(e2, QString("Method not found"));
        }

        // Keys made on node 1
        json taddr = n1.call("getnewaddress");
        QVERIFY(taddr.is_string());
        json wif = n1.call("dumpprivkey", json::array({taddr}));
        QVERIFY(wif.is_string());
        json zaddr = n1.call("z_getnewaddress", json::array({"sapling"}));
        QVERIFY(zaddr.is_string());
        json ivk = n1.call("z_exportivk", json::array({zaddr}));
        QVERIFY(ivk.is_string());

        // The wallet's Connection to node 0
        auto nam = new QNetworkAccessManager();
        auto req = new QNetworkRequest(n0.request);
        auto conn = new Connection(nullptr, nam, req, std::make_shared<ConnectionConfig>());
        conn->nodeVersion = version;
        ZcashdRPC rpc;
        rpc.setConnection(conn);

        // importprivkey: the last key of a batch rescans (from height 1 on v4.5.0; from genesis on 6.20.0)
        json got; QString err; bool done = false;
        rpc.importTPrivKey(QString::fromStdString(wif.get<std::string>()), true, 1,
            [&](json r) { got = r; done = true; }, [&](QString x) { err = x; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 120000);
        QVERIFY2(err.isEmpty(), qPrintable("importprivkey: " + err));
        QVERIFY(got.is_string());
        QCOMPARE(got.get<std::string>(), taddr.get<std::string>());
        json va = n0.call("validateaddress", json::array({taddr}));
        QVERIFY(va.value("ismine", false));

        // z_importivk: the zivk line of the import dialog
        got = json(); err.clear(); done = false;
        rpc.importZViewingKey(QString::fromStdString(ivk.get<std::string>()), true, 0,
            QString::fromStdString(zaddr.get<std::string>()),
            [&](json r) { got = r; done = true; }, [&](QString x) { err = x; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 120000);
        QVERIFY2(err.isEmpty(), qPrintable("z_importivk: " + err));
        // 6.20.0 answers {"address": zaddr}; v4.5.0 answers null (the wallet reads neither)
        if (NodeCompat::isYcash6(version)) {
            QVERIFY(got.is_object());
            QCOMPARE(got.value("address", std::string()), zaddr.get<std::string>());
        }
        json zs = n0.call("z_listaddresses", json::array({true}));
        QVERIFY(zs.is_array());
        bool listed = false;
        for (auto& a : zs) if (a == zaddr) listed = true;
        QVERIFY2(listed, "the imported zaddr is not listed (watch-only) on node 0");

        // and the same import without a rescan, as for the earlier keys of a batch
        json zaddr2 = n1.call("z_getnewaddress", json::array({"sapling"}));
        json ivk2 = n1.call("z_exportivk", json::array({zaddr2}));
        got = json(); err.clear(); done = false;
        rpc.importZViewingKey(QString::fromStdString(ivk2.get<std::string>()), false, 0,
            QString::fromStdString(zaddr2.get<std::string>()),
            [&](json r) { got = r; done = true; }, [&](QString x) { err = x; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        QVERIFY2(err.isEmpty(), qPrintable("z_importivk (no rescan): " + err));
    }
};

QTEST_MAIN(NodeCompatTest)
#include "nodecompat_test.moc"
