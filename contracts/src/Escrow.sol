// SPDX-License-Identifier: Apache-2.0
// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// VOID-051 — Minimal Escrow contract.
//
// Single-transaction settleBatch that records settlement intents for the
// flat-sat alpha path. The Go gateway (journey #14 / VOID-052) submits
// up to 10 intents per call; each entry is stored with status PENDING
// and announced via SettlementCreated so downstream receipt emission
// (#15 / VOID-135) can observe.
//
// Journey Change Log v6 amendment (2026-04-19) — `address wallet` added
// to SettlementIntent + Settlement + SettlementCreated event so the
// on-chain record is self-contained. A block-explorer view of the
// settleBatch tx alone now tells the full payment-intent story without
// needing the gateway's off-chain receipts.json. `EmptyWallet()` rejects
// the zero address to keep "who gets paid" explicit on-chain.
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
    /// wallet is the seller's recipient address, looked up by the
    /// gateway from its sat_id → wallet registry before submission.
    struct SettlementIntent {
        uint32  satId;
        uint256 amount;
        uint16  assetId;
        uint256 txNonce;
        address wallet;
    }

    /// @dev Storage-side shape. Adds the Status discriminant so the
    /// default mapping read (all-zero) reliably signals "never seen".
    struct Settlement {
        uint32  satId;
        uint256 amount;
        uint16  assetId;
        uint256 txNonce;
        address wallet;
        Status  status;
    }

    // ── Constants ────────────────────────────────────────────────────

    uint16 public constant USDC_ASSET_ID = 1;
    uint8  public constant MAX_BATCH     = 10;

    // ── State ────────────────────────────────────────────────────────

    mapping(uint256 => Settlement) public settlements;

    // ── Events ───────────────────────────────────────────────────────

    /// @notice Emitted per stored intent. `satId`, `txNonce`, and
    ///         `wallet` are indexed so a block explorer can filter by
    ///         any combination — this is the TRL-4 audit hook.
    event SettlementCreated(
        uint32  indexed satId,
        uint256 amount,
        uint256 indexed txNonce,
        address indexed wallet
    );

    // ── Custom errors ────────────────────────────────────────────────

    error BatchEmpty();
    error BatchTooLarge(uint256 size);
    error InvalidAsset(uint16 assetId);
    error DuplicateNonce(uint256 txNonce);
    error EmptyWallet();

    // ── Entrypoint ───────────────────────────────────────────────────

    /// @notice Records 1..MAX_BATCH settlement intents, each keyed by
    ///         its txNonce. Reverts on empty batch, over-size batch,
    ///         non-USDC asset, duplicate nonce, or zero-address wallet.
    ///         All reverts are all-or-nothing — a single bad entry
    ///         aborts the whole batch (no partial writes).
    /// @param intents Calldata array of submission-shape intents.
    function settleBatch(SettlementIntent[] calldata intents) external {
        uint256 n = intents.length;
        if (n == 0) revert BatchEmpty();
        if (n > MAX_BATCH) revert BatchTooLarge(n);

        for (uint256 i = 0; i < n; ++i) {
            SettlementIntent calldata item = intents[i];

            if (item.assetId != USDC_ASSET_ID) revert InvalidAsset(item.assetId);
            if (item.wallet == address(0)) revert EmptyWallet();
            if (settlements[item.txNonce].status != Status.NONE) {
                revert DuplicateNonce(item.txNonce);
            }

            settlements[item.txNonce] = Settlement({
                satId:   item.satId,
                amount:  item.amount,
                assetId: item.assetId,
                txNonce: item.txNonce,
                wallet:  item.wallet,
                status:  Status.PENDING
            });

            emit SettlementCreated(item.satId, item.amount, item.txNonce, item.wallet);
        }
    }
}
