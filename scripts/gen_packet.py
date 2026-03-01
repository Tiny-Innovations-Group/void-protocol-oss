import struct
import os
'''
/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      gen_packet.py
 * Desc:      Packet generator for VOID Protocol v2.1 ksy testing.
 * -------------------------------------------------------------------------*/
'''

# ==============================================================================
# CONFIGURATION & CONSTANTS
# ==============================================================================
OUTPUT_DIR = "generated_packets"

# SNLP Constants
SYNC_WORD = b'\x1D\x01\xA5\xA5'  # 0x1D01A5A5
SNLP_PAD  = b'\x00\x00\x00\x00'  # 4-Byte Alignment Pad (Total Header = 14B)

# APID Defaults
APID_GROUND = 0
APID_SAT_A  = 100  # Seller
APID_SAT_B  = 101  # Mule/Buyer

# ==============================================================================
# HEADER BUILDER
# ==============================================================================
def build_header(is_snlp, payload_len, apid, is_cmd=False):
    """
    Constructs either a 14-byte SNLP header or a 6-byte CCSDS header.
    """
    # --- 1. CCSDS Primary Header (6 Bytes) ---
    # Bits 0-2: Version (000)
    # Bit 3:    Type (0=Telemetry/Data, 1=Command)
    # Bit 4:    Sec Header Flag (1=Present)
    # Bits 5-15: APID
    
    ver = 0
    pkt_type = 1 if is_cmd else 0
    sec_flag = 1
    
    # Construct the 16-bit ID field
    # (Ver << 13) | (Type << 12) | (Sec << 11) | APID
    id_field_val = (ver << 13) | (pkt_type << 12) | (sec_flag << 11) | (apid & 0x7FF)
    ver_type_apid = struct.pack('>H', id_field_val)

    # Sequence Flags (11=Unsegmented) | Count (0)
    # 0xC000 = 1100 0000 ...
    seq_count = struct.pack('>H', 0xC000)

    # Packet Length (16 bits)
    # Definition: Total Octets in Packet Data Field (Payload) - 1
    length_val = payload_len - 1
    pkt_length = struct.pack('>H', length_val)

    ccsds_block = ver_type_apid + seq_count + pkt_length

    # --- 2. SNLP Wrapper (If enabled) ---
    if is_snlp:
        # 14 Bytes: Sync(4) + CCSDS(6) + Pad(4)
        return SYNC_WORD + ccsds_block + SNLP_PAD
    else:
        # 6 Bytes: CCSDS Only
        return ccsds_block

# ==============================================================================
# BODY GENERATORS
# ==============================================================================

def gen_packet_a(is_snlp):
    """Packet A: Invoice (62 Byte Body)"""
    # Payload Fields
    epoch    = struct.pack('<Q', 1709300000)       # 8B
    pos_vec  = struct.pack('<ddd', 1.1, 2.2, 3.3)  # 24B
    vel_vec  = struct.pack('<fff', 0.1, 0.2, 0.3)  # 12B
    sat_id   = struct.pack('<I', APID_SAT_A)       # 4B
    amount   = struct.pack('<Q', 5000)             # 8B
    asset_id = struct.pack('<H', 1)                # 2B
    crc      = struct.pack('<I', 0xAAAA1111)       # 4B
    
    body = epoch + pos_vec + vel_vec + sat_id + amount + asset_id + crc
    # Total Body: 62 Bytes
    
    header = build_header(is_snlp, len(body), APID_SAT_A, is_cmd=False)
    return header + body

def gen_packet_b(is_snlp):
    """Packet B: Payment (170 Byte Body)"""
    epoch       = struct.pack('<Q', 1709300001)       # 8B
    pos_vec     = struct.pack('<ddd', 4.4, 5.5, 6.6)  # 24B
    enc_payload = b'\xBB' * 62                        # 62B (Ciphertext/Plaintext)
    sat_id      = struct.pack('<I', APID_SAT_B)       # 4B
    nonce       = struct.pack('<I', 999)              # 4B
    signature   = b'\xCC' * 64                        # 64B
    crc         = struct.pack('<I', 0xBBBB2222)       # 4B
    
    body = epoch + pos_vec + enc_payload + sat_id + nonce + signature + crc
    # Total Body: 170 Bytes
    
    header = build_header(is_snlp, len(body), APID_SAT_B, is_cmd=False)
    return header + body

def gen_packet_h(is_snlp):
    """Packet H: Handshake (106 Byte Body)"""
    ttl       = struct.pack('<H', 600)          # 2B
    timestamp = struct.pack('<Q', 1709300002)   # 8B
    pub_key   = b'\xDD' * 32                    # 32B
    signature = b'\xEE' * 64                    # 64B
    
    body = ttl + timestamp + pub_key + signature
    # Total Body: 106 Bytes
    
    header = build_header(is_snlp, len(body), APID_SAT_B, is_cmd=False)
    return header + body

def gen_packet_c(is_snlp):
    """Packet C: Receipt (98 Byte Body)"""
    pad_head   = b'\x00\x00'                      # 2B
    exec_time  = struct.pack('<Q', 1709300003)    # 8B
    enc_tx_id  = struct.pack('<Q', 8888)          # 8B
    enc_status = b'\x01'                          # 1B
    pad_sig    = b'\x00' * 7                      # 7B
    signature  = b'\xFF' * 64                     # 64B
    crc        = struct.pack('<I', 0xCCCC3333)    # 4B
    tail_pad   = b'\x00' * 4                      # 4B
    
    body = pad_head + exec_time + enc_tx_id + enc_status + pad_sig + signature + crc + tail_pad
    # Total Body: 98 Bytes
    
    header = build_header(is_snlp, len(body), APID_SAT_A, is_cmd=False)
    return header + body

