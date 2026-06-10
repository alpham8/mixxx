#!/usr/bin/env python3
"""
OS2L Test Server — empfängt und zeigt OS2L-Nachrichten von Mixxx an.

Registriert sich automatisch als _os2l._tcp Service via Avahi,
lauscht auf TCP Port 9996 und gibt empfangene Beat/Button/Command
Messages in Echtzeit auf der Konsole aus.

Voraussetzungen:
    - avahi-daemon muss laufen (Standard auf den meisten Linux-Distros)
    - avahi-utils installiert (für avahi-publish-service)
    - Python 3.6+

Nutzung:
    python3 tools/os2l_test_server.py

Beenden mit Ctrl+C.
"""

import json
import os
import signal
import socket
import subprocess
import sys
import time

PORT = 9996
SERVICE_NAME = "Mixxx OS2L Test"
SERVICE_TYPE = "_os2l._tcp"


def start_avahi_publish():
    """Registriert einen _os2l._tcp mDNS Service via avahi-publish-service."""
    try:
        proc = subprocess.Popen(
            ["avahi-publish-service", SERVICE_NAME, SERVICE_TYPE, str(PORT)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        time.sleep(0.5)
        if proc.poll() is not None:
            stderr = proc.stderr.read().decode().strip()
            print(f"[WARN] avahi-publish-service fehlgeschlagen: {stderr}")
            print("[WARN] Mixxx muss den Server manuell finden (kein mDNS)")
            return None
        print(f"[OK]   mDNS Service registriert: {SERVICE_NAME} ({SERVICE_TYPE} Port {PORT})")
        return proc
    except FileNotFoundError:
        print("[WARN] avahi-publish-service nicht gefunden")
        print("[WARN] Installiere avahi-utils: sudo zypper install avahi-utils")
        print("[WARN] Server läuft trotzdem — Mixxx muss manuell verbinden")
        return None


def format_beat(msg):
    pos = msg.get("pos", "?")
    bpm = msg.get("bpm", 0)
    change = msg.get("change", False)
    strength = msg.get("strength", 0)

    bar = (pos // 4) + 1 if isinstance(pos, int) else "?"
    beat_in_bar = (pos % 4) + 1 if isinstance(pos, int) else "?"

    indicator = ">>>" if change else "   "
    bar_marker = " |" if isinstance(pos, int) and pos % 4 == 0 else "  "

    return (
        f"  {indicator} BEAT {bar_marker} "
        f"Bar {bar:>3}.{beat_in_bar}  "
        f"BPM {bpm:6.1f}  "
        f"Strength {strength:4.0%}"
    )


def format_button(msg):
    name = msg.get("name", "?")
    state = msg.get("state", "?")
    page = msg.get("page", "")
    page_str = f" (page: {page})" if page else ""
    symbol = "ON " if state == "on" else "OFF"
    return f"  [{symbol}] BTN  {name}{page_str}"


def format_command(msg):
    cmd_id = msg.get("id", "?")
    param = msg.get("param", 0)
    return f"  [CMD] id={cmd_id}  param={param:.1f}%"


def format_feedback(msg):
    name = msg.get("name", "?")
    state = msg.get("state", "?")
    return f"  [FB]  {name} → {state}"


def handle_message(line):
    try:
        msg = json.loads(line)
    except json.JSONDecodeError:
        print(f"  [RAW] {line}")
        return

    evt = msg.get("evt", "unknown")
    if evt == "beat":
        print(format_beat(msg))
    elif evt == "btn":
        print(format_button(msg))
    elif evt == "cmd":
        print(format_command(msg))
    elif evt == "feedback":
        print(format_feedback(msg))
    else:
        print(f"  [???] {msg}")


def run_server():
    avahi_proc = start_avahi_publish()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.settimeout(1.0)

    try:
        srv.bind(("0.0.0.0", PORT))
    except OSError as e:
        print(f"[FEHLER] Port {PORT} belegt: {e}")
        print(f"[TIPP]  Läuft bereits ein OS2L-Server? Prüfe: lsof -i :{PORT}")
        if avahi_proc:
            avahi_proc.terminate()
        sys.exit(1)

    srv.listen(1)
    print(f"[OK]   TCP Server lauscht auf Port {PORT}")
    print()
    print("=" * 60)
    print("  Starte jetzt Mixxx und spiele einen Track ab.")
    print("  Beat-Messages sollten hier erscheinen.")
    print("  Beenden mit Ctrl+C")
    print("=" * 60)
    print()

    def cleanup(signum=None, frame=None):
        print("\n[OK]   Server wird beendet...")
        if avahi_proc:
            avahi_proc.terminate()
            avahi_proc.wait()
        srv.close()
        sys.exit(0)

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    msg_count = 0
    while True:
        try:
            print("[WAIT] Warte auf Verbindung von Mixxx...")
            conn, addr = srv.accept()
            print(f"[OK]   Verbunden: {addr[0]}:{addr[1]}")
            print()

            buffer = ""
            while True:
                try:
                    data = conn.recv(4096)
                except ConnectionResetError:
                    break
                if not data:
                    break
                buffer += data.decode("utf-8", errors="replace")
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if line:
                        handle_message(line)
                        msg_count += 1

            print(f"\n[INFO] Verbindung getrennt ({msg_count} Nachrichten empfangen)")
            print()
            msg_count = 0
            conn.close()

        except socket.timeout:
            continue
        except OSError:
            break

    cleanup()


if __name__ == "__main__":
    print()
    print("  OS2L Test Server für Mixxx")
    print("  ==========================")
    print()
    run_server()
