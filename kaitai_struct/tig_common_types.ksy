meta:
  id: tig_common_types
  title: "TIG Common Binary Types & Enums"
  endian: le
  license: Apache-2.0

doc: |
  Reusable field types and enumerations for all VOP packet bodies.
  
  All types in this module are Little-Endian (payload convention).
  Import this module into any packet body definition that uses
  vectors, signatures, or protocol-defined enumerations.

enums:
  # asset_id: Stablecoin-only mandate. Extend here for new verified assets.
  asset_id:
    1: usdc

  settlement_status:
    0x01: settled
    0xFF: rejected

  sys_state:
    0: boot
    1: idle
    2: tx_active
    3: rx_active
    4: connected
    5: provisioning
    6: error

  cmd_code:
    0x0001: unlock_dispense
    0x00FF: atomic_destroy

  packet_type_id:
    0: telemetry
    1: telecommand

types:
  # ==========================================
  # GEOMETRIC PRIMITIVES
  # ==========================================
  vector_3d:
    doc: |
      IEEE 754 Double-Precision 3D Position Vector (24 bytes).
      ECEF Coordinates in meters. Little-Endian.
      Alignment: Each f8 sits on natural 8-byte boundary when
      the vector starts on an 8-byte aligned offset.
    seq:
      - id: x
        type: f8
        doc: "ECEF X Coordinate (meters)"
      - id: y
        type: f8
        doc: "ECEF Y Coordinate (meters)"
      - id: z
        type: f8
        doc: "ECEF Z Coordinate (meters)"

  vector_3f:
    doc: |
      IEEE 754 Single-Precision 3D Velocity Vector (12 bytes).
      Derivative components (dX, dY, dZ). Little-Endian.
    seq:
      - id: x
        type: f4
        doc: "dX velocity component"
      - id: y
        type: f4
        doc: "dY velocity component"
      - id: z
        type: f4
        doc: "dZ velocity component"

  # ==========================================
  # CRYPTOGRAPHIC PRIMITIVES
  # ==========================================
  ed25519_signature:
    doc: |
      64-byte Ed25519 / PUF Identity Signature.
      Opaque blob — no internal structure parsed.
      Verification is performed by the application layer,
      not the binary parser.
    seq:
      - id: raw
        size: 64

  x25519_public_key:
    doc: "32-byte X25519 Ephemeral Public Key for ECDH."
    seq:
      - id: raw
        size: 32

  # ==========================================
  # OPERATIONAL SUB-STRUCTURES
  # ==========================================
  relay_ops:
    doc: |
      12-byte Relay Operations Sub-structure.
      Used by Sat B to orient transmission toward Sat A.
      All fields Little-Endian.
    seq:
      - id: azimuth
        type: u2
        doc: "Horizontal Look Angle (degrees * scale factor)"
      - id: elevation
        type: u2
        doc: "Vertical Look Angle (degrees * scale factor)"
      - id: frequency
        type: u4
        doc: "Target Tx Frequency in Hz"
      - id: duration_ms
        type: u4
        doc: "Relay Window duration in milliseconds"

  # ==========================================
  # GPS FIXED-POINT TYPES
  # ==========================================
  gps_fixed_point:
    doc: |
      GPS coordinates in fixed-point representation.
      Scaled by 10^7 (e.g., 515000000 = 51.5000000 degrees N).
      Signed: negative = South/West.
    seq:
      - id: latitude
        type: s4
        doc: "Latitude * 10^7"
      - id: longitude
        type: s4
        doc: "Longitude * 10^7"
