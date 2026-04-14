meta:
  id: void_protocol
  title: VOID Protocol v2.1 (Dual-Header Routing / Mixed-Endian )
  license: Apache 2.0
  endian: le # Default: Payloads are Little-Endian
  file-extension: void_protocol

doc: |
  ------------------------------------------------------------------------
  🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
  -------------------------------------------------------------------------
  Authority: Tiny Innovation Group Ltd
  License:   Apache 2.0
  Status:    Authenticated Clean Room Spec
  File:      void_protocol.ksy
  Desc:      A Kaitai Struct file(KSY) containing the dual-header aerospace protocol for orbital settlement.
  -------------------------------------------------------------------------

  VOID Protocol v2.1 (Layer 2 M2M Settlement)
  
  ## Routing Architecture
  This parser automatically distinguishes between two physical layer frame formats:
  1. Community Tier (SNLP): Optimized for LoRa/ISM bands (433/868/915 MHz). 
     Identified by the Sync Word `0x1D01A5A5`. Uses a 14-byte header.
  2. Enterprise Tier (CCSDS): Standard S-Band/X-Band format. 
     Uses a strict 6-byte CCSDS Space Packet Protocol header.
  
  ## Public Data Mandate
  As per the TinyGS operational requirement, all traffic routed via the SNLP (Community) 
  path is treated as PUBLIC DOMAIN. The `enc_payload` field in Packet B is transmitted 
  in plaintext when using SNLP to ensure compliance with amateur radio encryption bans.

doc-ref:
  https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/README.md
  https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Acknowledgment-spec.md
  https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Protocol-spec-CCSDS.md
  https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Protocol-spec-SNLP.md
  https://github.com/Tiny-Innovations-Group/void-protocol-oss/blob/main/docs/Receipt-spec.md


seq:
  # 2. HEADER: Parse the correct routing header based on the peek
  - id: routing_header
    type:
      switch-on: _root.is_snlp
      cases:
        true: header_snlp    # 14 Bytes (Community)
        false: header_ccsds  # 6 Bytes  (Enterprise)

  # 3. BODY: Dispatch based on Payload Length (Derived from Header)
  - id: body
    type:
      switch-on: payload_len
      cases:
        # --- SHARED PAYLOADS (Identical Body) ---
        66:  packet_a_body    # Invoice (A) — VOID-114B: body 62→66 with _pad_head + _pre_crc
        178: packet_b_body    # Payment (B) — VOID-114B: body 166→174+_tail_pad[4]=178, frame 184/192 (÷8 ✅)
        106: packet_h_body    # Handshake (H)
        98:  packet_c_body    # Receipt (C)

        # --- COLLISIONS (Resolved by CCSDS Type Bit) ---
        # Packet D (Telemetry) vs ACK (Command)
        122: dispatch_122

        # --- VARIANT PAYLOADS (Padding Differences) ---
        114: packet_ack_body  # ACK (CCSDS - No Tail Pad)
        34: heartbeat_body # System Heartbeat — VOID-114B: reordered, size unchanged

instances:
  magic_peek:
    pos: 0
    type: u4be
    
  # Logic: If first 4 bytes = 0x1D01A5A5 (sync word), it's SNLP.
  is_snlp:
    value: magic_peek == 0x1D01A5A5

  # Calculate Payload Length: Total Size - Header Size
  payload_len:
    value: >-
      is_snlp ? (_io.size - 14) : (_io.size - 6)
      
  global_packet_type:
    value: >-
      is_snlp 
      ? (routing_header.as<header_snlp>.ccsds.packet_type) 
      : (routing_header.as<header_ccsds>.packet_type)
    doc: "0=Telemetry, 1=Command. valid for both SNLP and CCSDS."

  global_apid:
    value: >-
      is_snlp 
      ? (routing_header.as<header_snlp>.ccsds.apid) 
      : (routing_header.as<header_ccsds>.apid)


