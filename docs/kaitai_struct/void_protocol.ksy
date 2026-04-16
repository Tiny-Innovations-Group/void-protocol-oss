meta:
  id: void_protocol
  title: "VOID Protocol v2.1 — Hardened Root Dispatcher"
  endian: le
  license: Apache-2.0
  imports:
    - ccsds_primary_header
    - snlp_header
    - tig_common_types

doc: |
  -------------------------------------------------------------------------
  VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd — HARDENED
  -------------------------------------------------------------------------
  Authority: Tiny Innovation Group Ltd
  License:   Apache-2.0
  Status:    Hardened / Audit-Remediated
  
  CHANGES FROM v2.0 (Pre-Audit):
    C-01 FIX: seq_count mask corrected to 0x3FFF (14-bit).
    F-01 FIX: SNLP header locked at 14B (Option B, VOID-113/114B).
              snlp_header.ksy align_pad = 4 bytes. See Protocol-spec-SNLP.md
              § 1-2 and VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md.
    F-02 FIX: Body dispatch uses CCSDS packet_data_length, not _io.size.
    F-03 FIX: dispatch_122 switches on `magic` byte at body offset 0
              (0xD0 = Packet D, 0xAC = Packet ACK). Replaces single-bit
              CCSDS Type-bit trust with an 8-bit Hamming-distance-4
              discriminant. Absorbed from existing pad bytes — zero
              impact on frame totals or downstream alignment.
    F-04 FIX: Packet ACK split into SNLP/CCSDS variants (fixed tunnel size).
    K-01 FIX: Modularized via imports.
    K-02 FIX: Enums defined in tig_common_types.
    K-03 FIX: valid constraints on CCSDS version, sync word, alignment pads.
    K-04 FIX: doc-ref converted to YAML list.

  OPEN ITEMS: none.

doc-ref:
  - "https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Acknowledgment-spec.md"
  - "https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Protocol-spec-CCSDS.md"
  - "https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Protocol-spec-SNLP.md"
  - "https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Receipt-spec.md"

seq:
  # 1. HEADER: Parse routing header based on sync word peek
  - id: routing_header
    type:
      switch-on: _root.is_snlp
      cases:
        true: snlp_header
        false: ccsds_primary_header

  # 2. BODY: Dispatch based on CCSDS-derived payload length
  #    The `size` constraint creates a bounded sub-stream,
  #    preventing overruns on corrupt or concatenated data.
  - id: body
    size: _root.body_length
    type:
      switch-on: _root.body_length
      cases:
        # --- DETERMINISTIC DISPATCH (Unique Sizes) ---
        # VOID-114B: dispatch keys reflect alignment pads (_pad_head,
        # _pre_crc, _pre_sat, _pre_sig, _tail_pad) added to bodies.
        66:  packet_a_body
        178: packet_b_body
        106: packet_h_body
        98:  packet_c_body
        34:  heartbeat_body

        # --- COLLISION ZONE (Resolved by Type Bit) ---
        122: dispatch_122

        # --- TIER-UNIQUE DISPATCH (CCSDS ACK) ---
        # body_length=114 is CCSDS ACK only (SNLP ACK is 122 and lands in
        # dispatch_122). Routes directly to the fixed-size CCSDS variant.
        # F-04 FIX: split from dynamic-sized packet_ack_body (Option B).
        114: packet_ack_body_ccsds

