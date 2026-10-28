#!/usr/bin/env python3
"""Round-trip a uniquely named temporary file and report verified FTP throughput."""
import argparse
import ftplib
import hashlib
import io
import secrets
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--mib", type=int, default=8)
    parser.add_argument("--directory", default="/Homebrew/Dev/Received")
    args = parser.parse_args()
    if not 1 <= args.mib <= 128:
        parser.error("--mib must be 1 through 128")
    payload = secrets.token_bytes(args.mib * 1024 * 1024)
    expected = hashlib.sha256(payload).digest()
    name = "dsi-link-benchmark-" + secrets.token_hex(12) + ".bin"
    with ftplib.FTP() as ftp:
        ftp.connect(args.address, args.port, timeout=60)
        ftp.login()
        ftp.cwd(args.directory)
        uploaded = False
        try:
            start = time.perf_counter()
            ftp.storbinary("STOR " + name, io.BytesIO(payload), blocksize=32768)
            upload_seconds = time.perf_counter() - start
            uploaded = True
            received = hashlib.sha256()
            count = 0

            def consume(data):
                nonlocal count
                received.update(data)
                count += len(data)

            start = time.perf_counter()
            ftp.retrbinary("RETR " + name, consume, blocksize=32768)
            download_seconds = time.perf_counter() - start
            if count != len(payload) or received.digest() != expected:
                raise RuntimeError("Round-trip size or SHA-256 mismatch")
            print(f"Verified {args.mib} MiB round trip (SHA-256).")
            print(f"Upload:   {args.mib / upload_seconds:.3f} MiB/s ({upload_seconds:.2f} s)")
            print(f"Download: {args.mib / download_seconds:.3f} MiB/s ({download_seconds:.2f} s)")
        finally:
            if uploaded:
                ftp.delete(name)


if __name__ == "__main__":
    main()
