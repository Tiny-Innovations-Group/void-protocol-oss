# 🏛️ Compliance Roadmap & Standards Traceability

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
> 
> Authority: Tiny Innovation Group Ltd
> 
> License: Apache 2.0
>
> Status:Pre-Certification Compliance Map
> 
> Desc:      Traceability matrix for NSA/CISA & SEI CERT C++ Standards.


Void Protocol is engineered for high-assurance orbital environments. This document defines the engineering standards utilized to achieve "Clean C++" status and the roadmap toward formal third-party certification.

## ⚠️ Certification Status
**Current Phase:** Pre-Certification / Development.
The codebase is currently in **Self-Assessed Compliance**. It has been built to satisfy the technical requirements of the following standards but has not yet received a formal Certificate of Networthiness (CoN) or FIPS validation.

## 🛡️ SEI CERT C++ Enforcement Matrix

| Rule ID | Description | Implementation Proof |
| :--- | :--- | :--- |
| **MEM50-CPP** | Do not access freed memory | **Strict No-Heap Policy.** `malloc`, `new`, and `free` are globally banned. |
| **INT30-C** | Ensure unsigned integer operations do not wrap | **Explicit Casting.** Bitwise operations use `static_cast<tcflag_t>` and `static_cast<uint16_t>` to prevent wrapping/sign errors. |
| **MEM52-CPP** | Detect and handle memory allocation errors | **Static Allocation.** All buffers (Packet H, A, B, C, D) are pre-allocated at compile-time. |
| **MSC51-CPP** | Ensure sensitive data is zeroed | **RAII Cleanup.** `sodium_memzero` is called on all private ephemeral keys immediately after ECDH derivation. |

## 🔍 Static Analysis Proof
Compliance is mathematically enforced at build-time using the following toolchain:
1. **Clang-Tidy:** Enforces `cert-*` and `bugprone-*` check sets.
2. **Cppcheck:** Performs "Inconclusive" state analysis to detect potential memory hazards.
3. **Compiler Hardening:** `-Werror` is active. Any code violating sign-safety, alignment, or type-safety results in a fatal build error.

## 🚀 Roadmap to Certification
1. **[Q2 2026]** Completion of GTest-based Requirement Traceability Matrix.
2. **[Q3 2026]** Formal FIPS 140-3 validation of the `void-core` cryptographic wrapper.
3. **[Q4 2026]** Third-party penetration test and memory safety audit.