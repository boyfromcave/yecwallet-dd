// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

#ifndef YELLOWBACKRPC_H
#define YELLOWBACKRPC_H

#include <QtGlobal>

// The Yellowback RPC contract (rpcversion 8, the post-quantum line's fee structure), as the wallet depends on it.
//
// Every yed_* method name, every result field and every error identifier the wallet reads is
// declared here and nowhere else, so the wallet can be reconciled against
// ycash-dd/doc/yellowback-rpc.md in one place. Nothing else crosses the node/wallet boundary.
//
// tests/check-rpc-contract.py (the `wallet` CI job, plan P7) parses this file: every namespace
// carries a `// contract: <command>[.<path>][[]]` marker naming the JSON object its constants
// are keys of in docs/yellowback-rpc-contract.json (`[]` = the example row of a list result);
// a constant marked `// value` is a field *value* (a status or type name), not a key; the
// method constants must be top-level commands of that file; the Errors namespace's identifiers
// must be listed under `errors`; and RPC_VERSION must equal the file's `rpcversion`. A constant
// marked `// optional` is a field the contract marks optional and its example omits, and a
// namespace marked `// contract: optional <command>.<path>` holds the keys of such an object.
//
// The vault primitive's own RPCs (set_*, vault_*) are in vaultrpc.h: they are the primitive's
// contract (ycash-dd/doc/vault-rpc.md), not Yellowback's.

namespace YellowbackRpc {

// The rpcversion this build of the wallet understands. yed_getinfo.rpcversion must equal it
// (bumped to 2 in Phase 7b-a's first commit, N27; to 3 in v3 Phase A5's first commit; to 4 with
// the hardening H-9.3 client bounds, yed_claim's maxBurnCents, H5-a; to 5 with the vault upgrade,
// upgrade plan §15.10: the enforcement machinery, the sweep and VOID are gone, a claim is an intent;
// to 6 with in-term claims, docs/plans/yellowback-in-term-claims-plan.md §4.1 in the workspace: a
// vault is claimable in term once under the claim threshold, the owner redeems at any height and pays
// an early-redeem fee before lockHeight, yed_listclaimable lists in-term rows with `claimable`; to 7
// with post-quantum vault owners, docs/plans/yellowback-quantum-spec.md §6.2 in the workspace: every
// vault owner is an SLH-DSA key, ownerScheme/ownerHash replace ownerPubKey/ownerKeyId, ownerAddress is
// the 53-character PQ address, yed_getinfo.params.pq, a MINT payload is version 4; to 8 with the
// fee structure, docs/plans/yellowback-quantum-fees-spec.md §3.4 in the workspace: the protocol fee is
// dollars of the debt in marginal bands at the vault's stored mint price pMint, the early-redeem fee is
// of the debt, network fees are dollars per KB at the attested price, params.feeMinZat/feeBps and
// yed_getfeepayee's collateralZat/feeZat are gone, yed_estimatefee takes <mintedCents> [priceMicroUsd]).
constexpr int RPC_VERSION = 8;

// ── Methods (node context) ────────────────────────────────────────────────────────────────
constexpr const char* GETINFO             = "yed_getinfo";
constexpr const char* GETSTATS            = "yed_getstats";
constexpr const char* GETACTIVATION       = "yed_getactivation";   // rpcversion 5: the vault upgrade's status
constexpr const char* LISTVAULTS          = "yed_listvaults";      // rpcversion 5: every CLAIMING vault (the Pending claims page)
constexpr const char* GETVAULT            = "yed_getvault";
constexpr const char* LISTCLAIMABLE       = "yed_listclaimable";
constexpr const char* GETTXINFO           = "yed_gettxinfo";
constexpr const char* ESTIMATECOLLATERAL  = "yed_estimatecollateral";
constexpr const char* GETFEEPAYEE         = "yed_getfeepayee";     // rpcversion 8: <refHeight> [selectorHex], no fee
constexpr const char* ESTIMATEFEE         = "yed_estimatefee";     // rpcversion 8: the protocol fee of a debt, in dollars and YEC
constexpr const char* GETPRICE            = "yed_getprice";          // v3: xMint/xClaim, armed, attestStatus
constexpr const char* LISTATTESTORS       = "yed_listattestors";     // v3: the Attestors view
constexpr const char* GETSELECTION        = "yed_getselection";      // v3: "n of m selected attestors reachable"
constexpr const char* GETNOTICE           = "yed_getnotice";         // v3: the standing notice of a vault (follows a pending notice)

// ── Methods (wallet context) ──────────────────────────────────────────────────────────────
constexpr const char* GETNEWADDRESS       = "yed_getnewaddress";
constexpr const char* VALIDATEADDRESS     = "yed_validateaddress";
constexpr const char* GETBALANCE          = "yed_getbalance";
constexpr const char* MINT                = "yed_mint";
constexpr const char* SEND                = "yed_send";
constexpr const char* REDEEM              = "yed_redeem";
constexpr const char* CLAIM               = "yed_claim";
constexpr const char* LISTPOSITIONS       = "yed_listpositions";
constexpr const char* LISTTRANSACTIONS    = "yed_listtransactions";
constexpr const char* ESTIMATEREDEEM      = "yed_estimateredeem";    // rpcversion 6 (IT-9): the redeem quote the dialog shows first
// v3 (plan §4.8, A5-b): the two-step notice, the carrier sweep and the attestor actions
constexpr const char* CLAIMNOTICE         = "yed_claimnotice";        // step 1 of the emergency claim (NOT-1)
constexpr const char* SWEEPCARRIERS       = "yed_sweepcarriers";      // reclaim lapsed carriers (W7)
constexpr const char* REGISTERATTESTOR    = "yed_registerattestor";
constexpr const char* WITHDRAWBOND        = "yed_withdrawbond";
constexpr const char* REPORTEQUIVOCATION  = "yed_reportequivocation";
// CAN-1 + CAN-2 (the cancel-limit design): the attestor cancel of a YED claim is built here, not with the
// primitive's vault_buildcancel (which refuses a YED intent); it refunds the burn and carries co-signatures
constexpr const char* BUILDCANCEL         = "yed_buildcancel";        // <intentOutpoint> [cosigs]
constexpr const char* SIGNCANCELCOSIGN    = "yed_signcancelcosign";   // <intentOutpoint> [priceMicroUsd] [seq]

// ── yed_getinfo result ─────────────────────────────────────────────────────────────────────
namespace Info {   // contract: yed_getinfo
    constexpr const char* RPCVERSION        = "rpcversion";
    constexpr const char* ENABLED           = "enabled";
    constexpr const char* NETWORK           = "network";        // "main" | "test" | "regtest"
    constexpr const char* HEIGHT            = "height";         // index tip; -1 while empty
    constexpr const char* CHAIN_HEIGHT      = "chainHeight";    // v2 has no `synced`: synced == (height == chainHeight)
    constexpr const char* START_HEIGHT      = "startHeight";
    constexpr const char* HEALTHY           = "healthy";
    constexpr const char* UNHEALTHY_REASON  = "unhealthyReason";
    constexpr const char* LOCKED_OUTPUTS    = "lockedOutputs";      // H10: YED outpoints the index holds locked
    constexpr const char* PROTECTED_BY_INDEX = "protectedByIndex";  // H10: false means ordinary sends could burn YED
    constexpr const char* UPGRADE           = "upgrade";        // rpcversion 5: the yed_getactivation object
    constexpr const char* MINER             = "miner";
    constexpr const char* PARAMS            = "params";
    constexpr const char* ATTEST            = "attest";         // v3: the arming state at the index tip
    constexpr const char* SUPPLY_CAP_REACHED = "supplyCapReached"; // v3 W20: the next mint of any class would exceed the cap; absent before W20
    constexpr const char* MINT_REQUIRES_ARMED = "mintRequiresArmed"; // hardening H-1: a mint whose reference height is not ARMED is VOID (mint-halted-unarmed)
}

// v3: yed_getinfo.attest, the arming state (ARM-1/2) the Attestors view's banner shows.
namespace Attest {   // contract: yed_getinfo.attest
    constexpr const char* STATUS            = "status";         // UNARMED | TRIGGERED | ARMED
    constexpr const char* TRIGGER_HEIGHT    = "triggerHeight";  // 0 while UNARMED
    constexpr const char* ARM_HEIGHT        = "armHeight";      // triggerHeight + ATTEST_ARM_DELAY once TRIGGERED
    constexpr const char* SEATED_COUNT      = "seatedCount";
    constexpr const char* POOL_SIZE         = "poolSize";       // attestations in this node's pool
    constexpr const char* POOL_FRESH        = "poolFresh";      // seated seqs with a fresh attestation in the pool
    constexpr const char* CARRIER_MODE      = "carrierMode";
    constexpr const char* REQUIRED          = "required";       // ATTEST_REQUIRED; false: the layer is disabled by parameter set
    constexpr const char* ARMED             = "armed";          // status == ARMED && required

