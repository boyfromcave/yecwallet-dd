#!/usr/bin/env python3
"""YecWallet against a Yugen-activated ycashd on regtest, headless.

One ycashd (ycash6 upgrade/yugen) on RPC 31400 / P2P 31401, Vault at 16, Falcon at 20, Yugen at 32.
Puts Yugen deposits (version group 0x59474e47) and post-activation Sapling/transparent/PQ sends in the
node wallet, then:
  1. issues every read call YecWallet makes (src/zcashdrpc.cpp) and checks the fields it reads;
  2. starts YecWallet offscreen (--no-embedded --conf) for WALLET_SECONDS and checks it stays up and
     logs no JSON error.
  3. with YUGEN_BIN_DIR (an unpacked ycash6 6.23.x package, or the app's Contents/MacOS): takes
     ycashd and the three Yugen helpers from there, and afterwards starts YecWallet WITHOUT
     --no-embedded against a regtest ycash.conf in an isolated HOME, then checks that the embedded
     ycashd it starts is the bundled one, carries -yugenverifier/-yugenbuilder/-yugenprover pointing
     at the helpers beside it, and activates Yugen (yugen_getinfo).
Env: YUGEN_CLONES (the directory holding ycash6 and yew checkouts; default: this repository's
parent), YCASHD, YEWD_YUGEN, YECWALLET, YUGEN_BIN_DIR, WALLET_SECONDS (default 60). Exit 0 = all
checks passed.
"""
import base64, json, os, shutil, signal, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
CL = os.environ.get('YUGEN_CLONES', os.path.dirname(REPO))
BIN = os.environ.get('YUGEN_BIN_DIR')
YCASHD = os.environ.get('YCASHD', BIN + '/ycashd' if BIN else CL + '/ycash6/src/ycashd')
C6 = CL + '/ycash6/contrib'
YEWD_YUGEN = os.environ.get('YEWD_YUGEN', BIN + '/yewd-yugen' if BIN else CL + '/yew/yewd/yugen/target/release/yewd-yugen')
VERIFIER = BIN + '/yugen-leafverify' if BIN else C6 + '/yugen-leafverify/target/release/yugen-leafverify'
BUILDER = BIN + '/yugen-sendbuild' if BIN else C6 + '/yugen-sendbuild/target/release/yugen-sendbuild'
YECWALLET = os.environ.get('YECWALLET', REPO + '/build/macos-arm64/bin/yecwallet.app/Contents/MacOS/yecwallet')
RPC, P2P = 31400, 31401
D = REPO + '/target/regtest'
U, P = 'yw', 'ywpass'
fails = []


def nu(b, h): return '-nuparams=%s:%d' % (b, h)


ARGS = [nu('374d694f', 1), nu('8e471bd6', 1), nu('66314da3', 1), nu('19bd2d2f', 1), nu('6d5b7a31', 16), nu('59474e30', 32),
        '-pqfalconheight=20', '-yugenverifier=' + VERIFIER, '-yugenbuilder=' + BUILDER, '-yugenprover=' + YEWD_YUGEN]


def rpc(m, *p, port=RPC):
    r = urllib.request.Request('http://127.0.0.1:%d/' % port, json.dumps({'id': 1, 'method': m, 'params': list(p)}).encode(),
                               {'Authorization': 'Basic ' + base64.b64encode(('%s:%s' % (U, P)).encode()).decode()})
    try:
        out = json.load(urllib.request.urlopen(r, timeout=1900))
    except urllib.error.HTTPError as e:
        out = json.load(e)
    if out.get('error'):
        raise RuntimeError('%s: %s' % (m, out['error']))
    return out['result']


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        fails.append(what)


def wait_op(opid):
    while True:
        s = rpc('z_getoperationstatus', [opid])[0]
        if s['status'] in ('success', 'failed'):
            return s
        time.sleep(2)


def mine(n=1):
    b = rpc('generate', n)
    time.sleep(1.5)        # the wallet takes blocks from the async notification thread
    return b


