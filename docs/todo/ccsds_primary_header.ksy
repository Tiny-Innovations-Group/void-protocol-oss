meta:
  id: ccsds_primary_header
  title: "CCSDS 133.0-B-2 Space Packet Primary Header (6 Octets)"
  endian: be
  license: Apache-2.0

doc: |
  Strict implementation of CCSDS 133.0-B-2 Section 4.1.3.
  
  6-octet Primary Header. Big-Endian (Network Byte Order).
  Self-delimiting via the Packet Data Length field.
  
  Bit Layout:
    [0-2]   Version Number     3 bits   MUST be 000
    [3]     Packet Type        1 bit    0=TM, 1=TC
    [4]     Sec Header Flag    1 bit    1=Present
    [5-15]  APID               11 bits  0x000-0x7FF
    [16-17] Sequence Flags     2 bits   11=Unsegmented
    [18-31] Sequence Count     14 bits  0-16383
    [32-47] Packet Data Length 16 bits  (N-1) octets

  This module is transport-agnostic and reusable across any
  CCSDS-compliant project. It owns ONLY the 6-byte header.

seq:
  - id: version_type_flag_apid
    type: u2
    doc: "Word 0: Version(3) + Type(1) + SecHdrFlag(1) + APID(11)"

  - id: seq_flags_count
    type: u2
    doc: "Word 1: SeqFlags(2) + SeqCount(14)"

  - id: packet_data_length
    type: u2
    doc: "Word 2: Packet Data Field Length minus 1"

instances:
  version:
    value: (version_type_flag_apid >> 13) & 0b111
    # NOTE (C-02): Kaitai `valid:` is not supported on computed instances —
    # it only works on seq fields parsed directly from the stream. Attempting
    # `valid: { eq: 0 }` here causes a kaitai-struct-compiler error. The
    # CCSDS Version 1 constraint is therefore enforced at the application
    # layer (gateway ingest handler rejects any frame with version != 0).
    doc: |
      3-bit Version Number. MUST be 0 (CCSDS Version 1 per CCSDS 133.0-B-2
      §4.1.3.1). Non-zero indicates an unknown future revision and MUST be
      rejected by the ingest handler. Enforced application-side — kaitai
      `valid:` is unsupported on instance fields.

  packet_type:
    value: (version_type_flag_apid >> 12) & 0b1
    doc: "0=Telemetry (TM), 1=Telecommand (TC)."

  sec_header_flag:
    value: (version_type_flag_apid >> 11) & 0b1
    doc: "1=Secondary Header present in Packet Data Field."

  apid:
    value: version_type_flag_apid & 0x7FF
    doc: "11-bit Application Process Identifier (0-2047)."

  seq_flags:
    value: (seq_flags_count >> 14) & 0b11
    doc: "00=Continuation, 01=First, 10=Last, 11=Unsegmented."

  seq_count:
    value: seq_flags_count & 0x3FFF
    doc: |
      14-bit Packet Sequence Count (0-16383).
      HARDENED: Mask is 0x3FFF (14 bits). 
      Previous version used 0x3FF (10 bits) — CRITICAL BUG.

  data_field_length:
    value: packet_data_length + 1
    doc: "Actual octets in Packet Data Field."

  total_packet_length:
    value: packet_data_length + 7
    doc: "Total packet size in octets = 6 (header) + data_field_length."