types:
  # ==========================================
  # HEADERS (BIG ENDIAN ENFORCED)
  # ==========================================
  header_ccsds:
  
    doc: |
      CCSDS Primary Header - 6 Bytes
  
      Standard CCSDS 133.0-B-2 Space Packet Header.
        - **Endianness:** Big-Endian (Network Byte Order).
        - **APID:** Used for local routing. Global identity is handled via `sat_id` in the payload.
    meta:
      endian: be  # STRICT: CCSDS Headers are Big-Endian
    seq:
      - id: version_type_sec_apid
        type: u2
      - id: seq_flags_count
        type: u2
      - id: packet_length
        type: u2
    instances:
      version:
        value: (version_type_sec_apid >> 13) & 0b111
      packet_type:
        value: (version_type_sec_apid >> 12) & 0b1
        doc: "0=Telemetry (Packet D), 1=Command (Packet ACK)"
      sec_header_flag:
        value: (version_type_sec_apid >> 11) & 0b1
      apid:
        value: version_type_sec_apid & 0x7FF
      seq_flags:
        value: (seq_flags_count >> 14) & 0b11
      seq_count:
        value: seq_flags_count & 0x3FF
      actual_length:
        value: packet_length + 7
        doc: "Total packet length in bytes (derived from CCSDS field)"

  header_snlp:
    doc: |
      Space Network Layer Protocol (SNLP) Header - 14 Bytes
    
      Designed for asynchronous, low-bandwidth LoRa links.
      Structure:
        - **Sync Word (4B):** `0x1D01A5A5` for frame detection in noise.
        - **CCSDS (6B):** Standard space packet identity.
        - **Alignment Pad (4B):** Ensures the payload starts at a 64-bit aligned memory address.
    meta:
      endian: be  # STRICT: Headers are Big-Endian
    seq:
    - id: sync_word
      contents: [0x1D, 0x01, 0xA5, 0xA5]
      doc: "Community Sync Word (LoRa)"
    - id: ccsds
      type: header_ccsds
    - id: align_pad
      size: 4
      doc: "4-Byte Pad for 64-bit Alignment"

  # ==========================================
  # Custom Types
  # ==========================================
  vector_3d:
      doc: "IEEE 754 Double Precision Vector (X, Y, Z)"
      seq:
        - id: x
          type: f8
          doc: "ECEF X Coordinate (Meters)"
        - id: y
          type: f8
          doc: "ECEF Y Coordinate (Meters)"
        - id: z
          type: f8
          doc: "ECEF Z Coordinate (Meters)"
  
  vector_3f:
    doc: "IEEE 754 Single Precision Vector (dX, dY, dZ)"
    seq:
      - id: x
        type: f4
      - id: y
        type: f4
      - id: z
        type: f4
  # ==========================================
  # DISPATCHERS
  # ==========================================
  
  dispatch_122:
    seq:
      - id: content
        type:
          switch-on: _root.global_packet_type
          cases:
            0: packet_d_body    # Type 0 = Telemetry (Delivery)
            1: packet_ack_body  # Type 1 = Command (Ack)

  # ==========================================
  # PAYLOAD BODIES (LITTLE ENDIAN)
  # ==========================================
  
  # --- PACKET B (Payment) ---
  packet_b_body:
    doc: |
      Packet B: The Encrypted Payment (VOID-110 + VOID-114B)

      Encapsulated payment intent sent by Sat B (Mule) to the Ground Station.

      ## Encryption Logic
      - **Enterprise (CCSDS):** Payload is ChaCha20 encrypted with a deterministic
        12-byte nonce derived as `sat_id || epoch_ts`. The nonce is NOT on the wire.
      - **Community (SNLP):** Payload is PLAINTEXT to comply with TinyGS/Amateur
        radio regulations. The ChaCha20 path is disabled.

      ## Nonce Derivation (CCSDS only)
        nonce[12] = sat_id[4] || epoch_ts[8]     # both little-endian
      Uniqueness is guaranteed by monotonic millisecond `epoch_ts` and per-asset
      `sat_id`. See Protocol-spec-CCSDS.md §3.

      ## VOID-114B Body Alignment
      Body = 178 bytes (174 payload + 4-byte `_tail_pad`). The `_pad_head`,
      `_pre_sat`, and `_pre_sig` slots push every critical field (epoch,
      pos_vec, sat_id, signature, global_crc) onto its natural alignment
      boundary under both CCSDS (H=6) and SNLP (H=14) headers, since
      6 ≡ 14 (mod 8). The `_tail_pad` brings frame totals to 184 (CCSDS ÷8)
      and 192 (SNLP ÷64 cache line) for DMA-coalesced SPI bursts to the
      SX1262 on ESP32-S3 / Heltec LoRa V3 class hardware.
    seq:
      - id: pad_head
        size: 2
        doc: "VOID-114B alignment pad (keeps u64 head aligned under both headers)"
      - id: epoch_ts
        type: u8
        doc: "Little-Endian millisecond Unix timestamp. Must be strictly monotonic."
      - id: pos_vec
        type: vector_3d
        doc: "Sat B Position Vector (X, Y, Z)"
      - id: enc_payload
        size: 62
        doc: "Inner Invoice. Plaintext if SNLP (Public), ChaCha20 Ciphertext if CCSDS (Private)."
      - id: pre_sat
        size: 2
        doc: "VOID-114B alignment pad (aligns sat_id after 62-byte ciphertext)"
      - id: sat_id
        type: u4
        doc: "Sat B identity. First 4 bytes of the derived ChaCha20 nonce."
      - id: pre_sig
        size: 4
        doc: "VOID-114B alignment pad (aligns signature on 8-byte boundary)"
      - id: signature
        size: 64
        doc: "Ed25519 over (header + body[0..pre_sig end] = 106 body bytes). 8-byte aligned."
      - id: global_crc
        type: u4
      - id: tail_pad
        size: 4
        doc: "VOID-114B tail pad — frame total 184 CCSDS / 192 SNLP (÷8 and ÷64 respectively)"


  # --- PACKET A (Invoice) ---
  packet_a_body:
    doc: |
      The Invoice (66 Bytes Payload)

      VOID-114B: `_pad_head` and `_pre_crc` pads ensure every critical
      field lands on a natural alignment boundary under both CCSDS (H=6)
      and SNLP (H=14) headers.
    seq:
      - id: pad_head
        size: 2
        doc: "VOID-114B alignment pad"
      - id: epoch_ts
        type: u8
      - id: pos_vec
        type: vector_3d
      - id: vel_vec
        type: vector_3f
      - id: sat_id
        type: u4
      - id: amount
        type: u8
      - id: asset_id
        type: u2
      - id: pre_crc
        size: 2
        doc: "VOID-114B alignment pad"
      - id: crc32
        type: u4

  # --- PACKET H (Handshake) ---
  packet_h_body:
    doc: "Ephemeral Key Exchange (106 Bytes Payload)"
    seq:
      - id: session_ttl
        type: u2
      - id: timestamp
        type: u8
      - id: eph_pub_key
        size: 32
      - id: signature
        size: 64

  # --- PACKET C (Receipt) ---
  packet_c_body:
    doc: |
      Packet C: The Receipt (Proof of Execution)

      Generated by Sat A (Seller) after a successful service dispensation.
      Contains the cryptographic signature linking the service to the original payment nonce.
    seq:
      - id: pad_head
        size: 2
      - id: exec_time
        type: u8
      - id: enc_tx_id
        type: u8
      - id: enc_status
        type: u1
      - id: pad_sig
        size: 7
      - id: signature
        size: 64
      - id: crc32
        type: u4
      - id: tail_pad
        size: 4

  # --- PACKET D (Delivery) ---
  packet_d_body:
    doc: |
      Packet D: The Delivery

      A transport wrapper used by Sat B (Mule) to downlink Packet C to the ground.
      This packet wraps the receipt in a new CCSDS/SNLP frame for radio transmission.
    seq:
      - id: pad_head
        size: 2
      - id: downlink_ts
        type: u8
      - id: sat_b_id
        type: u4
      - id: payload
        size: 98
      - id: global_crc
        type: u4
      - id: tail
        size: 6

  # --- PACKET ACK (Command) ---
  packet_ack_body:
    doc: |
      Packet ACK: Ground Station Command

      Uplink packet containing Relay Instructions and the 'Unlock' key.

      ## Tunnel Data Sizing
      The `enc_tunnel` field varies in size to maintain alignment:
      - **SNLP:** 96 Bytes (Includes 14B SNLP Header internally).
      - **CCSDS:** 88 Bytes (Standard 6B Header).
    seq:
      - id: pad_a
        size: 2
      - id: target_tx_id
        type: u4
      - id: status
        type: u1
      - id: pad_b
        size: 1
      - id: relay_ops
        type: relay_ops_t
      - id: enc_tunnel
        size: "_root.is_snlp ? 96 : 88"  # <--- DYNAMIC SIZING
        doc: "SNLP=96B (Full Header), CCSDS=88B (Compact Header)"
      - id: pad_c
        size: 2
      - id: crc32
        type: u4
        
  relay_ops_t:
    seq:
      - id: azimuth
        type: u2
      - id: elevation
        type: u2
      - id: frequency
        type: u4
      - id: duration_ms
        type: u4
  
  heartbeat_body:
    doc: |
      System Heartbeat (Health & Status & GPS)

      VOID-114B: Fields reordered so every critical field lands on its
      natural alignment boundary. Body total is still 34 bytes — the
      legacy `reserved_interval[2]` field was dropped (unused) and
      `pad_head` takes its place as the forward-compat slot.
    seq:
      - id: pad_head
        size: 2
        doc: "VOID-114B alignment pad (also serves as forward-compat slot)"
      - id: epoch_ts
        type: u8
        doc: "Timestamp (Unix)"
      - id: pressure_pa
        type: u4
        doc: "Barometric Pressure (Pascals)"
      - id: lat_fixed
        type: s4
        doc: "Latitude (Scaled by 10^7, e.g. 51500000 = 51.5N)"
      - id: lon_fixed
        type: s4
        doc: "Longitude (Scaled by 10^7, e.g. -120000 = 0.12W)"
      - id: vbatt_mv
        type: u2
        doc: "Battery Voltage (mV)"
      - id: temp_c
        type: s2
        doc: "Internal Temp (Centidegrees, e.g. 2500 = 25.00C)"
      - id: gps_speed_cms
        type: u2
        doc: "Ground Speed (cm/s, e.g. 540 = 5.4 m/s)"
      - id: sys_state
        type: u1
        doc: "State Machine ID (0=Boot, 1=Idle, 2=Tx...)"
      - id: sat_lock
        type: u1
        doc: "GPS Satellite Count"
      - id: crc32
        type: u4