    constexpr const char* STATUS_UNARMED    = "UNARMED";        // value
    constexpr const char* STATUS_TRIGGERED  = "TRIGGERED";      // value
    constexpr const char* STATUS_ARMED      = "ARMED";          // value
}

// rpcversion 5: the vault upgrade (UPGRADE_VAULT) the Yellowback rules are consensus under; the
// same object as yed_getactivation. Signalling, lock-in, the valve and the sunset are retired.
namespace Upgrade {   // contract: yed_getinfo.upgrade
    constexpr const char* NAME              = "name";           // "Vault"
    constexpr const char* BRANCH_ID         = "branchId";       // "6d5b7a31"
    constexpr const char* STATUS            = "status";         // active | pending
    constexpr const char* ACTIVATION_HEIGHT = "activationHeight";
    constexpr const char* ATTESTOR_SET_ID   = "attestorSetId";  // the vault primitive set whose members cancel claims
    constexpr const char* CLAIM_DELAY       = "claimDelay";     // CLAIM_DELAY: blocks a claim intent waits before release
    constexpr const char* HEIGHT            = "height";

    constexpr const char* STATUS_ACTIVE     = "active";         // value
    constexpr const char* STATUS_PENDING    = "pending";        // value
}

// The connected node's own mining state (only meaningful when it is a pool node).
namespace Miner {   // contract: yed_getinfo.miner
    constexpr const char* PAYOUT_ADDRESS    = "payoutAddress";  // null when the node has no payout key
    constexpr const char* QUOTE_KIND        = "quoteKind";      // quote | none (the signal-only tag left with ACT-1)
    constexpr const char* QUOTE_AGE_SECONDS = "quoteAgeSeconds"; // null when no quote is held
    constexpr const char* REGISTERED        = "registered";
    constexpr const char* ELIGIBLE          = "eligible";

    constexpr const char* KIND_QUOTE        = "quote";          // value
    constexpr const char* KIND_NONE         = "none";           // value
}

// yed_getinfo.params: the network's protocol parameters (§3.1). Displayed, never configurable.
namespace Params {   // contract: yed_getinfo.params
    constexpr const char* START_HEIGHT        = "startHeight";
    constexpr const char* ATTESTOR_SET_ID     = "attestorSetId";   // rpcversion 5 (U-22)
    constexpr const char* CLAIM_DELAY         = "claimDelay";      // rpcversion 5 (U-23)
    constexpr const char* SIGMA_REF_BPS       = "sigmaRefBps";
    constexpr const char* SUPPLY_CAP_BPS      = "supplyCapBps";
    constexpr const char* REF_LAG             = "refLag";
    constexpr const char* REF_WINDOW          = "refWindow";
    constexpr const char* GRACE               = "grace";
    constexpr const char* PAYEE_WINDOW        = "payeeWindow";
    constexpr const char* TOKEN_VALUE_ZAT     = "tokenValueZat";
    constexpr const char* FEE_ZAT             = "feeZat";       // the network fee, not the pool fee
    constexpr const char* WINDOWS             = "windows";
    constexpr const char* MIN_FILL            = "minFill";
    constexpr const char* CLASSES             = "classes";
    constexpr const char* GLOBAL_RATIO_HALT_BPS = "globalRatioHaltBps";  // HALT-2's floor
    constexpr const char* RECAP_RATIO_BPS     = "recapRatioBps";         // v3 W16: the class minimum that mints through a halt
    constexpr const char* POLICY              = "policy";
    constexpr const char* ATTEST              = "attest";       // v3: the attestation-layer parameters
    // rpcversion 6 (in-term claims, IT-7 / IT-9)
    constexpr const char* IN_TERM_CLAIMS      = "inTermClaims";      // true: a vault is claimable in term once under the threshold
    constexpr const char* CLAIM_THRESHOLD_BPS = "claimThresholdBps"; // θ: RED-4 (12500 = 125 %)
    constexpr const char* EARLY_REDEEM_FEE_BPS = "earlyRedeemFeeBps"; // [A, B, C]: of the debt at pMint (IT-9'), a redeem before lockHeight
    constexpr const char* SIGMA_MULT_MAX_BPS  = "sigmaMultMaxBps";   // 10000: the volatility multiplier is pinned at 1
    // rpcversion 7 (post-quantum owners, quantum spec §6.2)
    constexpr const char* PQ                  = "pq";                // the ParamsPq object
    constexpr const char* ADDRESS_VERSION     = "addressVersion";    // hex Base58Check version of a 20-byte YED address (mainnet 1fdd "yb")
    constexpr const char* PQ_ADDRESS_VERSION  = "pqAddressVersion";  // hex version of a 33-byte PQ YED address (mainnet 56ab "yb")
    // rpcversion 8 (the fee structure, D-Q-23..D-Q-25)
    constexpr const char* MAX_MINT_CENTS      = "maxMintCents";      // D-Q-24: $100,000
    constexpr const char* FEES                = "fees";              // the ParamsFees object
    constexpr const char* SUPPLY_CAP          = "supplyCap";         // the ParamsSupplyCap object (D-Q-25)
}

// rpcversion 8: yed_getinfo.params.fees, the protocol fee (FEE-1'), the early fee (IT-9') and the network fee policy.
namespace ParamsFees {   // contract: yed_getinfo.params.fees
    constexpr const char* PROTOCOL            = "protocol";
    constexpr const char* EARLY_REDEEM_FEE_BPS = "earlyRedeemFeeBps"; // [A, B, C]: of the debt at pMint, a redeem before lockHeight
    constexpr const char* NETWORK             = "network";
}

namespace ParamsFeesProtocol {   // contract: yed_getinfo.params.fees.protocol
    constexpr const char* BANDS               = "bands";             // marginal brackets, the last with uptoCents null (unbounded)
    constexpr const char* FLOOR_CENTS         = "floorCents";        // $1 per action (mint, redeem, claim)
    constexpr const char* ATTEST_FEE_BPS      = "attestFeeBps";      // AFEE-1': the attestors' share of the protocol fee
    constexpr const char* PRICE_SOURCE        = "priceSource";       // "pMint": the vault's stored mint price
}

namespace FeeBand {   // contract: yed_getinfo.params.fees.protocol.bands[]
    constexpr const char* UPTO_CENTS          = "uptoCents";         // null: unbounded
    constexpr const char* BPS                 = "bps";
}

// The network fee: dollars per KB at the attested tip price (fees spec §3.1). A transaction of n bytes pays
// max(flatZat, zatPerKB × n / 1000), a vault / Yellowback one at most yellowback.maxTxFeeZat.
namespace ParamsFeesNetwork {   // contract: yed_getinfo.params.fees.network
    constexpr const char* PRICE_MICRO_USD     = "priceMicroUsd";     // null without an attested price (then the fallback rate)
    constexpr const char* PRICE_SOURCE        = "priceSource";       // "tip" | "fallback"
    constexpr const char* PLAIN               = "plain";             // a plain post-quantum transaction ($0.02/KB)
    constexpr const char* YELLOWBACK          = "yellowback";        // a vault / Yellowback transaction ($0.05/KB)
    constexpr const char* FLOOR_ZAT_PER_KB    = "floorZatPerKB";
    constexpr const char* FALLBACK_ZAT_PER_KB = "fallbackZatPerKB";  // -pqfeerate
    constexpr const char* FLAT_ZAT            = "flatZat";           // the flat floor of one transaction (= params.feeZat)
    constexpr const char* MAX_TX_FEE_ZAT      = "maxTxFeeZat";       // -maxtxfee

