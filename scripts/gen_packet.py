import struct
import os
import zlib  # <--- ADDED for real CRC-32

try:
    from cryptography.hazmat.primitives.asymmetric import ed25519
    from cryptography.hazmat.primitives import serialization
    HAS_CRYPTO = True
except ImportError:
    HAS_CRYPTO = False
    print("⚠️  'cryptography' module not found. Installing it is required for real signatures: pip install cryptography")

"""
/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      gen_packet.py
 * Desc:      Packet generator for VOID Protocol v2.1 ksy testing.
 * -------------------------------------------------------------------------*/
"""

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

# --- PUF Private Keys (These match the public keys in your Go registry) ---
# Sat A (Seller - 100) Private Key
PRIV_HEX_A = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
# Sat B (Buyer/Mule - 101) Private Key
PRIV_HEX_B = "8994ec3b3d470df7432bd5b74783765822c756c7ff972942a5efdad61473605b"

if HAS_CRYPTO:
    # Load the keys into memory
    priv_key_a = ed25519.Ed25519PrivateKey.from_private_bytes(bytes.fromhex(PRIV_HEX_A))
    priv_key_b = ed25519.Ed25519PrivateKey.from_private_bytes(bytes.fromhex(PRIV_HEX_B))

# ==============================================================================
# HEADER BUILDER
# ==============================================================================
def build_header(is_snlp, payload_len, apid, is_cmd=False):
    ver = 0
    pkt_type = 1 if is_cmd else 0
    sec_flag = 1
    
    id_field_val = (ver << 13) | (pkt_type << 12) | (sec_flag << 11) | (apid & 0x7FF)
    ver_type_apid = struct.pack('>H', id_field_val)
    seq_count = struct.pack('>H', 0xC000)
    
    length_val = payload_len - 1
    pkt_length = struct.pack('>H', length_val)
    ccsds_block = ver_type_apid + seq_count + pkt_length

    if is_snlp:
        return SYNC_WORD + ccsds_block + SNLP_PAD
    else:
        return ccsds_block

# ==============================================================================
# BODY GENERATORS
# ==============================================================================

def gen_packet_a(is_snlp):
    """Packet A: Invoice (62 Byte Body, protocol-compliant)"""
    epoch    = struct.pack('<Q', 1710000000)       
    pos_vec  = struct.pack('<ddd', 7000.123, -12000.456, 550.789)  
    vel_vec  = struct.pack('<fff', 7.5, -0.2, 0.01)  
    sat_id   = struct.pack('<I', APID_SAT_A)       
    amount   = struct.pack('<Q', 420000000)        
    asset_id = struct.pack('<H', 1)                
    
    partial_body = epoch + pos_vec + vel_vec + sat_id + amount + asset_id
    header = build_header(is_snlp, len(partial_body) + 4, APID_SAT_A, is_cmd=False)
    
    crc = struct.pack('<I', zlib.crc32(header + partial_body) & 0xFFFFFFFF)
    return header + partial_body + crc

def gen_packet_b(is_snlp):
    """Packet B: Payment (protocol-compliant, 184B SNLP, 176B CCSDS)"""
    epoch       = struct.pack('<Q', 1710000100)       
    pos_vec     = struct.pack('<ddd', 7010.0, -11990.0, 560.0)  
    enc_payload = (
        struct.pack('<Q', 1710000000) +  
        struct.pack('<I', 420000000) +   
        struct.pack('<H', 1) +           
        b'PAYMENT' + b'\x00' * (62-8-4-2-7)  
    )[:62]
    sat_id      = struct.pack('<I', APID_SAT_B)       
    nonce       = struct.pack('<I', 123456)           
    
    message_to_sign = epoch + pos_vec + enc_payload + sat_id + nonce
    
    if HAS_CRYPTO:
        signature = priv_key_b.sign(message_to_sign)
    else:
        signature = bytes([0xAB + (i % 5) for i in range(64)])
        
    partial_body = message_to_sign + signature
    header = build_header(is_snlp, len(partial_body) + 4, APID_SAT_B, is_cmd=False)
    
    crc = struct.pack('<I', zlib.crc32(header + partial_body) & 0xFFFFFFFF)
    return header + partial_body + crc

def gen_packet_h(is_snlp):
    """Packet H: Handshake (106 Byte Body, protocol-compliant)"""
    ttl       = struct.pack('<H', 900)                
    timestamp = struct.pack('<Q', 1710000200)         
    pub_key   = bytes([0xA0 + (i % 16) for i in range(32)])
    
    to_sign = ttl + timestamp + pub_key
    if HAS_CRYPTO:
        signature = priv_key_b.sign(to_sign)
    else:
        signature = bytes([0xC0 + (i % 8) for i in range(64)])
        
    body = ttl + timestamp + pub_key + signature
    header = build_header(is_snlp, len(body), APID_SAT_B, is_cmd=False) # Packet H does not have a CRC
    return header + body