# ==========================================
# COMPUTED INSTANCES
# ==========================================
instances:
  magic_peek:
    pos: 0
    type: u4be
    doc: "Peek first 4 bytes without consuming. Used for header selection."

  is_snlp:
    value: magic_peek == 0x1D01A5A5
    doc: "true = SNLP (Community), false = CCSDS (Enterprise)."

  # HARDENED: Derive body length from CCSDS packet_data_length field.
  # This replaces the fragile _io.size dependency (F-02 fix).
  #
  # CCSDS packet_data_length = (octets in data field) - 1
  # data_field_length       = packet_data_length + 1 (computed in header)
  #
  # SNLP SPEC OVERRIDE (Protocol-spec-SNLP.md § 2, offsets 08-09):
  # Inside an SNLP frame, the CCSDS `length` field reports the body
  # length ONLY — it EXCLUDES the entire 14-byte SNLP header, including
  # the 4-byte align_pad. This differs from the pure CCSDS convention
  # (where packet_data_length covers everything after the 6-byte primary
  # header). The override is required to keep body sizes identical
  # across tiers and is enforced by the packet generator in
  # gateway/test/utils/generate_packets.go, which writes
  # uint16(body_len - 1) regardless of tier.
  #
  # Result: body_length = data_field_length in BOTH tiers. No tier-
  # specific subtraction needed.
  body_length:
    value: >-
      is_snlp
      ? (routing_header.as<snlp_header>.ccsds.data_field_length)
      : (routing_header.as<ccsds_primary_header>.data_field_length)
    doc: |
      Payload body length in octets. Derived from the CCSDS
      packet_data_length field (SNLP override applied — the length
      field excludes the full 14B SNLP header). Stream-safe: does
      not depend on _io.size.

  global_packet_type:
    value: >-
      is_snlp
      ? (routing_header.as<snlp_header>.ccsds.packet_type)
      : (routing_header.as<ccsds_primary_header>.packet_type)
    doc: "0=Telemetry, 1=Telecommand. Sourced from CCSDS header."

  global_apid:
    value: >-
      is_snlp
      ? (routing_header.as<snlp_header>.ccsds.apid)
      : (routing_header.as<ccsds_primary_header>.apid)
    doc: "11-bit APID. Local routing only; global ID is sat_id in payload."