    constexpr const char* SOURCE_TIP          = "tip";               // value
    constexpr const char* SOURCE_FALLBACK     = "fallback";          // value
}

namespace NetworkRate {   // contract: yed_getinfo.params.fees.network.yellowback
    constexpr const char* USD_PER_KB_MICRO    = "usdPerKBMicro";
    constexpr const char* ZAT_PER_KB          = "zatPerKB";          // the rate at the price, the floor included
    constexpr const char* MAX_TX_FEE_ZAT      = "maxTxFeeZat";       // yellowback only: the cap of a vault / Yellowback fee
}

// rpcversion 8 (D-Q-25, MINT-6b): the supply cap's base and the per-vault share limit.
namespace ParamsSupplyCap {   // contract: yed_getinfo.params.supplyCap
    constexpr const char* CIRCULATING_ZAT     = "circulatingZat";
    constexpr const char* PRICE_MICRO_USD     = "priceMicroUsd";     // null when undefined
    constexpr const char* CAP_CENTS           = "capCents";          // null when undefined
    constexpr const char* MAX_VAULT_SHARE_BPS = "maxVaultShareBps";  // 1000: one mint at most 10 % of the cap ...
    constexpr const char* VAULT_SHARE_FLOOR_CENTS = "vaultShareFloorCents"; // ... but always at least $10,000
    constexpr const char* MAX_VAULT_CENTS     = "maxVaultCents";     // null when the cap is undefined (no limit)
}

// rpcversion 7: yed_getinfo.params.pq, the post-quantum signature schemes the network accepts.
namespace ParamsPq {   // contract: yed_getinfo.params.pq
    constexpr const char* SCHEMES             = "schemes";           // [1] (SLH-DSA-SHA2-128s), [1, 2] once Falcon (FN-DSA-512) is active
    constexpr const char* FALCON_ACTIVE       = "falconActive";      // YED holders must be Falcon PQ addresses from then on (TOK-PQ)
    constexpr const char* FALCON_HEIGHT       = "falconHeight";      // -1 while no height is set
    constexpr const char* FEE_RATE_ZAT_PER_KB = "feeRateZatPerKB";   // the node's -pqfeerate: fees grow with a transaction's size
    constexpr const char* PLAIN_ADDRESS_VERSION = "plainAddressVersion"; // hex version of a plain-YEC PQ address (D-Q-20: sq/tq/rq)
}

// rpcversion 7: the post-quantum scheme identifiers (ownerScheme, yed_validateaddress.pqscheme).
namespace PqScheme {
    constexpr int SLH_DSA_SHA2_128S = 1;   // vault owners: a ~7.9 KB signature, signed by the node in ~0.25-0.6 s
    constexpr int FN_DSA_512        = 2;   // YED holders once Falcon is active: a ~1.6 KB signature
}

// v3: yed_getinfo.params.attest, the values the Mint page and the Attestors view derive from.
namespace ParamsAttest {   // contract: yed_getinfo.params.attest
    constexpr const char* REQUIRED            = "required";
    constexpr const char* M_SELECT            = "mSelect";
    constexpr const char* K_SLACK             = "kSlack";
    constexpr const char* DIVERGE_BPS_ATTEST  = "divergeBpsAttest";  // mint10-diverged above this
    constexpr const char* ARM_DELAY           = "armDelay";
    constexpr const char* ARM_MIN             = "armMin";
    constexpr const char* EMERGENCY_PERSIST   = "emergencyPersist";
    constexpr const char* CARRIER_MODE        = "carrierMode";
    constexpr const char* BOND_MIN_ZAT        = "bondMinZat";       // A5-b: the register dialog states them. D-Q-30: computed, the floor a
                                                                    // join in the next block must meet (follows YED vault collateral)
    constexpr const char* BOND_MIN_FLOOR_ZAT  = "bondMinFloorZat";  // D-Q-30: bondMinZat never below this, nor above the cap
    constexpr const char* BOND_MIN_CAP_ZAT    = "bondMinCapZat";    // a bond of the cap always qualifies
    constexpr const char* BOND_SHARE_BPS      = "bondShareBps";     // the share of vault collateral the seats back together
    constexpr const char* BOND_MIN_LOCK       = "bondMinLock";      // 0 since the bond exit delay (no year lock)
    constexpr const char* N_SLOTS             = "nSlots";           // the seated attestors per snapshot
    constexpr const char* SEATS               = "seats";            // the OPEN attestor set's seats; a larger bond can evict
    constexpr const char* SEAT_AGE_UNIT       = "seatAgeUnit";
    constexpr const char* SEAT_AGE_CAP        = "seatAgeCap";
    constexpr const char* SEAT_MARGIN_BPS     = "seatMarginBps";
    constexpr const char* EVICT_MARGIN_BPS    = "evictMarginBps";
    constexpr const char* BOND_MATURITY       = "bondMaturity";
    constexpr const char* ATTEST_FEE_BPS      = "attestFeeBps";     // AFEE-1; H-9.3 recomputes the attestor fee from it
}

namespace ParamClass {   // contract: yed_getinfo.params.classes[]
    constexpr const char* CLASS               = "class";        // "A" | "B" | "C"
    constexpr const char* MIN_BLOCKS          = "minBlocks";
    constexpr const char* MAX_BLOCKS          = "maxBlocks";
    constexpr const char* BASE_RATIO_BPS      = "baseRatioBps";
    constexpr const char* EARLY_REDEEM_FEE_BPS = "earlyRedeemFeeBps";  // rpcversion 6 (IT-9)
}

// ── yed_getstats result ────────────────────────────────────────────────────────────────────
namespace Stats {   // contract: yed_getstats
    constexpr const char* HEIGHT            = "height";
    constexpr const char* SUPPLY_CENTS      = "supplyCents";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* ACTIVE_VAULTS     = "activeVaults";
    constexpr const char* VOID_VAULTS       = "voidVaults";
    constexpr const char* CLOSED_VAULTS     = "closedVaults";
    constexpr const char* CLAIMED_VAULTS    = "claimedVaults";
    constexpr const char* UNBACKED_CENTS    = "unbackedCents";
    constexpr const char* ISSUED_ZAT        = "issuedZat";
    constexpr const char* P_FAST            = "pFast";          // null when undefined
    constexpr const char* P_MID             = "pMid";           // null when undefined
    constexpr const char* P_SLOW            = "pSlow";          // null when undefined
    constexpr const char* P_MINT            = "pMint";          // null when undefined
    constexpr const char* P_CLAIM           = "pClaim";         // null when undefined
    constexpr const char* SIGMA_MULT_BPS    = "sigmaMultBps";
    constexpr const char* GLOBAL_RATIO_BPS  = "globalRatioBps"; // null when no supply
    constexpr const char* SUPPLY_CAP_CENTS  = "supplyCapCents"; // null when no cap
    constexpr const char* HALT_MASK         = "haltMask";       // [names]; empty when minting is open
    constexpr const char* MINTING_ALLOWED   = "mintingAllowed";
    constexpr const char* MINTABLE_CLASSES  = "mintableClasses"; // v3 W16: the classes a mint can use now ([] = none)

