# DSi Link

Send homebrew builds to a Nintendo DSi over Wi-Fi, the way `3dslink` works for
the 3DS. DSi Link saves the received `.nds` to `Homebrew/Dev/Received/` on your
flashcart; start it from Pico Launcher. No more pulling the SD card out of the
cart.

- Works with WPA/WPA2 networks saved in the DSi's Advanced Setup (connections
  4–6), because it runs in DSi mode with BlocksDS's DSWiFi.
- Each file arrives as `<name>.part`, is checked for size and CRC-32, and only
  then replaces `<name>`. The previous copy is kept as `<name>.bak`.
- Senders for Windows (PowerShell) and any system with Python 3.8+.
- Version 1.4 adds a passive FTP server on port 5000 for general file transfers,
  using the same DSi-mode Wi-Fi connection as the build receiver.

> **Status:** sending and saving work on a DSi LL with a DSpico (Wi-Fi in
> Advanced Setup, 2026-10-21). Booting the received file straight from DSi Link
> through [Pico Loader](https://github.com/LNH-team/pico-loader) is experimental
> and does not work yet, so senders only save by default. Findings so far:
> [docs/boot-handoff-notes.md](docs/boot-handoff-notes.md).

## Requirements

- A DSi with a flashcart running Pico Launcher / Pico Loader (developed for the
  DSpico). `/_pico/picoLoader9.bin` and `/_pico/picoLoader7.bin` must be on the
  card, which a working Pico Launcher setup already has.
- A Wi-Fi connection saved in the DSi's System Settings.
- To build: [BlocksDS](https://blocksds.skylyrac.net/), or Docker.

## Install

1. Build `DSiLink.nds`:
   - with BlocksDS installed: `make`
   - with Docker on Windows: `powershell -ExecutionPolicy Bypass -File scripts\build-docker.ps1`
   - with Docker on macOS or Linux: `sh scripts/build-docker.sh`
2. Copy `DSiLink.nds` to the card once by hand, for example to
   `Homebrew/DevTools/DSiLink.nds`. Received builds always go to
   `Homebrew/Dev/Received/`, wherever DSi Link itself lives, so a send never
   replaces the working copies you keep in `Homebrew/Dev/`.

DSi Link shows where booted apps will return to when you exit them. It uses
the path Pico Loader passes it; if none arrives, it looks for itself in
`Homebrew/Dev`, `Homebrew/DevTools`, `Homebrew` and the card root, and
otherwise returns to Pico Launcher (`/_picoboot.nds`).

## Send a build

Start DSi Link from Pico Launcher. It connects to your saved Wi-Fi and shows
an address and port, for example `192.168.1.50 : 17494`.

Python (any OS):

```bash
python3 host/dsi_send.py 192.168.1.50 path/to/game.nds
```

PowerShell (Windows):

```bash
powershell -ExecutionPolicy Bypass -File host/dsi-send.ps1 -Address 192.168.1.50 -File path\to\game.nds
```

Then press **START** on the DSi to exit to Pico Launcher and open the file
from `Homebrew/Dev/Received/`.

In **1.4.2**, press **X** to retry Wi-Fi without exiting. This cancels any
active build/FTP transfer, closes the listeners, disconnects from the AP, waits
four frames and reconnects to saved settings using the initialized stack. Incomplete
uploads are discarded; completed files remain. Initialization/association
errors keep this control available. A connection attempt times out after about
30 seconds instead of waiting indefinitely. The IP may change after reconnect;
use the newly displayed address. On 2026-10-28, the user confirmed one idle X
reconnect on DSi with the same IP; subsequent hardware FTP upload/download,
resume, listings, rename/delete and transfer-abort checks passed. Recovery after
a failed initial association, repeated retries and X during a transfer remain
unverified. The user also accepted FileZilla navigation, uploads, moving files
between directories and cleanup. After a real disconnect prevented FileZilla
from reconnecting, X restored the service and the upload completed. The cause of
that disconnect and the need for manual recovery remain unresolved.

Do not use 1.4.1: its retry attempted unsupported lwIP teardown and failed on
hardware. 1.4.2 keeps lwIP and the DSi Wi-Fi driver initialized during retry.

Options: `--name` / `-Name` saves under another file name, `--port` / `-Port`
changes the port. `--run` / `-Run` also asks DSi Link to boot the file through
Pico Loader (experimental). Before booting, DSi Link shows a boot check screen
with the card driver, Pico Loader version and paths; **A** boots, **B**
cancels. On the main screen **A** tries to boot the last received file the
same way.

## Safety

DSi Link listens on TCP 17494 only while it is open, and there is no
password, so use it on a network you trust. It writes only plain `.nds` file
names (letters, digits, space, dot, dash, underscore; up to 63 characters)
into `Homebrew/Dev/Received/`, files up to 32 MiB, and refuses to overwrite
itself.

## FTP (1.4)

Connect FileZilla, WinSCP or another FTP client to the displayed IP, port
**5000**, using **plain FTP**, **passive mode**, username **anonymous** and
any password. Limit the client to **one connection**. Disconnect the FTP
session before using the build sender or booting the last build.

FTP exposes the entire flashcart mounted as `fat:`, with `/` as its root.
Unlike the build receiver, it can upload any file type, download, create and
remove directories, rename and delete files. It protects the detected DSi Link
return path and its parent directories. It does not export the DSi internal
NAND or a separately mounted SD device. FTP login is anonymous; passwords and
files are not encrypted. Use a trusted LAN.

Uploads go to a sibling `<name>.dsilink-upload`, are flushed and synced, then
replace the destination. Replacement briefly saves the old file as
`<name>.dsilink-backup`; a failed replacement attempts to restore it. Cancel,
socket errors and control disconnect remove the incomplete staging file.
FTP cannot verify a sender checksum or detect a sender that deliberately
closes its data connection early: a normal data EOF finishes the upload.
Use the build sender when its known-size/CRC-32 validation matters.
After power loss, inspect any `.dsilink-upload` / `.dsilink-backup` files on
the card locally. FTP hides these reserved files and refuses a new upload
when either exists, rather than discarding recovery data.

Supported operations include PASV/EPSV, LIST/NLST/MLSD/MLST, RETR, STOR,
download resume (REST), SIZE/MDTM, CWD/CDUP/PWD, MKD/RMD, DELE, RNFR/RNTO,
ABOR, FEAT and NOOP. Paths are printable ASCII; device prefixes, backslashes,
wildcards and trailing dots/spaces are rejected. There is no active FTP
(PORT/EPRT), upload resume/append, MODE Z, TLS or parallel-client support yet.
`TYPE A` is accepted for listings, but all file bytes are preserved unchanged.

### Scheduling and performance

The ARM7 runs the SDK Wi-Fi core while ARM9 handles FTP and card I/O.
On ARM9, FTP uses nonblocking sockets and bounded polling, yields to BlocksDS
cothreads every four transfer iterations, then returns to the main loop after
at most sixteen. Directory scans are bounded too. This is cooperative
concurrency, not preemptive POSIX threading. FTP sessions and the original
receiver serialize card access; booting is disabled during an FTP session.
Idle sessions/data waits expire after 120 seconds without activity.

DSi mode explicitly requests the normal fast ARM9 clock (134.06 MHz); the
screen reports the clock register state. DS fallback remains at its normal
clock and only supports open/WEP networks. The existing BlocksDS DSi Wi-Fi
driver enables Advanced Setup WPA/WPA2 connections. FTP uses a 32 KiB transfer
buffer and retains unsent bytes after partial socket writes. Actual throughput
depends on Wi-Fi, the driver and flashcart writes; no speedup is claimed yet.

Measure on the DSi with a temporary file (removed after successful upload and
download, including checksum comparison):

```bash
python3 host/ftp_benchmark.py 192.168.1.50 --mib 8
```

Repeat on the same console, card, AP and distance for comparisons. Hardware
testing now covers FTP transfers and FileZilla file management, plus successful
X reconnect/recovery on 1.4.2. The mode/clock display and long-term stability
have not been separately confirmed. See [CHANGELOG.md](CHANGELOG.md) and
[the 1.4.2 handoff](docs/ftp-1.4.2-handoff.md) for results and remaining checks.

## How booting works

DSi Link follows Pico Launcher's `picoLoaderBootstrap`. It loads both Pico
Loader binaries into VRAM, fills the `picoLoader7.bin` header with the ROM
path, argv and return path, and starts both CPUs. Pico Launcher uses its own
ARM7 code for the last step. DSi Link keeps BlocksDS's stock ARM7 core
(needed for Wi-Fi), so it points the header's DLDI driver at its own patched
DLDI stub and starts the ARM7 with libnds `resetARM7()`.

## Protocol

Documented in [`source/link_protocol.h`](source/link_protocol.h): a 20-byte
little-endian header (`DSLK`, flags, size, CRC-32, name length), the file
name, a `READY` reply, the file bytes, then an `OK ...` or `ERR ...` line.

## Tests

```bash
python3 tests/run_tests.py
# POSIX host with cc, or inside a Linux development container:
python3 tests/test_ftp.py
```

This builds `tests/link_check.c` with any available C compiler (gcc, clang, cc
or Visual Studio), runs the wire-format self-test, and sends a file from each
available sender to a stand-in receiver on 127.0.0.1. It then checks the
captured bytes against the receiver's own parser.

## Licence

Zlib; see [LICENSE](LICENSE). The Pico Loader header layout in
`source/main.c` is transcribed from Pico Loader (Zlib, LNH team).