def gen_packet_c(is_snlp):
    """Packet C: Receipt (98 Byte Body, protocol-compliant)"""
    pad_head   = b'\x00\x00'                      
    exec_time  = struct.pack('<Q', 1710000300)      
    enc_tx_id  = struct.pack('<Q', 0xDEADBEEFCAFEBABE) 
    enc_status = b'\x01'                           
    pad_sig    = b'\x00' * 7                       
    
    message_to_sign = pad_head + exec_time + enc_tx_id + enc_status + pad_sig
    if HAS_CRYPTO:
        signature = priv_key_a.sign(message_to_sign)
    else:
        signature = bytes([0xD0 + (i % 7) for i in range(64)])
        
    tail_pad   = b'\x00' * 4
    partial_body = message_to_sign + signature
    
    header = build_header(is_snlp, len(partial_body) + 4 + len(tail_pad), APID_SAT_A, is_cmd=False)
    crc = struct.pack('<I', zlib.crc32(header + partial_body) & 0xFFFFFFFF)
    
    return header + partial_body + crc + tail_pad

def gen_packet_d(is_snlp):
    """Packet D: Delivery (122 Byte Body, protocol-compliant)"""
    pad_head    = b'\x00\x00'                     
    downlink_ts = struct.pack('<Q', 1710000400)     
    sat_b_id    = struct.pack('<I', APID_SAT_B)     
    payload     = bytes([0xE0 + (i % 16) for i in range(98)])
    
    tail = b'\x00' * 6
    partial_body = pad_head + downlink_ts + sat_b_id + payload
    
    header = build_header(is_snlp, len(partial_body) + 4 + len(tail), APID_SAT_B, is_cmd=False)
    crc = struct.pack('<I', zlib.crc32(header + partial_body) & 0xFFFFFFFF)
    
    return header + partial_body + crc + tail

def gen_packet_ack(is_snlp):
    """Packet ACK: Command (114B CCSDS, 122B SNLP, protocol-compliant)"""
    pad_a        = b'\x00\x00'                    
    target_tx_id = struct.pack('<I', 0xCAFEBABE)   
    status       = b'\x01'                        
    pad_b        = b'\x00'                        
    
    relay_ops    = (
        struct.pack('<H', 180) +    
        struct.pack('<H', 45) +     
        struct.pack('<I', 437200000) + 
        struct.pack('<I', 5000)     
    )
    
    tunnel_size = 96 if is_snlp else 88
    enc_tunnel = (
        b'\xAA' * 6 + b'\x00' * 2 + struct.pack('<Q', 0x123456789ABCDEF0) +
        struct.pack('<H', 0x0001) + struct.pack('<H', 600) +
        bytes([0xF0 + (i % 8) for i in range(64)]) + struct.pack('<I', 0xBEEFCAFE)
    )[:tunnel_size]
    
    pad_c        = b'\x00\x00'                    
    
    partial_body = pad_a + target_tx_id + status + pad_b + relay_ops + enc_tunnel + pad_c
    header = build_header(is_snlp, len(partial_body) + 4, APID_SAT_B, is_cmd=True)
    
    crc = struct.pack('<I', zlib.crc32(header + partial_body) & 0xFFFFFFFF)
    return header + partial_body + crc

def gen_packet_l(is_snlp):
    """Packet L: Heartbeat (34 Byte Body, protocol-compliant)"""
    epoch_ts      = struct.pack('<Q', 1710000500)   
    vbatt_mv      = struct.pack('<H', 4100)         
    temp_c        = struct.pack('<h', 2300)         
    pressure_pa   = struct.pack('<I', 100800)       
    sys_state     = struct.pack('<B', 3)            
    sat_lock      = struct.pack('<B', 8)            
    lat_fixed     = struct.pack('<i', 515000000)    
    lon_fixed     = struct.pack('<i', -128000)      
    reserved      = b'\x00\x00'                    
    gps_speed_cms = struct.pack('<H', 600)          
    
    partial_body = (epoch_ts + vbatt_mv + temp_c + pressure_pa + 
        sys_state + sat_lock + lat_fixed + lon_fixed + 
        reserved + gps_speed_cms)
        
    header = build_header(is_snlp, len(partial_body) + 4, APID_SAT_B, is_cmd=False)
    crc = struct.pack('<I', zlib.crc32(header + partial_body) & 0xFFFFFFFF)
    return header + partial_body + crc

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
        ("packet_l_heartbeat", gen_packet_l),
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