    // haltMask names (§3.6)
    constexpr const char* HALT_NOT_ACTIVE   = "NOT_ACTIVE";     // value
    constexpr const char* HALT_NO_PRICE     = "NO_PRICE";       // value
    constexpr const char* HALT_GLOBAL_RATIO = "GLOBAL_RATIO";   // value
    constexpr const char* HALT_DIVERGENCE   = "DIVERGENCE";     // value
}

// ── yed_getactivation result (rpcversion 5: the vault upgrade, the Upgrade keys) ───────────
namespace ActivationInfo {   // contract: yed_getactivation
    constexpr const char* NAME              = "name";
    constexpr const char* BRANCH_ID         = "branchId";
    constexpr const char* STATUS            = "status";
    constexpr const char* ACTIVATION_HEIGHT = "activationHeight";
    constexpr const char* ATTESTOR_SET_ID   = "attestorSetId";
    constexpr const char* CLAIM_DELAY       = "claimDelay";
    constexpr const char* HEIGHT            = "height";
}

// ── yed_getbalance result ──────────────────────────────────────────────────────────────────
namespace Balance {   // contract: yed_getbalance
    constexpr const char* CONFIRMED_CENTS   = "confirmedCents";
    constexpr const char* UNCONFIRMED_CENTS = "unconfirmedCents";
    constexpr const char* HEIGHT            = "height";
}

// ── yed_getvault result / yed_listpositions element ────────────────────────────────────────
// yed_listpositions rows are yed_getvault rows plus the can* flags; both are read with the same
// constants (the checker verifies them against yed_listpositions[], a superset). yed_listvaults
// rows are yed_getvault rows.
namespace Position {   // contract: yed_listpositions[]
    constexpr const char* TXID              = "txid";
    constexpr const char* VOUT              = "vout";
    constexpr const char* STATUS            = "status";        // ACTIVE | CLAIMING | CLOSED | CLAIMED (| VOID before the upgrade)
    constexpr const char* OWNER_SCHEME      = "ownerScheme";   // rpcversion 7: PqScheme (1 = SLH-DSA); replaces ownerPubKey
    constexpr const char* OWNER_HASH        = "ownerHash";     // rpcversion 7: SHA256(scheme || pk), 64 hex in byte order; replaces ownerKeyId
    constexpr const char* OWNER_ADDRESS     = "ownerAddress";  // rpcversion 7: the 53-character PQ address: a vault identifier, never a YED receive address
    constexpr const char* TERM_CLASS        = "termClass";     // "A" | "B" | "C"
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* COLLATERAL        = "collateral";    // decimal YEC twin
    constexpr const char* MINTED_CENTS      = "mintedCents";
    constexpr const char* MINT_HEIGHT       = "mintHeight";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* FEE_PAID_ZAT      = "feePaidZat";
    constexpr const char* P_MINT            = "pMint";         // rpcversion 8: the stored mint price (µUSD/YEC) every fee of the vault is struck at
    constexpr const char* CLOSE_HEIGHT      = "closeHeight";   // null until CLOSED/CLAIMED
    constexpr const char* CLOSING_TXID      = "closingTxid";   // "" until CLOSED/CLAIMED
    constexpr const char* BURNED_CENTS      = "burnedCents";
    constexpr const char* UNBACKED          = "unbacked";      // closed without its burn (no rule produces one since the upgrade)
    constexpr const char* CLAIMABLE         = "claimable";
    constexpr const char* UNDERWATER_AT     = "underwaterAt";  // µUSD; null for VOID
    constexpr const char* VOID_REASON       = "voidReason";    // "" unless VOID (none is produced since the upgrade)
    constexpr const char* SCRIPT_PUBKEY     = "scriptPubKey";  // rpcversion 5: the vault's V template in hex
    constexpr const char* INTENTS           = "intents";       // optional: present while CLAIMING (PositionIntent rows)
    constexpr const char* CANCEL_COUNT      = "cancelCount";   // CAN-2: the position's cancels counted by the escalation schedule
    constexpr const char* CANCELS_REQUIRED  = "cancelsRequired"; // CAN-2: co-signatures of distinct current members a cancel confirming next block needs
    constexpr const char* CANCEL_QUIET_UNTIL = "cancelQuietUntil"; // CAN-2: null when never cancelled
    constexpr const char* REFUND_HOLDER     = "refundHolder";  // optional, CAN-1: while CLAIMING, the script a cancel refunds the burn to
    constexpr const char* CAN_REDEEM        = "canRedeem";     // listpositions only
    constexpr const char* CAN_CLAIM         = "canClaim";      // listpositions only
    constexpr const char* NOTICED           = "noticed";       // v3: a Notices record stands (NOT-1)
    constexpr const char* NOTICE_HEIGHT     = "noticeHeight";  // v3: null unless noticed
    constexpr const char* EMERGENCY_OPEN_AT = "emergencyOpenAt"; // v3: first refHeight a clause-(b) claim may cite; null unless noticed
    constexpr const char* CAN_NOTICE        = "canNotice";     // v3, listpositions only
    constexpr const char* EARLY_REDEEM_FEE_ZAT = "earlyRedeemFeeZat"; // rpcversion 6 (IT-9), listpositions only: what a redeem now would pay on top of FEE-1

    constexpr const char* STATUS_ACTIVE     = "ACTIVE";        // value
    constexpr const char* STATUS_VOID       = "VOID";          // value
    constexpr const char* STATUS_CLOSED     = "CLOSED";        // value
    constexpr const char* STATUS_CLAIMED    = "CLAIMED";       // value
    constexpr const char* STATUS_CLAIMING   = "CLAIMING";      // value, rpcversion 5: a claim intent awaits release or cancel
}

// rpcversion 5: one claim intent of a CLAIMING vault (yed_getvault.intents, absent from the example).
namespace PositionIntent {   // contract: optional yed_getvault.intents[]
    constexpr const char* TXID              = "txid";
    constexpr const char* VOUT              = "vout";
    constexpr const char* ROLE              = "role";          // claimant | residual
    constexpr const char* HEIGHT            = "height";        // the claim's height
    constexpr const char* RELEASE_HEIGHT    = "releaseHeight"; // height + CLAIM_DELAY: the first height vault_release can spend it

