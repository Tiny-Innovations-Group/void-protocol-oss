// SPDX-License-Identifier: Apache-2.0
// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// VOID-051 — Minimal Escrow contract.
//
// Single-transaction settleBatch that records settlement intents for the
// flat-sat alpha path. The Go gateway (journey #14 / VOID-052 lands the
// RPC wiring) submits up to 10 intents per call; each entry is stored
// with status PENDING and announced via SettlementCreated so downstream
// receipt emission (#15 / VOID-135) can observe.
//
// FLAT-SAT SCOPE (journey #6): local Anvil only, no token transfers, no
// access control. Testnet promotion and hardening are Phase B
// (VOID-053 / journey #21).

pragma solidity ^0.8.20;

contract Escrow {
    // ── Types ────────────────────────────────────────────────────────

    enum Status {
        NONE,    // default slot = "this nonce was never submitted"
        PENDING  // intent recorded, awaiting off-chain settlement observer
    }

    /// @dev Submission-side shape. txNonce is opaque to the contract —
    /// the gateway derives it from the on-wire (sat_id || epoch_ts)
    /// tuple per VOID-110 so replays collapse to a single entry.
    struct SettlementIntent {
        uint32  satId;
        uint256 amount;
        uint16  assetId;
        uint256 txNonce;
    }

    /// @dev Storage-side shape. Adds the Status discriminant so the
    /// default mapping read (all-zero) reliably signals "never seen".
    struct Settlement {
        uint32  satId;
        uint256 amount;
        uint16  assetId;
        uint256 txNonce;
        Status  status;
    }

    // ── Constants ────────────────────────────────────────────────────

    uint16 public constant USDC_ASSET_ID = 1;
    uint8  public constant MAX_BATCH     = 10;

    // ── State ────────────────────────────────────────────────────────

    mapping(uint256 => Settlement) public settlements;

    // ── Events ───────────────────────────────────────────────────────

    event SettlementCreated(uint32 indexed satId, uint256 amount, uint256 indexed txNonce);

    // ── Custom errors ────────────────────────────────────────────────

    error BatchEmpty();
    error BatchTooLarge(uint256 size);
    error InvalidAsset(uint16 assetId);
    error DuplicateNonce(uint256 txNonce);

    // ── Entrypoint ───────────────────────────────────────────────────

    /// @notice Records 1..MAX_BATCH settlement intents, each keyed by
    ///         its txNonce. Reverts on empty batch, over-size batch,
    ///         non-USDC asset, or any duplicate nonce. All reverts are
    ///         all-or-nothing — a single bad entry aborts the whole
    ///         batch (no partial writes).
    /// @param intents Calldata array of submission-shape intents.
    function settleBatch(SettlementIntent[] calldata intents) external {
        uint256 n = intents.length;
        if (n == 0) revert BatchEmpty();
        if (n > MAX_BATCH) revert BatchTooLarge(n);

        for (uint256 i = 0; i < n; ++i) {
            SettlementIntent calldata item = intents[i];

            if (item.assetId != USDC_ASSET_ID) revert InvalidAsset(item.assetId);
            if (settlements[item.txNonce].status != Status.NONE) {
                revert DuplicateNonce(item.txNonce);
            }

            settlements[item.txNonce] = Settlement({
                satId:   item.satId,
                amount:  item.amount,
                assetId: item.assetId,
                txNonce: item.txNonce,
                status:  Status.PENDING
            });

            emit SettlementCreated(item.satId, item.amount, item.txNonce);
        }
    }
}
