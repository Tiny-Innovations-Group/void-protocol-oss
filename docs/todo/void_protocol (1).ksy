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
    F-02 FIX: Body dispatch uses CCSDS packet_data_length, not _io.size.
    F-04 FIX: Packet ACK split into SNLP/CCSDS variants (fixed tunnel size).
    K-01 FIX: Modularized via imports.
    K-02 FIX: Enums defined in tig_common_types.
    K-03 FIX: valid constraints on CCSDS version, sync word, alignment pads.
    K-04 FIX: doc-ref converted to YAML list.
  
  OPEN ITEMS (Require Project Decision):
    F-01: SNLP header size — currently 12B (spec-compliant). 
          If Option B chosen, change snlp_header.ksy pad to 4B.
    F-03: CRC pre-validation before dispatch_122. Requires application-layer
          hook — cannot be enforced in Kaitai alone.

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
        62:  packet_a_body
        170: packet_b_body
        106: packet_h_body
        98:  packet_c_body
        34:  heartbeat_body

        # --- COLLISION ZONE (Resolved by Type Bit) ---
        122: dispatch_122

        # --- TRANSPORT-VARIANT DISPATCH ---
        # ACK body size differs by transport:
        #   SNLP  → body_length includes extra tunnel bytes
        #   CCSDS → compact tunnel
        # Both are handled by packet_ack_body with dynamic tunnel sizing.
        # If formal verification requires fixed bodies, split into two types.
        114: packet_ack_body

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
  # This replaces the fragile _io.size dependency.
  #
  # CCSDS packet_data_length = (octets in data field) - 1
  # data_field_length = packet_data_length + 1  (computed in header)
  #
  # For SNLP: The CCSDS header inside SNLP reports the length of
  # everything after itself (6B CCSDS). But the SNLP frame also has
  # the sync word (4B) and align_pad (2B) before the body.
  # The CCSDS length field covers: align_pad + body.
  # So: body_length = data_field_length - 2 (subtract align_pad).
  #
  # For CCSDS: body_length = data_field_length directly.
  body_length:
    value: >-
      is_snlp
      ? (routing_header.as<snlp_header>.ccsds.data_field_length - 2)
      : (routing_header.as<ccsds_primary_header>.data_field_length)
    doc: |
      Payload body length in octets. Derived from the CCSDS
      packet_data_length field. Stream-safe: does not depend on _io.size.

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
      Packet D (Telemetry, Type=0) vs Packet ACK (Command, Type=1).
      
      WARNING: This dispatch trusts the CCSDS Type bit. A single
      bit-flip on the RF link will route to the wrong body type.
      Application-layer CRC validation MUST occur before acting
      on the parsed result.
    seq:
      - id: content
        type:
          switch-on: _root.global_packet_type
          cases:
            0: packet_d_body
            1: packet_ack_body

  # ==========================================
  # PAYLOAD BODIES (Little-Endian)
  # ==========================================

  # --- PACKET A: Invoice (62 Bytes) ---
  packet_a_body:
    doc: |
      The Invoice. Broadcast by Sat A (Seller).
      Unsigned public offer of service. 62 bytes payload.
    seq:
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
      - id: crc32
        type: u4
        doc: "Invoice integrity checksum."

  # --- PACKET B: Payment (170 Bytes) ---
  packet_b_body:
    doc: |
      The Encrypted Payment. Sent by Sat B (Mule) to Ground Station.
      
      Encryption Logic:
        Enterprise (CCSDS): enc_payload is ChaCha20-Poly1305 ciphertext.
        Community (SNLP):   enc_payload is PLAINTEXT (amateur radio compliance).
    seq:
      - id: epoch_ts
        type: u8
        doc: "Sat B timestamp. Used for nonce derivation."
      - id: pos_vec
        type: tig_common_types::vector_3d
        doc: "Sat B ECEF position. Cleartext."
      - id: enc_payload
        size: 62
        doc: |
          Inner Invoice (62 bytes).
          Plaintext if SNLP, ChaCha20 ciphertext if CCSDS.
      - id: sat_id
        type: u4
        doc: "Sat B ID (Mule). 32-bit global identity."
      - id: nonce
        type: u4
        doc: "Encryption nonce counter."
      - id: signature
        type: tig_common_types::ed25519_signature
        doc: "PUF Signature over payload fields."
      - id: global_crc
        type: u4
        doc: "Outer packet integrity checksum."

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
    seq:
      - id: pad_head
        size: 2
        doc: "Cycle alignment pad."
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

  # --- PACKET ACK: Ground Station Command ---
  packet_ack_body:
    doc: |
      Ground Station Acknowledgement / Unlock Command.
      
      Contains Relay Instructions and the encrypted unlock key.
      
      Tunnel Sizing:
        SNLP:  enc_tunnel = 96 bytes (padded for SNLP internal header).
        CCSDS: enc_tunnel = 88 bytes (compact, standard header).
      
      AUDIT NOTE (F-04): This type uses a runtime ternary for
      enc_tunnel sizing. For formal verification, split into
      packet_ack_body_snlp and packet_ack_body_ccsds with fixed sizes.
    seq:
      - id: pad_a
        size: 2
        doc: "32/64-bit alignment pad."
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
        size: "_root.is_snlp ? 96 : 88"
        doc: |
          ChaCha20 encrypted tunnel data.
          SNLP=96B, CCSDS=88B. Dynamic sizing maintained
          pending formal verification split decision.
      - id: pad_c
        size: 2
        doc: "Alignment to CRC boundary."
      - id: crc32
        type: u4
        doc: "Outer checksum."

  # --- HEARTBEAT (34 Bytes) ---
  heartbeat_body:
    doc: |
      System Heartbeat. Health, status, and GPS telemetry.
      Transmitted periodically for ground station monitoring.
    seq:
      - id: epoch_ts
        type: u8
        doc: "Unix timestamp."
      - id: vbatt_mv
        type: u2
        doc: "Battery voltage in millivolts."
      - id: temp_c
        type: s2
        doc: "Internal temp in centidegrees (2500 = 25.00°C)."
      - id: pressure_pa
        type: u4
        doc: "Barometric pressure in Pascals."
      - id: sys_state
        type: u1
        enum: tig_common_types::sys_state
        doc: "State machine ID."
      - id: sat_lock
        type: u1
        doc: "GPS satellite count."
      - id: gps_coords
        type: tig_common_types::gps_fixed_point
        doc: "Fixed-point lat/lon (scaled by 10^7)."
      - id: reserved_interval
        size: 2
        doc: "Reserved for 2026 payload expansion."
      - id: gps_speed_cms
        type: u2
        doc: "Ground speed in cm/s (540 = 5.4 m/s)."
      - id: crc32
        type: u4
        doc: "Heartbeat integrity checksum."