    constexpr const char* ROLE_CLAIMANT     = "claimant";      // value
    constexpr const char* ROLE_RESIDUAL     = "residual";      // value
}

// CAN-1 + CAN-2: yed_buildcancel's result (the unsigned cancel; sign with set_signcancel, send with vault_send)
namespace BuildCancel {   // contract: yed_buildcancel
    constexpr const char* HEX               = "hex";
    constexpr const char* REQUIRED          = "required";          // the cancel set's cancelthreshold (1, pinned by the module)
    constexpr const char* CANCEL_SET_ID     = "cancelsetid";
    constexpr const char* DEADLINE          = "deadline";          // the last height the cancel can confirm at
    constexpr const char* INTENT_CONFIRMED  = "intentconfirmed";
    constexpr const char* REFUND            = "refund";            // BuildCancelRefund
    constexpr const char* CANCELS_REQUIRED  = "cancelsRequired";
    constexpr const char* COSIGNERS         = "cosigners";         // the co-signers' seqs, carrier order
    constexpr const char* CARRIER_TXID      = "carrierTxid";       // the carrier holding the co-signatures
    constexpr const char* WARNINGS          = "warnings";          // optional: "cosigs[i] dropped: <identifier>: <why>"
}

namespace BuildCancelRefund {   // contract: yed_buildcancel.refund
    constexpr const char* VOUT              = "vout";              // always 1
    constexpr const char* SCRIPT            = "script";            // the holder of the claim's lowest-index YED input
    constexpr const char* CENTS             = "cents";             // the claim's whole burn, re-issued
}

// CAN-2 + C4b: yed_signcancelcosign's result, a member's co-signature and price statement at the claim's refHeight
namespace CancelCosign {   // contract: yed_signcancelcosign
    constexpr const char* COSIG             = "cosig";             // 69 bytes of hex: price LE32 || sig65
    constexpr const char* SEQ               = "seq";
    constexpr const char* PRICE_MICRO_USD   = "priceMicroUsd";
    constexpr const char* CITED_HEIGHT      = "citedHeight";       // the claim's refHeight
    constexpr const char* REUSED            = "reused";            // the guard already held this price
}

// ── yed_listclaimable element ──────────────────────────────────────────────────────────────
namespace Claimable {   // contract: yed_listclaimable[]
    constexpr const char* VAULT             = "vault";         // "txid:0"
    constexpr const char* OWNER_ADDRESS     = "ownerAddress";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* MINTED_CENTS      = "mintedCents";   // the burn a claim must carry
    constexpr const char* P_MINT            = "pMint";         // rpcversion 8: the vault's stored mint price
    constexpr const char* FEE_ZAT           = "feeZat";        // rpcversion 8: FEE-1' of the debt at pMint
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* UNDERWATER_AT     = "underwaterAt";
    constexpr const char* P_CLAIM           = "pClaim";
    constexpr const char* CLAIM_PATH        = "claimPath";     // v3: "a" | "b" | "" (no bundle could be built)
    constexpr const char* RESIDUAL_ZAT      = "residualZat";   // v3: RED-5, returned to the owner by the claim
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";  // v3: AFEE-1
    constexpr const char* NOTICED           = "noticed";       // v3
    constexpr const char* NOTICE_HEIGHT     = "noticeHeight";  // v3: null unless noticed
    constexpr const char* EMERGENCY_OPEN_AT = "emergencyOpenAt"; // v3: null unless noticed
    // rpcversion 6 (IT-7): every ACTIVE vault whose claim branch is open is listed, in term too;
    // a row with claimable false is above the threshold and underwaterAt is the price it becomes claimable at
    constexpr const char* CLAIMABLE         = "claimable";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";    // tip < lockHeight: a claim in term
}

// ── yed_listtransactions element ───────────────────────────────────────────────────────────
namespace Transaction {   // contract: yed_listtransactions[]
    constexpr const char* TXID              = "txid";
    constexpr const char* HEIGHT            = "height";        // -1 for an expired row
    constexpr const char* CONFIRMATIONS     = "confirmations";
    constexpr const char* TYPE              = "type";
    constexpr const char* VERDICT           = "verdict";       // "expired" for an expired row
    constexpr const char* PATH              = "path";          // owner | claim | ""
    constexpr const char* YED_IN            = "yedIn";
    constexpr const char* YED_OUT           = "yedOut";
    constexpr const char* BURNED            = "burned";
    constexpr const char* AMOUNT_CENTS      = "amountCents";
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* PAYEE             = "payee";         // null where there is no fee output
    constexpr const char* UNBACKED          = "unbacked";
    constexpr const char* EXPIRED           = "expired";

    constexpr const char* TYPE_MINT         = "mint";          // value
    constexpr const char* TYPE_SEND         = "send";          // value
    constexpr const char* TYPE_RECEIVE      = "receive";       // value
    constexpr const char* TYPE_BURN         = "burn";          // value
    constexpr const char* TYPE_REDEEM       = "redeem";        // value
    constexpr const char* TYPE_CLAIM        = "claim";         // value
    constexpr const char* TYPE_CLAIMED      = "claimed";       // value
    constexpr const char* TYPE_NOTICE       = "notice";        // value, v3: a CLAIM_NOTICE this wallet posted
    constexpr const char* TYPE_NOTICED      = "noticed";       // value, v3: a notice against a vault of ours
    constexpr const char* TYPE_REGISTER     = "register";      // value, v3
    constexpr const char* TYPE_EQUIVOCATION = "equivocation";  // value, v3
    constexpr const char* TYPE_REVIVE       = "revive";        // value, v3 (not written since P4-b)
    // rpcversion 5: yed_gettxinfo types of the claim intent spends and of the attestor set's acts
    constexpr const char* TYPE_CLAIM_RELEASE = "claim_release"; // value
    constexpr const char* TYPE_CLAIM_CANCEL = "claim_cancel";  // value
    constexpr const char* TYPE_SET_ACT      = "set_act";       // value

    constexpr const char* VERDICT_EXPIRED   = "expired";       // value
}

// ── yed_estimatecollateral result ──────────────────────────────────────────────────────────
namespace Estimate {   // contract: yed_estimatecollateral
    constexpr const char* REQUIRED_ZAT      = "requiredZat";   // null when pMint is undefined
    constexpr const char* TERM_CLASS        = "termClass";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* MIN_RATIO_BPS     = "minRatioBps";
    constexpr const char* BASE_RATIO_BPS    = "baseRatioBps";
    constexpr const char* SIGMA_MULT_BPS    = "sigmaMultBps";
    constexpr const char* P_MINT            = "pMint";         // null when undefined; v3: min(xMint, aMint) while armed
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* X_MINT            = "xMint";         // v3: the pool cross-section bound
    constexpr const char* A_MINT            = "aMint";         // v3: the attestation quantile bound; null when not armed
    constexpr const char* SOURCE            = "source";        // v3: "x" | "a" | "" (not armed, or a price override)
    constexpr const char* ARMED             = "armed";         // v3
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";    // v3: the seqs the estimate used
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";  // v3: AFEE-1', on top of the protocol fee
    constexpr const char* PROTOCOL_FEE_CENTS = "protocolFeeCents"; // rpcversion 8: FEE-1' of the debt in cents
    constexpr const char* PROTOCOL_FEE_ZAT  = "protocolFeeZat";   // rpcversion 8: the same at pMint; null with pMint
    constexpr const char* DIVERGENCE_BPS    = "divergenceBps"; // v3: |xMint - aMint| / min; null when either is undefined

    constexpr const char* SOURCE_X          = "x";             // value
    constexpr const char* SOURCE_A          = "a";             // value
}

// ── v3: yed_getprice result (the snapshot at a height; the Overview's two source prices) ──
namespace Price {   // contract: yed_getprice
    constexpr const char* HEIGHT            = "height";
    constexpr const char* P_MINT            = "pMint";         // = xMint (the v2 key keeps the snapshot value)
    constexpr const char* P_CLAIM           = "pClaim";        // = xClaim
    constexpr const char* X_MINT            = "xMint";         // null when undefined
    constexpr const char* X_CLAIM           = "xClaim";        // null when undefined
    constexpr const char* ARMED             = "armed";
    constexpr const char* ATTEST_STATUS     = "attestStatus";  // UNARMED | TRIGGERED | ARMED
    constexpr const char* SEATED            = "seated";        // seqs
    constexpr const char* PINNED_SEQS       = "pinnedSeqs";
    constexpr const char* PINNED_KEYS       = "pinnedKeys";
}

// ── v3: yed_listattestors element (the Attestors view) ────────────────────────────────────
namespace Attestor {   // contract: yed_listattestors[]
    constexpr const char* SEQ               = "seq";
    constexpr const char* STATUS            = "status";        // PENDING | ELIGIBLE | DORMANT | EJECTED | WITHDRAWN
    constexpr const char* STATUS_HEIGHT     = "statusHeight";
    constexpr const char* ATTESTOR_PUBKEY   = "attestorPubKey";
    constexpr const char* BOND_ADDRESS      = "bondAddress";
    constexpr const char* BOND_KEY_ADDRESS  = "bondKeyAddress";
    constexpr const char* BOND_ZAT          = "bondZat";
    constexpr const char* BOND_LOCKTIME     = "bondLocktime";
    constexpr const char* BOND_SPENT_HEIGHT = "bondSpentHeight";   // null until spent
    constexpr const char* REGISTER_HEIGHT   = "registerHeight";
    constexpr const char* WEIGHT            = "weight";        // decimal string
    constexpr const char* SEATED            = "seated";
    constexpr const char* SEATED_SINCE      = "seatedSince";   // null while unseated
    constexpr const char* PINNED            = "pinned";
    constexpr const char* FOUNDING          = "founding";
    constexpr const char* FLAGS             = "flags";
    constexpr const char* LAST_BUNDLE_HEIGHT= "lastBundleHeight";  // null when none
    constexpr const char* POOL_FRESH        = "poolFresh";
    constexpr const char* LAST_ACT          = "lastAct";       // P4-b: the set's lastAct for the member (join + maturity, or its last heartbeat)
    constexpr const char* BOND_FROZEN       = "bondFrozen";    // P4-b: the set froze the bond (removal with burn, set or price equivocation)
    constexpr const char* SEAT_WEIGHT       = "seatWeight";    // decimal string, Q16 (divide by 65,536 for zat): the seat ranking
    constexpr const char* EVICTED           = "evicted";       // WITHDRAWN by a larger bond's join into the full set (bond not frozen)

