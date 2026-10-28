#!/usr/bin/env python3
"""Exercise the actual FTP C service with ftplib (POSIX host or Docker)."""
import ftplib
import io
import os
from pathlib import Path
import random
import shlex
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def refused(call, code):
    try:
        call()
    except ftplib.error_perm as e:
        assert str(e).startswith(str(code)), str(e)
    else:
        raise AssertionError(f"Expected FTP {code}")


def connect():
    ftp = ftplib.FTP()
    ftp.connect("127.0.0.1", 5000, timeout=10)
    ftp.login()
    return ftp


def main():
    with tempfile.TemporaryDirectory() as temp:
        folder = Path(temp)
        binary = folder / "ftp_host"
        subprocess.run([os.environ.get("CC", "cc"), "-D_DEFAULT_SOURCE", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
                        "-O2", str(ROOT / "source/ftp.c"), str(ROOT / "tests/ftp_host.c"),
                        "-o", str(binary)] + shlex.split(os.environ.get("FTP_TEST_CFLAGS", "")), check=True)
        card = folder / "card"
        card.mkdir()
        app_dir = card / "Homebrew"
        app_dir.mkdir()
        app = app_dir / "DSiLink.nds"
        app.write_bytes(b"running app")
        server = subprocess.Popen([str(binary), str(card), str(app)], stdout=subprocess.PIPE, text=True)
        try:
            assert server.stdout.readline().strip() == "READY"
            ftp = connect()
            assert ftp.pwd() == "/"
            refused(lambda: ftp.storbinary("STOR Homebrew/DSiLink.nds", io.BytesIO(b"bad")), 550)
            refused(lambda: ftp.delete("Homebrew/DSiLink.nds"), 550)
            refused(lambda: ftp.rename("Homebrew", "Moved"), 550)
            refused(lambda: ftp.cwd("fat:/"), 550)
            refused(lambda: ftp.mkd("bad\\path"), 550)
            ftp.cwd("../../../../")
            assert ftp.pwd() == "/"
            ftp.mkd("files")
            ftp.cwd("files")
            payload = random.Random(17).randbytes(512 * 1024 + 17)
            ftp.storbinary("STOR test file.bin", io.BytesIO(payload), blocksize=113)
            assert ftp.size("test file.bin") == len(payload)
            downloaded = bytearray()
            ftp.retrbinary("RETR test file.bin", downloaded.extend, blocksize=131)
            assert downloaded == payload
            tail = bytearray()
            ftp.retrbinary("RETR test file.bin", tail.extend, rest=19999)
            assert tail == payload[19999:]
            assert "test file.bin" in ftp.nlst()
            assert dict(ftp.mlsd())["test file.bin"]["type"] == "file"
            lines = []
            ftp.retrlines("LIST", lines.append)
            assert any("test file.bin" in line for line in lines)
            ftp.sendcmd("MDTM test file.bin")
            assert "type=file;" in ftp.sendcmd("MLST test file.bin")
            ftp.rename("test file.bin", "renamed.bin")
            # Replace through a staged upload; verify old contents are not appended.
            ftp.storbinary("STOR renamed.bin", io.BytesIO(b"replacement"))
            assert (card / "files/renamed.bin").read_bytes() == b"replacement"
            assert not list(card.rglob("*.dsilink-*"))
            # Empty files and EPSV data channels.
            response = ftp.sendcmd("EPSV")
            port = int(response.split("|||")[1].split("|")[0])
            data = socket.create_connection(("127.0.0.1", port))
            ftp.sendcmd("STOR empty.bin")
            data.close()
            assert ftp.voidresp().startswith("226")
            assert ftp.size("empty.bin") == 0
            # ABOR discards partial data and preserves an existing destination.
            data = ftp.transfercmd("STOR renamed.bin")
            data.sendall(b"partial")
            time.sleep(.05)
            assert ftp.sendcmd("ABOR").startswith("226")
            data.close()
            assert (card / "files/renamed.bin").read_bytes() == b"replacement"
            assert not list(card.rglob("*.dsilink-upload"))
            # A TCP reset is an error, not the clean EOF that commits STOR.
            data = ftp.transfercmd("STOR renamed.bin")
            data.sendall(b"incomplete")
            time.sleep(.05)
            data.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            data.close()
            try:
                ftp.voidresp()
            except ftplib.error_temp as e:
                assert str(e).startswith("426"), str(e)
            else:
                raise AssertionError("TCP reset must fail the transfer")
            assert (card / "files/renamed.bin").read_bytes() == b"replacement"
            refused(lambda: ftp.sendcmd("REST -1"), 501)
            refused(lambda: ftp.sendcmd("PORT 127,0,0,1,1,1"), 502)
            refused(lambda: ftp.storbinary("STOR renamed.bin", io.BytesIO(b"bad"), rest=1), 504)
            ftp.delete("renamed.bin")
            ftp.delete("empty.bin")
            ftp.cwd("/")
            ftp.rmd("files")
            # Bounded command parser recovers after an overlong line.
            refused(lambda: ftp.sendcmd("X" * 2048), 500)
            assert ftp.sendcmd("NOOP").startswith("200")
            ftp.sock.sendall(b"NO")
            time.sleep(.01)
            ftp.sock.sendall(b"OP\r\nPWD\r\n")
            assert ftp.getresp().startswith("200")
            assert ftp.getresp().startswith("257")
            # A pre-existing recovery file must never be overwritten or deleted.
            recovery = card / "saved.bin.dsilink-upload"
            recovery.write_bytes(b"recover me")
            refused(lambda: ftp.storbinary("STOR saved.bin", io.BytesIO(b"bad")), 550)
            assert recovery.read_bytes() == b"recover me"
            ftp.quit()
            # Control disconnect during an upload cleans up staging.
            ftp = connect()
            data = ftp.transfercmd("STOR interrupted.bin")
            data.sendall(b"partial")
            time.sleep(.05)
            ftp.close()
            time.sleep(.05)
            data.close()
            assert not (card / "interrupted.bin").exists()
            assert not (card / "interrupted.bin.dsilink-upload").exists()
            ftp = connect()
            assert ftp.sendcmd("NOOP").startswith("200")
            ftp.quit()
            assert app.read_bytes() == b"running app"
            subprocess.run([os.sys.executable, str(ROOT / "host/ftp_benchmark.py"), "127.0.0.1",
                            "--directory", "/", "--mib", "1"], check=True)
            assert not list(card.glob("dsi-link-benchmark-*"))
            print("FTP integration tests passed: transfers, resume, listings, mutations, abort, disconnect, paths and self-protection.")
        finally:
            server.terminate()
            server.wait(timeout=10)


if __name__ == "__main__":
    main()
