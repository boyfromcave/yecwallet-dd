# YecWallet against a Yugen-activated ycashd

Branch `upgrade/yugen`. Node: ycash6 `upgrade/yugen`, ycashd 6.23.x (rpcversion 12).

## Built

- Yellowback handshake accepts rpcversion 9 to 12 (`RPC_VERSION` 12, contract copy 12; 12 changes no `yed_*` shape).
- Send: `Settings::isYugenAddress` (`yqf1…`/`yqtf1…`/`yqrf1…`, Bech32m, any length) is a valid recipient, checked
  before the Sapling length patterns; post-quantum `sqf/sqs`, `tqf/tqs`, `rqf/rqs` recipients are valid too. A Yugen
  recipient keeps the memo field (ycashd refuses a memo to a payment request itself). Payment goes through the
  existing `z_sendmany` path; ycashd builds the deposit (doc/yugen-sendmany.md in ycash6).
- A `z_sendmany` operation in ycashd's `pending` state (ruling 16) stays watched; its reason shows in the
  progress tooltip.
- macOS static build links the offscreen platform plugin, so scripted runs need no window.

## Verified

`contrib/yugen-regtest/run.py`: one ycashd on RPC 31400 (Vault 16, Falcon 20, Yugen 32); post-activation sends
t to Sapling/t/SLH/FN, t to Yugen and Sapling to Yugen (deposits, version group `59474e47`); every read call
YecWallet makes returns the fields it reads; YecWallet runs headless (`--headless --no-embedded`, offscreen,
isolated settings) for 90 s, completes its refresh cycle (z_gettotalbalance, listtransactions,
z_listreceivedbyaddress, listunspent, z_listunspent, gettransaction) and stays up.

Bundled helpers: the app carries `yugen-leafverify`, `yugen-sendbuild` and `yewd-yugen` beside ycashd (from the
verified ycash6 package) and starts the embedded ycashd with `-yugenverifier`, `-yugenbuilder`, `-yugenprover`
pointing at them (`yugenHelperArgs`, src/connection.cpp; a flag set in ycash.conf is left alone). With
`YUGEN_BIN_DIR=<app>/Contents/MacOS`, `run.py` runs the node and helpers from the app bundle, then starts YecWallet
without `--no-embedded` on a regtest conf and checks the embedded ycashd's flags and Yugen activation.

Not verified: the Yellowback tab at rpcversion 12 with an attestor set configured (the run has none); the Send
tab's GUI flow end to end (Qt Test is not in the static Qt build, so no unit test was added).

## Open: UX parity items (lead architect: Yugen as easy as Sapling)

ycashd holds no Yugen account (doc/yugen-sendmany.md N-S2), so receiving and balances need node RPCs first.

| Item | Acceptance criterion |
|---|---|
| U-1 Yugen receive address | Receive tab has a "Yugen" type; it shows a `yq…f1…` address issued by a node RPC (none exists in ycashd yet; yewd has `yugen_newaddress`). Needs: a ycashd Yugen account and RPC, or YecWallet driving yewd. |
| U-2 Yugen balance | Balances tab shows a Yugen line (applied notes) and a pending line (mempool or below anchor depth); total includes it. Needs the same account plus a balance RPC. |
| U-3 Yugen fee in Send | Confirm dialog shows the transfer fee (255,600 zat, plus the Sapling hop fee from a Sapling sender) beside the network fee; total spent matches the node's result. |
| U-4 Yugen rows in Transactions | A deposit row reads "Yugen deposit" with the amount sent to the pool (vgid `59474e47` from getrawtransaction), not a plain transparent send. |
| U-5 Address-book and URI | `ycash:` URIs and the address book accept `yq…` addresses (the URI parser's `[a-zA-Z0-9]+` already does; check after U-1). |