    constexpr const char* STATUS_PENDING    = "PENDING";       // value
    constexpr const char* STATUS_ELIGIBLE   = "ELIGIBLE";      // value
    constexpr const char* STATUS_DORMANT    = "DORMANT";       // value
    constexpr const char* STATUS_EJECTED    = "EJECTED";       // value
    constexpr const char* STATUS_WITHDRAWN  = "WITHDRAWN";     // value
}

namespace AttestorFlags {   // contract: yed_listattestors[].flags
    constexpr const char* TIER              = "tier";          // 0 exchange APIs | 1 mixed | 2 aggregator
    constexpr const char* POOL              = "pool";          // operates a mining pool
}

// ── v3: yed_getselection result ("n of m selected attestors reachable") ───────────────────
namespace Selection {   // contract: yed_getselection
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* ARMED             = "armed";
    constexpr const char* M_SELECT          = "mSelect";
    constexpr const char* K_SLACK           = "kSlack";
    constexpr const char* POOL              = "pool";          // seqs
    constexpr const char* SELECTED          = "selected";      // rows
    constexpr const char* REACHABLE         = "reachable";     // count of poolFresh among selected
    constexpr const char* FALLBACK          = "fallback";
    constexpr const char* SUM_WEIGHT        = "sumWeight";
}

namespace SelectionRow {   // contract: yed_getselection.selected[]
    constexpr const char* SEQ               = "seq";
    constexpr const char* STATUS            = "status";
    constexpr const char* WEIGHT            = "weight";
    constexpr const char* BOND_KEY_ADDRESS  = "bondKeyAddress";
    constexpr const char* POOL_FRESH        = "poolFresh";
}

// ── yed_getfeepayee result (the mint / redeem confirmation shows the fee and its payee) ────
namespace FeePayee {   // contract: yed_getfeepayee
    constexpr const char* ELIGIBLE          = "eligible";      // E(refHeight)
    constexpr const char* DEFAULT           = "default";       // the FEE-W choice
    constexpr const char* PREFERRED         = "preferred";     // optional: the configured preference, when eligible
    constexpr const char* POLICY            = "policy";
}

namespace FeePayeeDefault {   // contract: yed_getfeepayee.default
    constexpr const char* PAYOUT_ADDRESS    = "payoutAddress";
    constexpr const char* WEIGHT            = "weight";
}

// rpcversion 8: yed_estimatefee <mintedCents> [priceMicroUsd], the protocol fee of a debt.
namespace EstimateFee {   // contract: yed_estimatefee
    constexpr const char* FEE_CENTS         = "feeCents";      // FEE-1' by the bands, the $1 floor included
    constexpr const char* FEE_ZAT           = "feeZat";        // at pMint; null when no price is defined
    constexpr const char* P_MINT            = "pMint";         // the estimate's price; null when undefined
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";  // AFEE-1''s share of feeZat; null with it
}

// ── yed_mint result ────────────────────────────────────────────────────────────────────────
namespace MintResult {   // contract: yed_mint
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";         // "txid:0"
    constexpr const char* TERM_CLASS        = "termClass";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* PAYEE             = "payee";         // null under FEE-0
    constexpr const char* FUNDED_FROM       = "fundedFrom";    // "transparent" | "sapling"
    constexpr const char* WARNING           = "warning";       // keypool-low nag, "" when none
    // v3 (W7): with wait = false the call returns after the carrier broadcast with pending = true,
    // carrierTxid set and every other field at its zero value; the main transaction follows on
    // the next ChainTip and yed_listtransactions / yed_gettxinfo then carry it.
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* X_MINT            = "xMint";
    constexpr const char* A_MINT            = "aMint";         // null when not armed
    constexpr const char* P_MINT            = "pMint";
    constexpr const char* SOURCE            = "source";        // "x" | "a" | ""
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";
    constexpr const char* ATTEST_PAYEE      = "attestPayee";   // the bondKeyAddress paid; null under AFEE-0
    constexpr const char* OWNER_ADDRESS     = "ownerAddress";  // rpcversion 7: the new vault's PQ owner (53 characters)
}

// ── yed_send result ────────────────────────────────────────────────────────────────────────
namespace SendResult {   // contract: yed_send
    constexpr const char* TXID              = "txid";
    constexpr const char* CHANGE_CENTS      = "changeCents";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
}

// ── yed_redeem / yed_claim result (same shape) ────────────────────────────────────────────
namespace RedeemResult {   // contract: yed_redeem
    constexpr const char* TXID              = "txid";
    constexpr const char* BURNED_CENTS      = "burnedCents";   // 0 for a VOID release (L14)
    constexpr const char* FEE_ZAT           = "feeZat";        // 0 for a VOID release
    constexpr const char* PAYEE             = "payee";         // null for a VOID release
    constexpr const char* COLLATERAL_OUT    = "collateralOut";
    constexpr const char* TO                = "to";
    constexpr const char* EXTRA_BURN_CENTS  = "extraBurnCents";  // H4: a sub-dollar remainder burned with the debt
    constexpr const char* EARLY_REDEEM_FEE_ZAT = "earlyRedeemFeeZat"; // rpcversion 6 (IT-9): the part of feeZat due to a redeem before lockHeight
}

// rpcversion 6 (IT-9): yed_estimateredeem, the quote of a redeem confirming in the next block.
namespace EstimateRedeem {   // contract: yed_estimateredeem
    constexpr const char* VAULT             = "vault";
    constexpr const char* STATUS            = "status";
    constexpr const char* TERM_CLASS        = "termClass";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* HEIGHT            = "height";        // tip + 1, the block the quote is for
    constexpr const char* EARLY             = "early";         // height < lockHeight on an ACTIVE vault
    constexpr const char* BURNED_CENTS      = "burnedCents";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* P_MINT            = "pMint";         // rpcversion 8: the vault's stored mint price, every fee's price
    constexpr const char* PROTOCOL_FEE_CENTS = "protocolFeeCents"; // rpcversion 8: FEE-1' of the debt in cents
    constexpr const char* PROTOCOL_FEE_ZAT  = "protocolFeeZat";   // rpcversion 8: the same at pMint (0 under FEE-0)
    constexpr const char* FEE_ZAT           = "feeZat";        // the whole fee output: the protocol fee plus the early-redeem fee (0 under FEE-0)
    constexpr const char* EARLY_REDEEM_FEE_BPS = "earlyRedeemFeeBps"; // rpcversion 8: of the debt
    constexpr const char* EARLY_REDEEM_FEE_CENTS = "earlyRedeemFeeCents"; // rpcversion 8: the early fee in dollars (0 when none is due)
    constexpr const char* EARLY_REDEEM_FEE_ZAT = "earlyRedeemFeeZat";
    constexpr const char* PAYEE             = "payee";         // null when no pool is eligible
    constexpr const char* CAN_REDEEM        = "canRedeem";
    constexpr const char* ERROR             = "error";         // what yed_redeem would refuse with now; "" otherwise
}

// v3: the fields yed_claim adds to the yed_redeem shape (read alongside RedeemResult).
namespace ClaimResult {   // contract: yed_claim
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* X_CLAIM           = "xClaim";
    constexpr const char* A_CLAIM           = "aClaim";
    constexpr const char* P_CLAIM           = "pClaim";
    constexpr const char* P_EMERG           = "pEmerg";        // null under clause (a)
    constexpr const char* CLAIM_PATH        = "claimPath";     // "a" underwater | "b" emergency clause
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";
    constexpr const char* ATTEST_PAYEE      = "attestPayee";
    constexpr const char* RESIDUAL_ZAT      = "residualZat";   // RED-5: returned to the vault owner