def gen_packet_d(is_snlp):
    """Packet D: Delivery (122 Byte Body)"""
    # Packet D wraps Packet C (Receipt). 
    # For simulation, we create a dummy payload of 98 bytes.
    pad_head    = b'\x00\x00'                     # 2B
    downlink_ts = struct.pack('<Q', 1709300004)   # 8B
    sat_b_id    = struct.pack('<I', APID_SAT_B)   # 4B
    payload     = b'\xDD' * 98                    # 98B (The inner Packet C)
    global_crc  = struct.pack('<I', 0xDDDD4444)   # 4B
    tail        = b'\x00' * 6                     # 6B
    
    body = pad_head + downlink_ts + sat_b_id + payload + global_crc + tail
    # Total Body: 122 Bytes
    
    # NOTE: Packet D is Telemetry (Cmd=False). Collision with ACK (Cmd=True) on 122 bytes.
    header = build_header(is_snlp, len(body), APID_SAT_B, is_cmd=False)
    return header + body

def gen_packet_ack(is_snlp):
    """Packet ACK: Command (Variable Body Size)"""
    # Common Fields (26 Bytes)
    pad_a        = b'\x00\x00'                    # 2B
    target_tx_id = struct.pack('<I', 12345)       # 4B
    status       = b'\x01'                        # 1B
    pad_b        = b'\x00'                        # 1B
    relay_ops    = b'\x11' * 12                   # 12B
    pad_c        = b'\x00\x00'                    # 2B
    crc          = struct.pack('<I', 0xEEEE5555)  # 4B

    # Variable Tunnel Data
    # SNLP Tunnel = 96 Bytes. CCSDS Tunnel = 88 Bytes.
    tunnel_size = 96 if is_snlp else 88
    enc_tunnel = b'\x99' * tunnel_size
    
    # Assemble: Note order from spec/KSY
    # pad_a + target + status + pad_b + relay + tunnel + pad_c + crc
    body = pad_a + target_tx_id + status + pad_b + relay_ops + enc_tunnel + pad_c + crc
    
    # NOTE: ACK is a COMMAND (Type=1)
    header = build_header(is_snlp, len(body), APID_SAT_B, is_cmd=True)
    return header + body

def gen_packet_l(is_snlp):
    """Packet L: Heartbeat (34 Byte Body)"""
    # 34 Bytes Payload
    epoch_ts      = struct.pack('<Q', 1709300005) # 8B
    vbatt_mv      = struct.pack('<H', 4150)       # 2B (4.15V)
    temp_c        = struct.pack('<h', 2550)       # 2B (25.50C)
    pressure_pa   = struct.pack('<I', 101325)     # 4B
    sys_state     = struct.pack('<B', 2)          # 1B (Tx)
    sat_lock      = struct.pack('<B', 12)         # 1B
    lat_fixed     = struct.pack('<i', 515074000)  # 4B (51.5074 N)
    lon_fixed     = struct.pack('<i', -127800)    # 4B (-0.1278 W)
    reserved      = b'\x00\x00'                   # 2B
    gps_speed_cms = struct.pack('<H', 550)        # 2B (5.5 m/s)
    crc32         = struct.pack('<I', 0xCAFEBABE) # 4B

    body = (
        epoch_ts + vbatt_mv + temp_c + pressure_pa + 
        sys_state + sat_lock + lat_fixed + lon_fixed + 
        reserved + gps_speed_cms + crc32
    )
    # Total Body: 34 Bytes
    
    header = build_header(is_snlp, len(body), APID_SAT_B, is_cmd=False)
    return header + body

# ==============================================================================
# MAIN EXECUTION
# ==============================================================================

def main():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    packets = [
        ("packet_a_invoice", gen_packet_a),
        ("packet_b_payment", gen_packet_b),
        ("packet_h_handshake", gen_packet_h),
        ("packet_c_receipt", gen_packet_c),
        ("packet_d_delivery", gen_packet_d),
        ("packet_ack_command", gen_packet_ack),
        ("packet_l_heartbeat", gen_packet_l), # <--- ADDED PACKET L
    ]

    print(f"--- Generating Packets in '{OUTPUT_DIR}/' ---")

    for name, func in packets:
        # 1. Generate SNLP Version
        data_snlp = func(is_snlp=True)
        fname_snlp = f"{OUTPUT_DIR}/void_{name}_snlp.bin"
        with open(fname_snlp, "wb") as f:
            f.write(data_snlp)
        print(f"[{name.upper()}] SNLP: {len(data_snlp)} bytes -> {fname_snlp}")

        # 2. Generate CCSDS Version
        data_ccsds = func(is_snlp=False)
        fname_ccsds = f"{OUTPUT_DIR}/void_{name}_ccsds.bin"
        with open(fname_ccsds, "wb") as f:
            f.write(data_ccsds)
        print(f"[{name.upper()}] CCSDS: {len(data_ccsds)} bytes -> {fname_ccsds}")

    print("\n✅ Generation Complete.")

if __name__ == "__main__":
    main()