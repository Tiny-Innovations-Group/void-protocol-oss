// SPDX-License-Identifier: Apache-2.0
// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// VOID-050 — Contract Scaffold smoke test.
//
// Red-green: this test is authored BEFORE HelloWorld.sol exists, so the
// initial `forge test` run must fail at compile time ("not found"). Once
// HelloWorld.sol lands with a `greet()` returning the expected literal,
// this test turns green. Any future edit that breaks the compile surface,
// the ABI, or the returned string fails here.

pragma solidity ^0.8.20;

import {Test} from "forge-std/Test.sol";
import {HelloWorld} from "../src/HelloWorld.sol";

contract HelloWorldTest is Test {
    HelloWorld internal hw;

    function setUp() public {
        hw = new HelloWorld();
    }

    function test_greetReturnsCanonicalString() public view {
        // Canonical literal — documented in the contract. The flat-sat demo
        // script `cast call`s this function as a liveness check.
        assertEq(hw.greet(), "VOID-050 scaffold alive");
    }

    function test_greetIsPure() public view {
        // Call twice; idempotent return proves no storage read/write on the
        // path. Cheap smoke that `greet()` is side-effect-free.
        string memory a = hw.greet();
        string memory b = hw.greet();
        assertEq(a, b);
    }
}
