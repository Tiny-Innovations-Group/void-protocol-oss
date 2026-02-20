# -------------------------------------------------------------------------
# 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
# -------------------------------------------------------------------------
# Authority: Tiny Innovation Group Ltd
# License:   Apache 2.0
# File:      ground_station.py
# Desc:      Class-based L2 Bridge Authority.
# Compliant: Strict C-Struct Memory Mapping & Endianness Enforcement.
# -------------------------------------------------------------------------


import serial
import binascii
import time
from nacl.public import PrivateKey, PublicKey
from nacl.hash import generichash
from nacl.signing import VerifyKey
from nacl.exceptions import BadSignatureError

# Import our strict memory mappings
import void_packets as vp 

class VoidGroundStation:
    def __init__(self, port: str, baud_rate: int = 115200):
        self.port = port
        self.baud_rate = baud_rate
        self.serial_conn = None
        
        # Ground's Ephemeral State
        self._eph_priv = PrivateKey.generate()
        self._eph_pub = self._eph_priv.public_key
        self.session_key = None

        # Mock Database for Satellite Identity Public Keys (Ed25519)
        # In production, this is fetched from Postgres using SatID
        self.trusted_sat_keys = {}

        self.receipts_db = []
        
        print("🛰️  Void Protocol L2 Ground Station Initialized.")
        print("🛡️  Strict C-Struct Memory Mapping Enabled.")

    def connect(self) -> bool:
        try:
            self.serial_conn = serial.Serial(self.port, self.baud_rate, timeout=1)
            print(f"📡 Connected to {self.port}")
            return True
        except serial.SerialException as e:
            print(f"❌ Failed to open {self.port}: {e}")
            return False

    def trigger_handshake(self):
        if self.serial_conn:
            self.serial_conn.write(b'H')

    def approve_buy(self):
        if self.serial_conn:
            self.serial_conn.write(b'ACK_BUY\n')

    def listen(self):
        if not self.serial_conn: return
        try:
            while True:
                line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                if not line: continue

                if "HANDSHAKE_TX:" in line:
                    self._process_handshake(line)
                elif "INVOICE:" in line:
                    self._process_invoice(line)
                elif "PACKET_B:" in line:
                    self._process_settlement(line)
                elif "PACKET_D:" in line:
                    self._process_receipt(line)
        except KeyboardInterrupt:
            self.shutdown()

    # --- PHASE 2: HANDSHAKE (Strict Parsing) ---
    def _process_handshake(self, raw_line: str):
        print("\n[>>] RECEIVED HANDSHAKE FROM SAT B")
        try:
            hex_data = raw_line.split("HANDSHAKE_TX:")[1]
            raw_bytes = binascii.unhexlify(hex_data)
            
            # 1. MAP MEMORY DIRECTLY TO C-STRUCT
            if len(raw_bytes) != vp.SIZE_PACKET_H:
                raise ValueError("Packet H Size Mismatch")
                
            pkt_h = vp.PacketH_t.from_buffer_copy(raw_bytes)
            
            # 2. VERIFY SIGNATURE (Zero Trust)
            signed_payload = raw_bytes[0:48] # Header(6) + TTL(2) + TS(8) + PubKey(32)
            signature = bytes(pkt_h.signature)
            
            # NOTE: In the demo, we bypass the actual VerifyKey check because 
            # we haven't synced the ESP32's random Ed25519 key to Python.
            # verify_key = VerifyKey(self.trusted_sat_keys[sat_id])
            # verify_key.verify(signed_payload, signature)
            print("     [🛡️] Signature verified against DB (Simulated).")

            # 3. EXTRACT PUBKEY USING STRUCT ATTRIBUTES
            sat_eph_pub = PublicKey(bytes(pkt_h.eph_pub_key))

            
            
            # 4. ECDH & HASH
            shared_secret = self._eph_priv.exchange(sat_eph_pub)
            self.session_key = generichash(shared_secret)
            print(f"     [🔒] Session Key: {binascii.hexlify(self.session_key).decode('utf-8')[:16]}...")

            response = b'HANDSHAKE_ACK:' + binascii.hexlify(bytes(self._eph_pub)) + b'\n'
            self.serial_conn.write(response)
            
        except Exception as e:
            print(f"❌ Handshake failed: {e}")

    def _process_invoice(self, raw_line: str):
        print("\n[🔔] NEW INVOICE RECEIVED FROM ORBIT!")
        print("     Type 'ack' in the CLI to approve.")

    # --- PHASE 4: ACKNOWLEDGEMENT & TUNNEL (Strict Generation) ---
    def _process_settlement(self, raw_line: str):
        print("\n[💸] PACKET B RECEIVED. Settling on L2 Chain...")
        time.sleep(1)
        print("     [✅] L2 Confirmed! Generating strictly typed ACK Downlink.")
        
        # 1. BUILD THE TUNNEL DATA (Destined for Sat A)
        tunnel = vp.TunnelData_t()
        tunnel.header.ver_type_sec = 0x18
        tunnel.header.apid_lo = 0xA1 # APID: Sat A
        
        # Endianness safety for length: (88 - 1) = 87 -> 0x0057
        raw_len_t = vp.SIZE_TUNNEL_DATA - 1
        tunnel.header.packet_len = ((raw_len_t >> 8) & 0xFF) | ((raw_len_t & 0xFF) << 8)
        
        tunnel.cmd_code = 0x0001 # 0x0001 = UNLOCK
        tunnel.ttl = 600
        
        # Mock Encryption of Tunnel (In prod, ChaCha20 using session_key)
        enc_tunnel_bytes = bytes(tunnel)

        # 2. BUILD THE ACK PACKET (Destined for Sat B)
        ack = vp.PacketAck_t()
        ack.header.ver_type_sec = 0x18
        ack.header.apid_lo = 0xB2 # APID: Sat B
        
        # Endianness safety for length: (120 - 1) = 119 -> 0x0077
        raw_len_a = vp.SIZE_PACKET_ACK - 1
        ack.header.packet_len = ((raw_len_a >> 8) & 0xFF) | ((raw_len_a & 0xFF) << 8)

        ack.target_tx_id = 0x12345678
        ack.status = 0x01 # 1 = Settled
        
        # Fill Relay Ops
        ack.relay_ops.duration_ms = 5000
        
        # Copy encrypted tunnel into the struct's byte array
        for i in range(len(enc_tunnel_bytes)):
            ack.enc_tunnel[i] = enc_tunnel_bytes[i]

        # 3. TRANSMIT EXACT BYTES
        ack_payload = bytes(ack)
        self.serial_conn.write(b'ACK_DOWNLINK:' + binascii.hexlify(ack_payload) + b'\n')

    def _process_receipt(self, raw_line: str):
        hex_data = raw_line.split("PACKET_D:")[1]
        print("\n[🧾] PACKET D (DELIVERY RECEIPT) RECEIVED FROM SAT A!")
        
        raw_bytes = binascii.unhexlify(hex_data)
        pkt_d = vp.PacketD_t.from_buffer_copy(raw_bytes)
        
        self.receipts_db.append(pkt_d)
        print(f"     [💾] Saved. Total receipts: {len(self.receipts_db)}")

    def shutdown(self):
        print("\n🛑 Shutting down.")
        if self.serial_conn: self.serial_conn.close()
        self.session_key = None