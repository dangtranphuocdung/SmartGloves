"""
glove_server.py
---------------
TCP server that receives sensor data from the ESP32 over WiFi,
calls Modal for inference, then executes the mapped action.

Architecture:
  ESP32  ──WiFi TCP──►  this script  ──Modal──►  result  ──►  action on PC
  ESP32  ──BLE HID──►  Windows                               (mouse/keyboard, independent)

Run BEFORE pressing the letter button on the glove:
  python glove_server.py

Find your PC's IP on the hotspot:
  Windows:  ipconfig  → look for "Wireless LAN adapter Wi-Fi: IPv4 Address"
  Then set PC_IP in wifi-tcp.h to that address and reflash the ESP32.

Install:
  pip install modal
"""

import asyncio
import socket
import webbrowser
import os
import modal

# ── Config ────────────────────────────────────────────────────────────────────
HOST        = "0.0.0.0"   # listen on all interfaces
PORT        = 9000        # must match PC_PORT in wifi-tcp.h
MAX_LEN     = 150         # must match training script
BUFFER_SIZE = 4096

# ── Modal inference ───────────────────────────────────────────────────────────
predict_fn = modal.Function.from_name("smart-glove-gestures", "predict_letter")

# ── Actions ───────────────────────────────────────────────────────────────────
ACTIONS = {
    'C': lambda: webbrowser.open("http://google.com"),
    'P': lambda: os.system("start powerpnt"),
    # Add more letters here:
    # 'V': lambda: os.system("start ms-settings:"),
}

SKIP_TOKENS = ('[', 'ets', 'rst:', 'boot:', '╔', '║', '═', 'Waiting', 'MPU')


# ══════════════════════════════════════════════════════════════════════════════
# Per-connection handler
# ══════════════════════════════════════════════════════════════════════════════
async def handle_glove(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    addr = writer.get_extra_info('peername')
    print(f"\n🔌 ESP32 connected from {addr}")

    recording    = []
    is_recording = False
    leftover     = ""

    try:
        while True:
            chunk = await reader.read(BUFFER_SIZE)
            if not chunk:
                break   # ESP32 closed the socket (sent LETTER_MODE_END)

            # Stitch with any leftover fragment from previous TCP segment
            text  = leftover + chunk.decode('utf-8', errors='replace')
            lines = text.split('\n')
            leftover = lines[-1]   # incomplete line — keep for next chunk

            for line in lines[:-1]:
                line = line.strip().rstrip('\r')
                if not line:
                    continue

                # ── Control signals ───────────────────────────────────────
                if line == "LETTER_MODE_START":
                    recording    = []
                    is_recording = True
                    print("⏺  Recording started")
                    continue

                if line == "LETTER_MODE_END":
                    is_recording = False
                    print(f"⏹  Recording ended — {len(recording)} samples")
                    await run_inference(recording)
                    continue

                # ── Skip debug lines ──────────────────────────────────────
                if any(t in line for t in SKIP_TOKENS):
                    continue

                # ── CSV sensor data ───────────────────────────────────────
                if is_recording and ',' in line:
                    try:
                        values = [float(x) for x in line.split(',')]
                        if len(values) == 6:
                            recording.append(values)
                    except ValueError:
                        print(f"⚠️  Bad line: {line[:60]}")

    except asyncio.CancelledError:
        pass
    except Exception as e:
        print(f"❌ Connection error: {e}")
    finally:
        writer.close()
        await writer.wait_closed()
        print(f"🔌 ESP32 disconnected from {addr}")


# ══════════════════════════════════════════════════════════════════════════════
# Inference + action
# ══════════════════════════════════════════════════════════════════════════════
async def run_inference(recording: list):
    captured = list(recording)

    if len(captured) <= 10:
        print("⚠️  Too few samples — ignored.")
        return

    # Pad or truncate to MAX_LEN
    if len(captured) < MAX_LEN:
        captured += [[0.0] * 6] * (MAX_LEN - len(captured))
    else:
        captured = captured[:MAX_LEN]

    print("☁️  Calling Modal inference...")
    try:
        # Run Modal call in a thread so we don't block the event loop
        result = await asyncio.to_thread(predict_fn.remote, captured)

        letter     = result['prediction']
        confidence = result['confidence']
        top3       = result.get('top3', [])

        print(f"\n── Result ─────────────────────────────────")
        print(f"  Detected : {letter}  ({confidence*100:.1f}%)")
        for r in top3:
            bar = '█' * int(r['confidence'] * 20)
            print(f"    {r['letter']}  {bar:<20}  {r['confidence']*100:.1f}%")
        print(f"───────────────────────────────────────────\n")

        if letter in ACTIONS:
            print(f"▶  Executing action for '{letter}'")
            ACTIONS[letter]()
        else:
            print(f"ℹ️  No action mapped for '{letter}'")

    except Exception as e:
        print(f"❌ Modal error: {e}")


# ══════════════════════════════════════════════════════════════════════════════
# Entry point
# ══════════════════════════════════════════════════════════════════════════════
async def main():
    server = await asyncio.start_server(handle_glove, HOST, PORT)

    addrs = [str(s.getsockname()) for s in server.sockets]
    print("╔═══════════════════════════════════════════╗")
    print("║        GLOVE TCP SERVER RUNNING           ║")
    print("╠═══════════════════════════════════════════╣")
    print(f"║  Listening on port {PORT}                   ║")
    print("║                                           ║")
    print("║  To find your PC IP on the hotspot:       ║")
    print("║    Windows: ipconfig                      ║")
    print("║    Look for: Wireless LAN IPv4 Address    ║")
    print("║  Then set PC_IP in wifi-tcp.h + reflash  ║")
    print("╠═══════════════════════════════════════════╣")
    print("║  Waiting for ESP32 glove to connect...    ║")
    print("╚═══════════════════════════════════════════╝\n")

    # Also print local IPs to help the user
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    print(f"  Detected local IP: {local_ip}")
    print(f"  Set PC_IP \"{local_ip}\" in wifi-tcp.h\n")

    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n🛑 Server stopped.")