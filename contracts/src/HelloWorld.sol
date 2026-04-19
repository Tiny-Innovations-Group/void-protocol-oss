// SPDX-License-Identifier: Apache-2.0
// 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
// VOID-050 — Contract Scaffold.
//
// Minimal smoke contract proving the Foundry + Anvil toolchain is wired up
// for the void-protocol settlement work that lands in VOID-051 / VOID-052.
// This contract intentionally does nothing else — it is the "hello world"
// required by the VOID-050 acceptance criteria. The canonical return
// string is asserted by `test/HelloWorld.t.sol` and consumed by the flat-
// sat demo script as a liveness check.

pragma solidity ^0.8.20;

contract HelloWorld {
    string private constant CANONICAL = "VOID-050 scaffold alive";

    function greet() external pure returns (string memory) {
        return CANONICAL;
    }
}
