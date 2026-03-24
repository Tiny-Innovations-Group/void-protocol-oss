meta:
  id: snlp_header
  title: "SNLP Frame Header (Community Tier) — Hardened"
  endian: be
  license: Apache-2.0
  imports:
    - ccsds_primary_header

doc: |
  Space Network Layer Protocol (SNLP) Header — 12 Bytes
  
  Designed for asynchronous, low-bandwidth LoRa links on ISM bands
  (433/868/915 MHz). Structure:
  
    Offset 00-03: Sync Word (0x1D01A5A5) — Frame detection in noise
    Offset 04-09: CCSDS Primary Header (6 bytes, Big-Endian)
    Offset 10-11: Alignment Pad (2 bytes, 0x0000)
  
  DESIGN NOTE (Reconciliation):
  The original KSY used a 4-byte pad (14B total header) to absorb
  SNLP tail padding and normalize body sizes across transport tiers.
  
  This hardened version restores the 2-byte pad per the SNLP spec
  document (Section 2). Packet bodies that require trailing padding
  on SNLP must include it explicitly via conditional tail fields.
  
  If the project decides on Option B (inflate header to normalize),
  change align_pad to size: 4 and update payload_len accordingly.

seq:
  - id: sync_word
    contents: [0x1D, 0x01, 0xA5, 0xA5]
    doc: "32-bit Community Sync Word. Hardcoded for frame detection."

  - id: ccsds
    type: ccsds_primary_header
    doc: "Standard 6-byte CCSDS 133.0-B-2 Primary Header."

  - id: align_pad
    type: u2
    valid:
      eq: 0
    doc: |
      16-bit alignment buffer. MUST be 0x0000.
      Matches Protocol-spec-SNLP.md Section 2, offsets 10-11.