    constexpr const char* PATH_A            = "a";             // value
    constexpr const char* PATH_B            = "b";             // value
}

// v3: yed_claimnotice result (step 1 of the emergency claim, NOT-1).
namespace NoticeResult {   // contract: yed_claimnotice
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* EMERGENCY_OPEN_AT = "emergencyOpenAt";  // refHeight + EMERGENCY_PERSIST
    constexpr const char* X_CLAIM           = "xClaim";
    constexpr const char* A_CLAIM           = "aClaim";
    constexpr const char* P_EMERG           = "pEmerg";
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
}

// v3: yed_getnotice, the standing notice record of a vault. A pending yed_claimnotice is
// followed through it: yed_listtransactions has no "notice" row on either node line.
namespace NoticeRecord {   // contract: yed_getnotice
    constexpr const char* FOUND             = "found";
    constexpr const char* TXID              = "txid";
    constexpr const char* HEIGHT            = "height";
    constexpr const char* REF_HEIGHT        = "refHeight";
}

// v3: yed_gettxinfo, the fields the two-step actions read once the main transaction exists
// (the v2 fields the Transactions view reads are in the Transaction namespace above).
namespace TxInfo {   // contract: yed_gettxinfo
    constexpr const char* TXID              = "txid";
    constexpr const char* TYPE              = "type";
    constexpr const char* HEIGHT            = "height";
    constexpr const char* VERDICT           = "verdict";
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* PAYEE             = "payee";
    constexpr const char* P_MINT            = "pMint";
    constexpr const char* X_MINT            = "xMint";
    constexpr const char* A_MINT            = "aMint";
    constexpr const char* P_CLAIM           = "pClaim";
    constexpr const char* X_CLAIM           = "xClaim";
    constexpr const char* A_CLAIM           = "aClaim";
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";
    constexpr const char* ATTEST_PAYEE      = "attestPayee";
    constexpr const char* CLAIM_PATH        = "claimPath";
    constexpr const char* RESIDUAL_ZAT      = "residualZat";
    constexpr const char* NOTICE            = "notice";        // true when this transaction wrote a notice
    constexpr const char* BURNED            = "burned";
    constexpr const char* CLOSED_VAULTS     = "closedVaults";  // [{txid, vout}]
    constexpr const char* REOPENED_VAULTS   = "reopenedVaults";  // rpcversion 5: the vault an attestor cancel re-created
}

// v3: yed_sweepcarriers result (W7).
namespace SweepCarriersResult {   // contract: yed_sweepcarriers
    constexpr const char* TXID              = "txid";          // "" when count is 0
    constexpr const char* COUNT             = "count";
    constexpr const char* RECLAIMED_ZAT     = "reclaimedZat";
    constexpr const char* OUTSTANDING       = "outstanding";   // carriers still inside their window
}

// v3: yed_registerattestor result. `seq` is null until the registration confirms (REG-A1).
namespace RegisterResult {   // contract: yed_registerattestor
    constexpr const char* TXID              = "txid";
    constexpr const char* SEQ               = "seq";
    constexpr const char* ATTESTOR_PUBKEY   = "attestorPubKey";
    constexpr const char* BOND_ADDRESS      = "bondAddress";
    constexpr const char* BOND_KEY_ADDRESS  = "bondKeyAddress";
    constexpr const char* BOND_ZAT          = "bondZat";
    constexpr const char* BOND_LOCKTIME     = "bondLocktime";
    constexpr const char* MATURES_AT        = "maturesAt";
    constexpr const char* WARNING           = "warning";       // "" or "bond-near-min: ..." (D-Q-30): post 10 % headroom or the cap
}

// v3: yed_withdrawbond result.
namespace WithdrawResult {   // contract: yed_withdrawbond
    constexpr const char* TXID              = "txid";
    constexpr const char* SEQ               = "seq";
    constexpr const char* BOND_ZAT          = "bondZat";
    constexpr const char* BOND_OUT          = "bondOut";       // net of the network fee
    constexpr const char* TO                = "to";
}

// v3: yed_reportequivocation result (two-step, as yed_mint).
namespace EquivocationResult {   // contract: yed_reportequivocation
    constexpr const char* TXID              = "txid";
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* SEQ               = "seq";
    constexpr const char* CITED_HEIGHT      = "citedHeight";
    constexpr const char* PRICE_A           = "priceA";
    constexpr const char* PRICE_B           = "priceB";
}

// ── yed_validateaddress result ─────────────────────────────────────────────────────────────
namespace ValidateAddress {   // contract: yed_validateaddress
    constexpr const char* ISVALID           = "isvalid";
    constexpr const char* ADDRESS           = "address";
    constexpr const char* TYPE              = "type";          // rpcversion 7: "p2pkh" | "pq"
    constexpr const char* PQSCHEME          = "pqscheme";      // optional, rpcversion 7: present only when type is "pq"
    constexpr const char* KEYID             = "keyid";         // rpcversion 7: for a PQ address the pqkeyid (scheme || hash, 66 hex)
    constexpr const char* ISMINE            = "ismine";
    constexpr const char* TRANSPARENT_ADDRESS = "transparentAddress";
    constexpr const char* REASON            = "reason";        // not-a-yellowback-address when !isvalid

