// SPDX-License-Identifier: Apache-2.0
// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// VOID-051 — Minimal Escrow contract test suite.
//
// Red-green: written BEFORE Escrow.sol exists. The initial `forge test`
// run fails to compile ("Escrow.sol not found") — proving the suite
// actually exercises the missing contract. Once Escrow.sol lands, every
// assertion below turns green.
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

    // Event signature from the contract — duplicated locally so expectEmit
    // can match on it without cross-importing from the implementation.
    event SettlementCreated(uint32 indexed satId, uint256 amount, uint256 indexed txNonce);

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
            txNonce: NONCE
        });

        // Expect the event BEFORE the call (Foundry pattern).
        vm.expectEmit(true, true, false, true, address(escrow));
        emit SettlementCreated(SAT_ID, AMOUNT, NONCE);

        escrow.settleBatch(intents);

        (uint32 satId, uint256 amount, uint16 assetId, uint256 txNonce, Escrow.Status status)
            = escrow.settlements(NONCE);
        assertEq(satId, SAT_ID, "satId");
        assertEq(amount, AMOUNT, "amount");
        assertEq(assetId, USDC, "assetId");
        assertEq(txNonce, NONCE, "txNonce");
        assertEq(uint8(status), uint8(Escrow.Status.PENDING), "status");
    }

    function test_batchOfTenAllStoredAndEmitted() public {
        Escrow.SettlementIntent[] memory intents = new Escrow.SettlementIntent[](10);
        for (uint256 i = 0; i < 10; i++) {
            intents[i] = Escrow.SettlementIntent({
                satId:   SAT_ID,
                amount:  AMOUNT + i,
                assetId: USDC,
                txNonce: NONCE + i
            });
        }

        // Expect an event for each entry, in order.
        for (uint256 i = 0; i < 10; i++) {
            vm.expectEmit(true, true, false, true, address(escrow));
            emit SettlementCreated(SAT_ID, AMOUNT + i, NONCE + i);
        }

        escrow.settleBatch(intents);

        for (uint256 i = 0; i < 10; i++) {
            (, uint256 amt, , , Escrow.Status status) = escrow.settlements(NONCE + i);
            assertEq(amt, AMOUNT + i, "amount[i]");
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
                txNonce: NONCE + i
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
            txNonce: NONCE
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
            txNonce: NONCE
        });
        escrow.settleBatch(first);

        Escrow.SettlementIntent[] memory second = new Escrow.SettlementIntent[](1);
        second[0] = Escrow.SettlementIntent({
            satId:   SAT_ID,
            amount:  AMOUNT + 1,     // different amount, same nonce
            assetId: USDC,
            txNonce: NONCE
        });
        vm.expectRevert(abi.encodeWithSelector(Escrow.DuplicateNonce.selector, NONCE));
        escrow.settleBatch(second);
    }
}