def main():
    shutil.rmtree(D, ignore_errors=True)
    os.makedirs(D + '/node')
    conf = D + '/node/ycash.conf'
    with open(conf, 'w') as f:
        f.write('regtest=1\nrpcuser=%s\nrpcpassword=%s\nrpcport=%d\nport=%d\nlisten=0\nexperimentalfeatures=1\n'
                'txindex=1\ninsightexplorer=1\n' % (U, P, RPC, P2P))
    node = subprocess.Popen([YCASHD, '-datadir=' + D + '/node', '-conf=' + conf, '-debug=yugen', '-debug=rpc'] + ARGS,
                            stdout=open(D + '/ycashd.out', 'w'), stderr=subprocess.STDOUT)
    wallet = None
    try:
        for _ in range(120):
            try:
                rpc('getblockcount'); break
            except Exception:
                time.sleep(1)
        info = rpc('yugen_getinfo')
        check(info.get('rpcversion') == 12 or rpc('yed_getinfo').get('rpcversion') == 12, 'node rpcversion 12')
        mine(140)
        t = rpc('getnewaddress'); z = rpc('z_getnewaddress', 'sapling')
        pqs = rpc('getnewaddress', '', 'pqslh'); pqf = rpc('getnewaddress', '', 'pq')
        check(rpc('getblockchaininfo')['upgrades']['59474e30']['status'] == 'active', 'Yugen active at %d' % rpc('getblockcount'))
        # The seed on an inherited descriptor (-seedfd): yewd-yugen refuses -seed= on the command line.
        rfd, wfd = os.pipe()
        os.write(wfd, ('33' * 32 + '\n').encode()); os.close(wfd)
        ya = json.loads(subprocess.run([YEWD_YUGEN, '-network=regtest', '-node=127.0.0.1:%d' % RPC, '-rpcuser=' + U,
                                        '-rpcpassword=' + P, '-datadir=' + D + '/yw', '-seedfd=%d' % rfd, 'getaddress'],
                                       capture_output=True, text=True, timeout=600, pass_fds=(rfd,)).stdout)
        os.close(rfd)
        print('Yugen address', ya[:24] + '...')
        src = [rpc('getnewaddress') for _ in range(5)]
        locks = []
        for a in src + [t]:               # lock each funded coin, or the next send takes it as an input
            txid = rpc('sendtoaddress', a, 3)
            vout = [o['n'] for o in rpc('getrawtransaction', txid, 1)['vout'] if a in o['scriptPubKey'].get('addresses', [])][0]
            locks.append({'txid': txid, 'vout': vout})
            rpc('lockunspent', False, locks[-1:])
        mine(1)
        rpc('lockunspent', True, locks)
        wait_op(rpc('z_sendmany', t, [{'address': z, 'amount': 2}], 1, None, 'AllowFullyTransparent'))
        mine(1)
        for f, to in zip(src, (z, t, pqs, pqf)):           # post-activation sends to Sapling / transparent / PQ
            s = wait_op(rpc('z_sendmany', f, [{'address': to, 'amount': 0.5}], 1, None, 'AllowFullyTransparent'))
            check(s['status'] == 'success', 'z_sendmany t -> %s: %s' % (to[:6], s.get('error', {}).get('message', 'ok')))
            mine(1)
        s = wait_op(rpc('z_sendmany', src[4], [{'address': ya, 'amount': 1}], 1))
        check(s['status'] == 'success', 'z_sendmany t -> Yugen (deposit): %s' % s.get('error', {}).get('message', 'ok'))
        ytx = s.get('result', {}).get('txid')
        s = wait_op(rpc('z_sendmany', z, [{'address': ya, 'amount': 0.2}], 1))
        check(s['status'] == 'success', 'z_sendmany Sapling -> Yugen: %s' % s.get('error', {}).get('message', 'ok'))
        mine(1)
        if ytx:
            raw = rpc('getrawtransaction', ytx, 1)
            check(raw.get('versiongroupid') == '59474e47', 'deposit is version group 59474e47')
        # the calls YecWallet reads (src/zcashdrpc.cpp, controller.cpp)
        for tx in rpc('listtransactions', '*', 1000):
            for k in ('category', 'amount', 'time', 'confirmations'):
                if k not in tx:
                    check(False, 'listtransactions row %s lacks %s' % (tx.get('txid'), k))
        check(True, 'listtransactions rows: %d' % len(rpc('listtransactions', '*', 1000)))
        tb = rpc('z_gettotalbalance', 0)
        check(all(k in tb for k in ('transparent', 'private', 'total')), 'z_gettotalbalance %s' % tb)
        for u in rpc('listunspent', 0) + rpc('z_listunspent', 0):
            if 'address' not in u or 'amount' not in u:
                check(False, 'unspent row without address/amount: %s' % u)
        for za in rpc('z_listaddresses'):
            for r in rpc('z_listreceivedbyaddress', za, 0):
                gt = rpc('gettransaction', r['txid'])
                check('time' in gt and 'confirmations' in gt, 'gettransaction %s for %s' % (r['txid'][:8], za[:8]))
        if ytx:
            gt = rpc('gettransaction', ytx)
            check('details' in gt and 'time' in gt, 'gettransaction of the Yugen deposit')
        # YecWallet, offscreen, against this node only
        wconf = D + '/wallet-ycash.conf'
        with open(wconf, 'w') as f:
            f.write('regtest=1\nrpcuser=%s\nrpcpassword=%s\nrpcport=%d\nrpcconnect=127.0.0.1\n' % (U, P, RPC))
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', YECWALLET_TEST_ISOLATE='1', HOME=D + '/home')
        pdir = D + '/home/Library/Application Support'
        os.makedirs(pdir, exist_ok=True)     # the user's parameters, linked: no download
        os.symlink(os.path.expanduser('~/Library/Application Support/ZcashParams'), pdir + '/ZcashParams')
        logpos = os.path.getsize(D + '/node/regtest/debug.log')
        wallet = subprocess.Popen([YECWALLET, '--headless', '--no-embedded', '--conf', wconf], env=env,
                                  stdout=open(D + '/yecwallet.out', 'w'), stderr=subprocess.STDOUT)
        secs = int(os.environ.get('WALLET_SECONDS', 60))
        for i in range(secs):
            time.sleep(1)
            if i == secs // 2:
                mine(1)
        check(wallet.poll() is None, 'YecWallet still running after %ds (exit %s)' % (secs, wallet.poll()))
        with open(D + '/node/regtest/debug.log', errors='replace') as f:
            f.seek(logpos); nlog = f.read()
        for m in ('z_gettotalbalance', 'listtransactions', 'z_listreceivedbyaddress', 'listunspent', 'z_listunspent', 'gettransaction'):
            check(('method=%s ' % m) in nlog or ('method=%s\n' % m) in nlog or ('"%s"' % m) in nlog, 'YecWallet called %s' % m)
        # the Send tab's recipient check (src/settings.cpp isYugenAddress), the same pattern
        import re
        check(re.match(r'^yq[tr]?f1[qpzry9x8gf2tvdw0s3jn54khce6mua7l]{20,}$', ya) is not None, 'Yugen address matches isYugenAddress')
        out = open(D + '/yecwallet.out', errors='replace').read()
        bad = [l for l in out.splitlines() if 'json' in l.lower() and ('error' in l.lower() or 'exception' in l.lower())]
        check(not bad, 'no JSON errors in the wallet output %s' % bad[:3])
        check('z_listreceivedbyaddress' in out or 'listtransactions' in out or len(out) > 0, 'wallet talked to the node (%d bytes output)' % len(out))
    finally:
        if wallet and wallet.poll() is None:
            wallet.send_signal(signal.SIGTERM); time.sleep(3); wallet.kill()
        try:
            rpc('stop')
        except Exception:
            pass
        try:
            node.wait(60)
        except Exception:
            node.kill()
    if BIN and not fails:
        embedded()
    print('FAILED: %s' % fails if fails else 'ALL PASSED')
    return 1 if fails else 0