    constexpr const char* TYPE_P2PKH        = "p2pkh";         // value
    constexpr const char* TYPE_PQ           = "pq";            // value
}

// rpcversion 7: yed_getnewaddress's optional argument. The wallet sends none: the node's default
// follows the holder policy (P2PKH while Falcon is inactive, a Falcon PQ address once active). It
// never asks for "pqowner": a vault owner key is not a YED receive address (clients survey C-10).
// The values are yed_validateaddress's `type` values plus "pqowner".
namespace NewAddressType {   // contract: yed_validateaddress
    constexpr const char* P2PKH             = "p2pkh";         // value
    constexpr const char* PQ                = "pq";            // value
    constexpr const char* PQ_OWNER          = "pqowner";       // value
}

// ── Error identifiers the wallet recognises (the first token of error.message) ────────────
// Node error strings are stable identifiers and are always shown verbatim; these are only
// the ones the wallet branches on.
namespace Errors {   // contract: errors
    // Every gated command while the index is unhealthy (unhealthyReason follows)
    constexpr const char* INDEX_UNHEALTHY   = "yellowback-unhealthy";
    // yed_send / yed_redeem / yed_claim: YED change would fall below the minimum output
    constexpr const char* CHANGE_FLOOR      = "change-floor";
    constexpr const char* NOT_YELLOWBACK_ADDRESS = "not-a-yellowback-address";
    constexpr const char* VAULT_LOCKED      = "vault-locked";
    constexpr const char* VAULT_NOT_FOUND   = "vault-not-found";
    constexpr const char* VAULT_NOT_ACTIVE  = "vault-not-active";     // CLOSED or CLAIMED only (L14)
    constexpr const char* VAULT_NOT_OWNED   = "vault-not-owned";
    constexpr const char* INSUFFICIENT_YED  = "insufficient-yed";
    // yed_mint: MINTPOL-1, one per halt bit and the cap
    constexpr const char* MINTPOL_NOT_ACTIVE    = "mintpol-not-active";
    constexpr const char* MINTPOL_NO_PRICE      = "mintpol-no-price";
    constexpr const char* MINTPOL_GLOBAL_RATIO  = "mintpol-global-ratio";
    constexpr const char* MINTPOL_DIVERGENCE    = "mintpol-divergence";
    constexpr const char* MINTPOL_CAP           = "mintpol-cap";
    constexpr const char* MINT_UNSATISFIABLE    = "mint-unsatisfiable";
    constexpr const char* MINT_BAD_LOCK         = "mint-bad-lock";
    constexpr const char* BAD_MINT_AMOUNT       = "bad-mint-amount";        // rpcversion 8: outside [MIN_MINT, params.maxMintCents] ($100,000)
    constexpr const char* MINTPOL_UNARMED       = "mintpol-unarmed";        // hardening H-1: mintRequiresArmed and the reference height not ARMED
    // The caps the wallet sends from its confirmation dialog (audit F-1, hardening H-9.3): yed_mint's
    // 6th argument maxCollateralZat, yed_claim's 5th argument minOutZat and 6th maxBurnCents
    // (rpcversion 4). The node refuses instead of building a vault (or a claim) the user did not
    // agree to when the reference price moved or the server inflated a figure.
    constexpr const char* COLLATERAL_ABOVE_MAX  = "collateral-above-max";
    constexpr const char* CLAIM_OUT_BELOW_MIN   = "claim-out-below-min";
    constexpr const char* CLAIM_BURN_ABOVE_MAX  = "claim-burn-above-max";
    constexpr const char* CLAIM_NOT_YET         = "claim-not-yet";
    constexpr const char* CLAIM_NOT_UNDERWATER  = "claim-not-underwater";
    constexpr const char* BAD_ADDRESS           = "bad-address";            // rpcversion 5: a claim to a Sapling address (D-U1)
    // The contract lists `mempool-check-failed:<verdict>`; the wallet matches the prefix
    constexpr const char* MEMPOOL_CHECK_FAILED  = "mempool-check-failed";   // value
    constexpr const char* FEE_NO_ELIGIBLE_PAYEE = "fee-no-eligible-payee";  // yed_getfeepayee under FEE-0: not an error for the wallet
    // v3: yed_estimatecollateral / yed_mint while armed. The Mint page shows the first as the
    // selection line and the second as "pools and attestors disagree by N %; minting paused".
    constexpr const char* BUNDLE_INSUFFICIENT   = "bundle-insufficient";     // "<count> of <selected> selected attestors have a fresh attestation; missing seq <a,b,…>"
    constexpr const char* MINT10_DIVERGED       = "mint10-diverged";
    constexpr const char* BUNDLE_MALFORMED      = "bundle-malformed";
    constexpr const char* INSUFFICIENT_YEC      = "insufficient-yec";        // yed_mint: the carrier and two fees are part of the need
    // v3: yed_claimnotice
    constexpr const char* NOTICE_STANDING       = "notice-standing";
    constexpr const char* NOTICE_NOT_UNDERWATER = "notice-not-underwater";
    // v3: the attestor actions
    constexpr const char* BOND_BELOW_MIN        = "bond-below-min";
    constexpr const char* LOCK_BELOW_MIN        = "lock-below-min";
    constexpr const char* BOND_LOCKED           = "bond-locked";
    constexpr const char* BOND_SPENT            = "bond-spent";
    constexpr const char* NOT_EQUIVOCATION      = "not-equivocation";
    constexpr const char* ATTEST_KEY_NOT_HELD   = "attest-key-not-held";
    constexpr const char* ATTEST_UNKNOWN_SEQ    = "attest-unknown-seq";
    constexpr const char* ATTEST_MALFORMED      = "attest-malformed";
    constexpr const char* ATTEST_RANGE          = "attest-range";
    constexpr const char* EQUIVOCATION_GUARD    = "equivocation-guard";
    // CAN-2 + C4b: the attestor cancel's co-signatures (yed_signcancelcosign, yed_buildcancel)
    constexpr const char* CANCEL_COSIGN_NEEDED  = "cancel-cosign-needed";
    constexpr const char* CANCEL_STATEMENT_CLAIMABLE = "cancel-statement-claimable";
    constexpr const char* PRICE_REQUIRED        = "price-required";
    constexpr const char* CLAIM_UNCONFIRMED     = "claim-unconfirmed";
}

// Wallet-side error matching that is not part of the yed_* contract (JSON-RPC and ycashd).
namespace RpcErrors {
    // JSON-RPC: the node has no yed_* methods (rpcversion 5: no vault upgrade or no YED attestor set;
    // the wallet probes yed_getinfo, never getexperimentalfeatures, which no longer lists yellowback)
    constexpr const char* METHOD_NOT_FOUND  = "Method not found";
    constexpr int         METHOD_NOT_FOUND_CODE = -32601;
    // yed_mint / yed_send / yed_redeem on an encrypted, locked wallet
    constexpr const char* WALLET_LOCKED     = "walletpassphrase";
    // yed_registerattestor since P4-b (the doc names them; the contract's error table does not)
    constexpr const char* REGISTER_NEEDS_ADMISSION = "register-needs-admission";
    constexpr const char* NO_ATTESTOR_SET   = "yellowback-no-attestor-set";
    // yed_registerattestor's warning (D-Q-30 review B2): the bond is below bondMinCapZat and within 10 % of bondMinZat
    constexpr const char* BOND_NEAR_MIN     = "bond-near-min";
    // rpcversion 8: the module's mint verdicts the wallet explains. The doc names them (MINT-6, MINT-6b) but
    // the contract's error table does not list them; they reach the wallet bare (a builder refusal) or after
    // mempool-check-failed:, so the wallet matches them anywhere in the message.
    constexpr const char* MINT_SUPPLY_CAP   = "mint-supply-cap";
    constexpr const char* MINT_VAULT_SHARE  = "mint-vault-share";
    constexpr const char* BAD_MINT_FEE      = "bad-mint-fee";      // MINT-8': the fee output below FEE-1'
}

// ── Display constants ─────────────────────────────────────────────────────────────────────
// Defaults only: everything the protocol fixes comes from yed_getinfo.params once the node
// has answered. SECONDS_PER_BLOCK is display-only (estimated dates).
constexpr int    SECONDS_PER_BLOCK   = 75;
constexpr qint64 MIN_MINT_CENTS      = 10000;     // $100 (§3.1 MIN_MINT)
constexpr qint64 MAX_MINT_CENTS      = 10000000;  // $100,000 (D-Q-24, every network; yed_getinfo.params.maxMintCents wins)
constexpr qint64 MIN_OUTPUT_CENTS    = 100;       // $1 change floor (§3.1 MIN_OUTPUT)
// v3: an attestation travels through RPC as 74 bytes of hex (yed_reportequivocation's arguments).
constexpr int ATTESTATION_HEX_LENGTH = 148;
// CAN-2: a cancel co-signature travels as 69 bytes of hex (yed_signcancelcosign's cosig, yed_buildcancel's cosigs).
constexpr int CANCEL_COSIG_HEX_LENGTH = 138;

}

#endif // YELLOWBACKRPC_H
