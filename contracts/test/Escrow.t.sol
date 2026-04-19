// SPDX-License-Identifier: Apache-2.0
// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// VOID-051 — Minimal Escrow contract test suite.
//
// VOID-051 amendment (Journey Change Log v6, 2026-04-19): `address wallet`
// added to SettlementIntent + Settlement + SettlementCreated event so the
// on-chain record is self-contained (a block explorer view of the
// settleBatch tx tells the whole payment story without consulting the
// gateway's receipts.json). Zero-address reject enforced via EmptyWallet().
//
// Red-green: written BEFORE Escrow.sol exists. The initial `forge test`
// run fails to compile ("Escrow.sol not found"). Once Escrow.sol lands,
// every assertion below turns green.
//
// Flat-sat scope (journey_to_hab_plain_text.md #6): single-transaction
// settleBatch on local Anvil only. Testnet deploy is Phase B (#21).

pragma solidity ^0.8.20;

import {Test} from "forge-std/Test.sol";
import {Escrow} from "../src/Escrow.sol";

contract EscrowTest is Test {
    Escrow internal escrow;

    // Canonical test fixture — values mirror the deterministic golden-vector
    // constants in gateway/test/utils/generate_packets.go so a Go integration
    // test (lands with VOID-052 / #14) can reuse this nonce as its settlement
    // intent without re-deriving.
    uint32  internal constant SAT_ID   = 0xCAFEBABE;
    uint256 internal constant AMOUNT   = 420_000_000;
    uint16  internal constant USDC     = 1;
    uint16  internal constant NON_USDC = 2;
    uint256 internal constant NONCE    = 0x1710000100000CAFEBABE;

    // Anvil default account #1 — used as the deterministic seller wallet
    // for sat_id 0xCAFEBABE in the flat-sat registry mapping. Keeping it
    // in one place here so the Go side's registry.MockDB test fixture and
    // this Foundry suite stay in lock-step.
    address internal constant WALLET     = 0x70997970C51812dc3A010C7d01b50e0d17dc79C8;
    address internal constant ALT_WALLET = 0x3C44CdDdB6a900fa2b585dd299e03d12FA4293BC;

    // Event signature (must match Escrow.sol). The Journey Change Log v6
    // amendment added `address indexed wallet` to the event.
    event SettlementCreated(
        uint32  indexed satId,
        uint256 amount,
        uint256 indexed txNonce,
        address indexed wallet
    );

    function setUp() public {
        escrow = new Escrow();
    }

    // ---------- Happy paths ----------

    function test_singleIntentStoresPendingAndEmits() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](1);
        intents[0] = Escrow.SettlementIntent({
            satId:   SAT_ID,
            amount:  AMOUNT,
            assetId: USDC,
            txNonce: NONCE,
            wallet:  WALLET
        });

        // Expect the event BEFORE the call (Foundry pattern).
        // All three indexed topics + unindexed data must match.
        vm.expectEmit(true, true, true, true, address(escrow));
        emit SettlementCreated(SAT_ID, AMOUNT, NONCE, WALLET);

        escrow.settleBatch(intents);

        (
            uint32 satId,
            uint256 amount,
            uint16 assetId,
            uint256 txNonce,
            address wallet,
            Escrow.Status status
        ) = escrow.settlements(NONCE);
        assertEq(satId, SAT_ID, "satId");
        assertEq(amount, AMOUNT, "amount");
        assertEq(assetId, USDC, "assetId");
        assertEq(txNonce, NONCE, "txNonce");
        assertEq(wallet, WALLET, "wallet");
        assertEq(uint8(status), uint8(Escrow.Status.PENDING), "status");
    }

    function test_batchOfTenAllStoredAndEmitted() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](10);
        for (uint256 i = 0; i < 10; i++) {
            // Alternate wallets across the batch to prove the field is
            // stored per-entry and not flattened from the first intent.
            address w = (i % 2 == 0) ? WALLET : ALT_WALLET;
            intents[i] = Escrow.SettlementIntent({
                satId:   SAT_ID,
                amount:  AMOUNT + i,
                assetId: USDC,
                txNonce: NONCE + i,
                wallet:  w
            });
        }

        for (uint256 i = 0; i < 10; i++) {
            address w = (i % 2 == 0) ? WALLET : ALT_WALLET;
            vm.expectEmit(true, true, true, true, address(escrow));
            emit SettlementCreated(SAT_ID, AMOUNT + i, NONCE + i, w);
        }

        escrow.settleBatch(intents);

        for (uint256 i = 0; i < 10; i++) {
            address expected = (i % 2 == 0) ? WALLET : ALT_WALLET;
            (, uint256 amt, , , address wallet, Escrow.Status status)
                = escrow.settlements(NONCE + i);
            assertEq(amt, AMOUNT + i, "amount[i]");
            assertEq(wallet, expected, "wallet[i]");
            assertEq(uint8(status), uint8(Escrow.Status.PENDING), "status[i]");
        }
    }

    // ---------- Rejection paths ----------

    function test_rejectsEmptyBatch() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](0);
        vm.expectRevert(Escrow.BatchEmpty.selector);
        escrow.settleBatch(intents);
    }

    function test_rejectsBatchOverTen() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](11);
        for (uint256 i = 0; i < 11; i++) {
            intents[i] = Escrow.SettlementIntent({
                satId:   SAT_ID,
                amount:  AMOUNT,
                assetId: USDC,
                txNonce: NONCE + i,
                wallet:  WALLET
            });
        }
        vm.expectRevert(abi.encodeWithSelector(Escrow.BatchTooLarge.selector, uint256(11)));
        escrow.settleBatch(intents);
    }

    function test_rejectsNonUSDCAsset() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](1);
        intents[0] = Escrow.SettlementIntent({
            satId:   SAT_ID,
            amount:  AMOUNT,
            assetId: NON_USDC,
            txNonce: NONCE,
            wallet:  WALLET
        });
        vm.expectRevert(abi.encodeWithSelector(Escrow.InvalidAsset.selector, NON_USDC));
        escrow.settleBatch(intents);
    }

    function test_rejectsDuplicateNonce() public {
        Escrow.SettlementIntent[] memory first = new Escrow.SettlementIntent[](1);
        first[0] = Escrow.SettlementIntent({
            satId:   SAT_ID,
            amount:  AMOUNT,
            assetId: USDC,
            txNonce: NONCE,
            wallet:  WALLET
        });
        escrow.settleBatch(first);

        Escrow.SettlementIntent[] memory second = new Escrow.SettlementIntent[](1);
        second[0] = Escrow.SettlementIntent({
            satId:   SAT_ID,
            amount:  AMOUNT + 1,    // different amount, same nonce
            assetId: USDC,
            txNonce: NONCE,
            wallet:  WALLET
        });
        vm.expectRevert(abi.encodeWithSelector(Escrow.DuplicateNonce.selector, NONCE));
        escrow.settleBatch(second);
    }

    // VOID-051 amendment (Change Log v6): a settlement with wallet=0x0
    // is meaningless — no recipient for future fund release. Reject
    // with a typed error so the gateway's off-chain registry lookup
    // can never silently submit an empty recipient.
    function test_rejectsZeroAddressWallet() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](1);
        intents[0] = Escrow.SettlementIntent({
            satId:   SAT_ID,
            amount:  AMOUNT,
            assetId: USDC,
            txNonce: NONCE,
            wallet:  address(0)
        });
        vm.expectRevert(Escrow.EmptyWallet.selector);
        escrow.settleBatch(intents);
    }
}
