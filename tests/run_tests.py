#!/usr/bin/env python3
"""Host tests for DSi Link: wire-format self-test, then a loopback send from each
available sender, checked byte-for-byte against the receiver's parser."""
import os
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "Loopback Test.nds"


def build_checker(out_dir):
    source = os.path.join(ROOT, "tests", "link_check.c")
    exe = os.path.join(out_dir, "link_check.exe" if os.name == "nt" else "link_check")
    for compiler in ("cc", "gcc", "clang"):
        if shutil.which(compiler):
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", source, "-o", exe], check=True)
            return exe
    if os.name == "nt":
        vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", ""), "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if os.path.exists(vswhere):
            vs = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                                 "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                capture_output=True, text=True, check=True).stdout.strip()
            setup = os.path.join(vs, "Common7", "Tools", "VsDevCmd.bat")
            # A batch file avoids Python's argument quoting, which cmd.exe does not understand.
            batch = os.path.join(out_dir, "build.bat")
            with open(batch, "w") as f:
                f.write(f'@call "{setup}" -arch=x64 >nul\r\n'
                        f'@cl /nologo /W3 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS "{source}" /Fo"{out_dir}\\\\" /Fe"{exe}"\r\n')
            subprocess.run(["cmd", "/d", "/c", batch], check=True, stdout=subprocess.DEVNULL)
            return exe
    sys.exit("No C compiler found (tried cc, gcc, clang and Visual Studio).")


def read_exact(conn, count):
    data = bytearray()
    while len(data) < count:
        piece = conn.recv(count - len(data))
        if not piece:
            raise RuntimeError("sender closed early")
        data += piece
    return bytes(data)


def loopback(sender, payload_path, out_dir, label):
    """Runs one sender against a stand-in receiver and returns the captured header path."""
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    server.settimeout(30)
    port = server.getsockname()[1]
    captured = {}

    def receive():
        conn, _ = server.accept()
        with conn:
            conn.settimeout(15)
            head = read_exact(conn, 20)
            name = read_exact(conn, struct.unpack_from("<H", head, 16)[0])
            conn.sendall(b"READY\n")
            captured["payload"] = read_exact(conn, struct.unpack_from("<I", head, 8)[0])
            captured["head"] = head + name
            conn.sendall(b"OK SAVED\n")

    thread = threading.Thread(target=receive)
    thread.start()
    result = subprocess.run(sender(port), capture_output=True, text=True, timeout=60)
    thread.join(timeout=30)
    server.close()
    if result.returncode != 0 or "OK SAVED" not in result.stdout:
        sys.exit(f"{label} sender failed:\n{result.stdout}\n{result.stderr}")
    with open(payload_path, "rb") as f:
        if captured.get("payload") != f.read():
            sys.exit(f"{label} sender: received bytes differ from the file")
    if struct.unpack_from("<I", captured["head"], 4)[0] != 0:
        sys.exit(f"{label} sender: --no-run did not clear the run flag")
    head_path = os.path.join(out_dir, f"header-{label}.bin")
    with open(head_path, "wb") as f:
        f.write(captured["head"])
    return head_path


def main():
    with tempfile.TemporaryDirectory() as out_dir:
        checker = build_checker(out_dir)
        subprocess.run([checker], check=True)
        payload = os.path.join(out_dir, "payload.nds")
        rng = random.Random(7)
        with open(payload, "wb") as f:
            f.write(bytes(rng.randrange(256) for _ in range(70001)))
        senders = {"python": lambda port: [sys.executable, os.path.join(ROOT, "host", "dsi_send.py"), "127.0.0.1",
                                           payload, "--port", str(port), "--name", NAME, "--no-run"]}
        shell = shutil.which("pwsh") or shutil.which("powershell")
        if shell:
            senders["powershell"] = lambda port: [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                                  os.path.join(ROOT, "host", "dsi-send.ps1"), "-Address", "127.0.0.1",
                                                  "-Port", str(port), "-File", payload, "-Name", NAME, "-NoRun"]
        else:
            print("PowerShell not found; skipping dsi-send.ps1 loopback")
        for label, sender in senders.items():
            subprocess.run([checker, loopback(sender, payload, out_dir, label), payload, NAME], check=True)
    print("All DSi Link host tests passed.")


if __name__ == "__main__":
    main()
