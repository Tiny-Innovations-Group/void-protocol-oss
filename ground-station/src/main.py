# -------------------------------------------------------------------------
# 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
# -------------------------------------------------------------------------
# Authority: Tiny Innovation Group Ltd
# License:   Apache 2.0
# File:      main.py
# Desc:      CLI Entry point for the Void Protocol Ground Station.
# -------------------------------------------------------------------------

import argparse
import threading   
from ground_station import VoidGroundStation

def main():
    # 1. Setup CLI Arguments (Fixed order)
    parser = argparse.ArgumentParser(description="Void Protocol L2 Ground Station CLI")
    parser.add_argument('-p', '--port', type=str, default='/dev/ttyUSB0', help="Serial port of the connected Heltec board (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument('-b', '--baud', type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()

    # 2. Instantiate the Ground Station Class
    gs = VoidGroundStation(port=args.port, baud_rate=args.baud)

    if gs.connect():
        # Start the Serial Listener on a background thread (Non-Blocking)
        listen_thread = threading.Thread(target=gs.listen, daemon=True)
        listen_thread.start()
        
        print("\n💻 CLI Ready. Commands: 'h' (Handshake), 'ack' (Approve Buy), 'exit' (Quit)")
        
        # Main Thread handles User Input
        while True:
            try:
                cmd = input("CLI> ").strip().lower()
                if cmd == 'h':
                    gs.trigger_handshake()
                elif cmd == 'ack':
                    gs.approve_buy()
                elif cmd == 'exit':
                    gs.shutdown()
                    break
            except KeyboardInterrupt:
                gs.shutdown()
                break

if __name__ == "__main__":
    main()