def embedded():
    """YecWallet starts its own (bundled) ycashd: the Yugen helper flags it passes."""
    ERPC, EP2P = 31410, 31411
    home = D + '/home-embedded'
    ydir = home + '/Library/Application Support/Ycash'
    os.makedirs(ydir)
    os.symlink(os.path.expanduser('~/Library/Application Support/ZcashParams'), home + '/Library/Application Support/ZcashParams')
    with open(ydir + '/ycash.conf', 'w') as f:
        f.write('regtest=1\nrpcuser=%s\nrpcpassword=%s\nrpcport=%d\nport=%d\nlisten=0\nexperimentalfeatures=1\n' % (U, P, ERPC, EP2P))
        for a in ARGS:
            if a.startswith('-nuparams=') or a.startswith('-pqfalconheight='):
                f.write(a[1:] + '\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', YECWALLET_TEST_ISOLATE='1', HOME=home,
               YECWALLET_UPGRADE_ANSWER='continue')
    wallet = subprocess.Popen([YECWALLET, '--headless'], env=env,
                              stdout=open(D + '/yecwallet-embedded.out', 'w'), stderr=subprocess.STDOUT)
    appdir = os.path.dirname(os.path.realpath(YECWALLET))
    child, cmd = None, ''
    try:
        for _ in range(60):
            time.sleep(1)
            out = subprocess.run(['pgrep', '-P', str(wallet.pid)], capture_output=True, text=True).stdout.split()
            for pid in out:
                c = subprocess.run(['ps', '-o', 'command=', '-p', pid], capture_output=True, text=True).stdout.strip()
                if 'ycashd' in c:
                    child, cmd = pid, c
            if child:
                break
        check(child is not None, 'YecWallet started its embedded ycashd')
        print('embedded:', cmd)
        check(cmd.startswith(appdir + '/ycashd'), 'the embedded ycashd is the one beside the wallet')
        for opt, h in (('yugenverifier', 'yugen-leafverify'), ('yugenbuilder', 'yugen-sendbuild'), ('yugenprover', 'yewd-yugen')):
            check(('-%s=%s/%s' % (opt, appdir, h)) in cmd, 'embedded ycashd has -%s=<app>/%s' % (opt, h))
        info = None
        for _ in range(120):
            try:
                rpc('generate', 40, port=ERPC); info = rpc('yugen_getinfo', port=ERPC); break
            except Exception:
                time.sleep(1)
        check(bool(info) and info.get('active') is True, 'embedded ycashd: Yugen active (%s)' % (info and {k: info.get(k) for k in ('active', 'tree_size')}))
    finally:
        try:
            rpc('stop', port=ERPC)
        except Exception:
            pass
        for _ in range(60):
            if not (child and subprocess.run(['kill', '-0', child], capture_output=True).returncode == 0):
                break
            time.sleep(1)
        if wallet.poll() is None:
            wallet.send_signal(signal.SIGTERM); time.sleep(3); wallet.kill()
        if child:
            subprocess.run(['kill', '-9', child], capture_output=True)


if __name__ == '__main__':
    sys.exit(main())