# ==========================================
# DISPATCHERS
# ==========================================
types:
  dispatch_122:
    doc: |
      Collision resolver for 122-byte bodies.
      Packet D (Delivery) vs Packet ACK SNLP (Command).

      TIER CONSTRAINT: body_length=122 is reachable by CCSDS Packet D
      (128 − 6 hdr), SNLP Packet D (136 − 14 hdr), and SNLP ACK
      (136 − 14 hdr). CCSDS ACK body is 114 bytes and is dispatched
      directly at the root (F-04 fix, Option B), so the ACK branch
      here is SNLP-only by construction.

      F-03 FIX (VOID-006, 2026-04-15): dispatch now switches on the
      `magic` byte at body offset 0, NOT on the CCSDS Type bit. The
      Type bit is a single point of failure (one bit-flip = wrong
      parse path). The magic byte is an 8-bit discriminant with
      Hamming distance 4 between 0xD0 and 0xAC — a single RF bit
      error cannot transform one into the other. An unknown magic
      value is a hard parse failure and MUST bounce at the gateway
      ingest boundary before any body-specific interpretation.
    seq:
      - id: content
        type:
          switch-on: magic_peek
          cases:
            0xD0: packet_d_body
            0xAC: packet_ack_body_snlp
    instances:
      magic_peek:
        pos: 0
        type: u1
        doc: "Peek body offset 0 without consuming. Selects body type."

  # ==========================================
  # PAYLOAD BODIES (Little-Endian)
  # ==========================================

  # --- PACKET A: Invoice (66 Bytes, VOID-114B) ---
  packet_a_body:
    doc: |
      The Invoice. Broadcast by Sat A (Seller).
      Unsigned public offer of service. 66 bytes payload.

      VOID-114B: _pad_head and _pre_crc added so epoch_ts lands on
      a natural 8-byte boundary and crc32 on a 4-byte boundary
      under both 6B CCSDS and 14B SNLP headers.
    seq:
      - id: pad_head
        size: 2
        doc: "Cycle alignment pad (VOID-114B). Pushes epoch_ts to 8-aligned."
      - id: epoch_ts
        type: u8
        doc: "Unix timestamp. Replay protection."
      - id: pos_vec
        type: tig_common_types::vector_3d
        doc: "Sat A ECEF position."
      - id: vel_vec
        type: tig_common_types::vector_3f
        doc: "Sat A velocity vector."
      - id: sat_id
        type: u4
        doc: "32-bit Seller Identifier."
      - id: amount
        type: u8
        doc: "Cost in lowest denomination (e.g., Satoshi)."
      - id: asset_id
        type: u2
        enum: tig_common_types::asset_id
        doc: "Currency Type ID. MUST be a verified stablecoin."
      - id: pre_crc
        size: 2
        doc: "CRC alignment pad (VOID-114B). Pushes crc32 to 4-aligned."
      - id: crc32
        type: u4
        doc: "Invoice integrity checksum."

  # --- PACKET B: Payment (178 Bytes, VOID-110 + VOID-114B) ---
  packet_b_body:
    doc: |
      The Encrypted Payment. Sent by Sat B (Mule) to Ground Station.
      178 bytes payload (174 data + 4-byte _tail_pad).

      Encryption Logic:
        Enterprise (CCSDS): enc_payload is ChaCha20-Poly1305 ciphertext.
        Community (SNLP):   enc_payload is PLAINTEXT (amateur radio compliance).

      VOID-110: ChaCha20 nonce is derived as sat_id[4] || epoch_ts[8]
      (12 bytes). It is NEVER transmitted on the wire. The former
      `nonce` field is now `_pre_sig` (alignment pad).

      VOID-114B: _pad_head, _pre_sat, _pre_sig, and _tail_pad added
      so every critical field (epoch_ts, sat_id, signature, global_crc)
      lands on its natural alignment boundary under both headers.
      Signature covers header + body[0..105] (106 body bytes).
    seq:
      - id: pad_head
        size: 2
        doc: "Cycle alignment pad (VOID-114B). Pushes epoch_ts to 8-aligned."
      - id: epoch_ts
        type: u8
        doc: "Sat B timestamp. Used for nonce derivation (VOID-110)."
      - id: pos_vec
        type: tig_common_types::vector_3d
        doc: "Sat B ECEF position. Cleartext."
      - id: enc_payload
        size: 62
        doc: |
          Inner Invoice (62 bytes).
          Plaintext if SNLP, ChaCha20 ciphertext if CCSDS.
      - id: pre_sat
        size: 2
        doc: "sat_id alignment pad (VOID-114B). Pushes sat_id to 4-aligned."
      - id: sat_id
        type: u4
        doc: "Sat B ID (Mule). 32-bit global identity."
      - id: pre_sig
        size: 4
        doc: "Signature alignment pad (VOID-114B). Pushes signature to 8-aligned."
      - id: signature
        type: tig_common_types::ed25519_signature
        doc: "Ed25519 PUF Signature over header + body[0..105]."
      - id: global_crc
        type: u4
        doc: "Outer packet integrity checksum."
      - id: tail_pad
        size: 4
        doc: "Frame tail pad (VOID-114B). CCSDS total 184 (÷8), SNLP total 192 (÷64 cache line)."

  # --- PACKET H: Handshake (106 Bytes) ---
  packet_h_body:
    doc: |
      Ephemeral Key Exchange. Bidirectional.
      MUST complete before any Packet B or ACK is transmitted.
    seq:
      - id: session_ttl
        type: u2
        doc: "Session duration in seconds."
      - id: timestamp
        type: u8
        doc: "Session start time (Unix epoch)."
      - id: eph_pub_key
        type: tig_common_types::x25519_public_key
        doc: "X25519 ephemeral public key (32 bytes)."
      - id: signature
        type: tig_common_types::ed25519_signature
        doc: "Ed25519 identity signature over offsets 00-47."

  # --- PACKET C: Receipt (98 Bytes) ---
  packet_c_body:
    doc: |
      The Receipt (Proof of Execution).
      Generated by Sat A after successful service dispensation.
    seq:
      - id: pad_head
        size: 2
        doc: "Cycle alignment pad."
      - id: exec_time
        type: u8
        doc: "Execution timestamp."
      - id: enc_tx_id
        type: u8
        doc: "Encrypted Transaction ID. Matches Tunnel BLOCK_NONCE."
      - id: enc_status
        type: u1
        doc: "Encrypted status byte. 0x01=Success after decryption."
      - id: pad_sig
        size: 7
        doc: "Signature alignment pad (pushes signature to 8-byte boundary)."
      - id: signature
        type: tig_common_types::ed25519_signature
        doc: "Sat A PUF signature over offsets 00-31."
      - id: crc32
        type: u4
        doc: "Receipt integrity checksum."
      - id: tail_pad
        size: 4
        doc: "Final alignment to 98 bytes."

  # --- PACKET D: Delivery (122 Bytes, Type=Telemetry) ---
  packet_d_body:
    doc: |
      The Delivery. Transport wrapper used by Sat B to downlink
      Packet C to the Ground Station. Wraps the receipt in a new
      CCSDS/SNLP frame for radio transmission.

      F-03 FIX (VOID-006, 2026-04-15): `magic` at body offset 0 is
      a packet-type discriminant that protects dispatch_122 against
      single-bit-flip collisions with Packet ACK SNLP. Value 0xD0
      identifies this as Delivery. The byte is absorbed from the
      former 2-byte pad_head (now 1 byte), so body total, frame
      total, and every subsequent field offset remain unchanged
      (downlink_ts still lands on the natural 64-bit boundary).
    seq:
      - id: magic
        contents: [0xD0]
        doc: "Packet-D discriminant (F-03 fix). MUST be 0xD0."
      - id: pad_head
        size: 1
        doc: "Cycle alignment pad (1 byte after magic absorption)."
      - id: downlink_ts
        type: u8
        doc: "Downlink timestamp."
      - id: sat_b_id
        type: u4
        doc: "Carrier ID (Sat B / Mule)."
      - id: payload
        size: 98
        doc: "Stripped Packet C (CCSDS header removed)."
      - id: global_crc
        type: u4
        doc: "Outer integrity checksum."
      - id: tail
        size: 6
        doc: "Final alignment to 122 bytes."

  # --- PACKET ACK (CCSDS): Ground Station Command, Enterprise Tier ---
  packet_ack_body_ccsds:
    doc: |
      Ground Station Acknowledgement / Unlock Command — CCSDS tier.

      Total body: 114 bytes (frame 120 = 6B CCSDS header + 114B body).
      Contains Relay Instructions and the encrypted unlock key.
      enc_tunnel is fixed at 88 bytes (compact, standard CCSDS header).

      F-04 FIX (Option B): split from the former polymorphic packet_ack_body
      so every field has a compile-time offset. Statically analyzable.

      F-03 FIX (VOID-006, 2026-04-15): `magic` at body offset 0 is a
      packet-type discriminant. Value 0xAC identifies ACK. Absorbed
      from the former 2-byte pad_a (now 1 byte). Body total, frame
      total, and all downstream field offsets unchanged.
    seq:
      - id: magic
        contents: [0xAC]
        doc: "Packet-ACK discriminant (F-03 fix). MUST be 0xAC."
      - id: pad_a
        size: 1
        doc: "32/64-bit alignment pad (1 byte after magic absorption)."
      - id: target_tx_id
        type: u4
        doc: "Cleartext transaction nonce being acknowledged."
      - id: status
        type: u1
        enum: tig_common_types::settlement_status
        doc: "0x01=Settled, 0xFF=Rejected."
      - id: pad_b
        size: 1
        doc: "Data boundary pad."
      - id: relay_ops
        type: tig_common_types::relay_ops
        doc: "12-byte Sat B relay instructions."
      - id: enc_tunnel
        size: 88
        doc: |
          ChaCha20 encrypted tunnel data. Fixed 88B (CCSDS tier).
          Decrypted payload is TunnelData_t (see Acknowledgment-spec.md § C).
      - id: pad_c
        size: 2
        doc: "Alignment to CRC boundary."
      - id: crc32
        type: u4
        doc: "Outer checksum."

  # --- PACKET ACK (SNLP): Ground Station Command, Community Tier ---
  packet_ack_body_snlp:
    doc: |
      Ground Station Acknowledgement / Unlock Command — SNLP tier.

      Total body: 122 bytes (frame 136 = 14B SNLP header + 122B body).
      Contains Relay Instructions and the encrypted unlock key.
      enc_tunnel is fixed at 96 bytes (padded for SNLP internal header).

      F-04 FIX (Option B): split from the former polymorphic packet_ack_body
      so every field has a compile-time offset. Statically analyzable.

      F-03 FIX (VOID-006, 2026-04-15): `magic` at body offset 0 is a
      packet-type discriminant. Value 0xAC identifies ACK and protects
      dispatch_122 against single-bit-flip collisions with Packet D
      (which shares body_length=122 in this tier). Absorbed from the
      former 2-byte pad_a (now 1 byte). Body total, frame total, and
      all downstream field offsets unchanged.
    seq:
      - id: magic
        contents: [0xAC]
        doc: "Packet-ACK discriminant (F-03 fix). MUST be 0xAC."
      - id: pad_a
        size: 1
        doc: "32/64-bit alignment pad (1 byte after magic absorption)."
      - id: target_tx_id
        type: u4
        doc: "Cleartext transaction nonce being acknowledged."
      - id: status
        type: u1
        enum: tig_common_types::settlement_status
        doc: "0x01=Settled, 0xFF=Rejected."
      - id: pad_b
        size: 1
        doc: "Data boundary pad."
      - id: relay_ops
        type: tig_common_types::relay_ops
        doc: "12-byte Sat B relay instructions."
      - id: enc_tunnel
        size: 96
        doc: |
          ChaCha20 encrypted tunnel data. Fixed 96B (SNLP tier).
          Decrypted payload is TunnelData_t (see Acknowledgment-spec.md § C).
      - id: pad_c
        size: 2
        doc: "Alignment to CRC boundary."
      - id: crc32
        type: u4
        doc: "Outer checksum."

  # --- HEARTBEAT (34 Bytes, VOID-114B) ---
  heartbeat_body:
    doc: |
      System Heartbeat. Health, status, and GPS telemetry.
      Transmitted periodically for ground station monitoring.
      34 bytes payload.

      VOID-114B: field order matches C++ HeartbeatPacket_t in
      void_packets_{ccsds,snlp}.h and Go generator genPacketL.
      _pad_head added so epoch_ts lands on 8-byte boundary.
      The legacy reserved[2] field is dropped (never used);
      _pad_head is the forward-compat slot.
    seq:
      - id: pad_head
        size: 2
        doc: "Cycle alignment pad (VOID-114B). Pushes epoch_ts to 8-aligned."
      - id: epoch_ts
        type: u8
        doc: "Unix timestamp."
      - id: pressure_pa
        type: u4
        doc: "Barometric pressure in Pascals."
      - id: lat_fixed
        type: s4
        doc: "Latitude * 10^7 (e.g., 515000000 = 51.5° N). Signed: negative = South."
      - id: lon_fixed
        type: s4
        doc: "Longitude * 10^7 (e.g., -128000 = -0.0128° W). Signed: negative = West."
      - id: vbatt_mv
        type: u2
        doc: "Battery voltage in millivolts."
      - id: temp_c
        type: s2
        doc: "Internal temp in centidegrees (2300 = 23.00°C)."
      - id: gps_speed_cms
        type: u2
        doc: "Ground speed in cm/s (600 = 6.0 m/s)."
      - id: sys_state
        type: u1
        enum: tig_common_types::sys_state
        doc: "State machine ID."
      - id: sat_lock
        type: u1
        doc: "GPS satellite count."
      - id: crc32
        type: u4
        doc: "Heartbeat integrity checksum."
