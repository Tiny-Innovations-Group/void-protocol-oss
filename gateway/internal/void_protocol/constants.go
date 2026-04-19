package void_protocol

// VOID Protocol wire-format invariants. These mirror the canonical values
// in docs/Protocol-spec-SNLP.md / docs/Protocol-spec-CCSDS.md and the C++
// side at void-core/include/void_packets*.h. The constants exist here so
// the Go gateway, its test suite, and the deterministic vector generator
// all consume the same source of truth — no magic numbers in handlers,
// tests, or packet builders.
//
// Any change here invalidates the checked-in golden vectors under
// test/vectors/ and MUST ship with a regenerated vector set + updated
// C++ / Go regression suites in the same PR.

const (
	// CCSDSHeaderLen — CCSDS Primary Header, 6 bytes (VOID-113, CCSDS 133.0-B-2).
	CCSDSHeaderLen = 6

	// SNLPHeaderLen — SNLP tier header, 14 bytes: 4-byte sync word
	// (0x1D01A5A5) + 6-byte CCSDS header + 4-byte align_pad (VOID-113 /
	// VOID-114B — see docs/VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md).
	SNLPHeaderLen = 14

	// PacketBBodyLen — Packet B body length (VOID-112, VOID-114B).
	// Ingest handlers MUST reject any buffer shorter than
	// headerLen + PacketBBodyLen with HTTP 400 BEFORE any struct cast.
	PacketBBodyLen = 178

	// PacketBSigScopeBody — body bytes covered by the Ed25519 signature
	// (VOID-111). The full signed region is `header + body[0..105]`,
	// byte-identical across CCSDS and SNLP tiers. Accessed in C++ via
	// offsetof(PacketB_t, signature); in Go the scope length is a
	// wire-format constant.
	PacketBSigScopeBody = 106
)
