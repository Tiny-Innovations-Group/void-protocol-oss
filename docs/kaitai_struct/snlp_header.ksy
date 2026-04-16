meta:
  id: snlp_header
  title: "SNLP Frame Header (Community Tier) — Hardened"
  endian: be
  license: Apache-2.0
  imports:
    - ccsds_primary_header

doc: |
  Space Network Layer Protocol (SNLP) Header — 14 Bytes

  Designed for asynchronous, low-bandwidth LoRa links on ISM bands
  (433/868/915 MHz). Structure:

    Offset 00-03: Sync Word (0x1D01A5A5) — Frame detection in noise
    Offset 04-09: CCSDS Primary Header (6 bytes, Big-Endian)
    Offset 10-13: Alignment Pad (4 bytes, 0x00000000)

  F-01 RESOLVED (VOID-113/114B, 2026-04-15):
  Header is 14 bytes (Option B). The 4-byte align_pad is required for
  32/64-bit cycle optimization and mod-8 congruence with the 6-byte
  CCSDS header — the only header size that lets both tiers share a
  single body-layout contract while preserving 64-bit alignment of
  body fields. See:
    - docs/Protocol-spec-SNLP.md § 1-2
    - docs/VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md
    - docs/VOID_114B_BODY_ALIGNMENT_2026-04-14.md
    - void-core/include/void_packets.h → SIZE_SNLP_HEADER = 14

  LENGTH FIELD SEMANTICS (spec override of CCSDS convention):
  The `packet_len` field inside the embedded CCSDS portion (SNLP
  offsets 08-09) reports the body length ONLY — it EXCLUDES the
  entire 14-byte SNLP header, including the align_pad. This differs
  from the pure CCSDS convention (where packet_data_length covers
  everything after the 6-byte primary header). The override is
  required by Protocol-spec-SNLP.md § 2 and enforced by
  gateway/test/utils/generate_packets.go::buildHeader which writes
  `uint16(body_len - 1)` regardless of tier.

seq:
  - id: sync_word
    contents: [0x1D, 0x01, 0xA5, 0xA5]
    doc: "32-bit Community Sync Word. Hardcoded for frame detection."

  - id: ccsds
    type: ccsds_primary_header
    doc: "Standard 6-byte CCSDS 133.0-B-2 Primary Header."

  - id: align_pad
    type: u4
    valid:
      eq: 0
    doc: |
      32-bit alignment buffer. MUST be 0x00000000.
      Matches Protocol-spec-SNLP.md Section 2, offsets 10-13.
      Required for mod-8 congruence with the 6-byte CCSDS header.
