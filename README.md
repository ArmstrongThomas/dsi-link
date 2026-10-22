# DSi Link

Send homebrew builds to a Nintendo DSi over Wi-Fi, the way `3dslink` works for
the 3DS. DSi Link saves the received `.nds` to `Homebrew/Dev/` on your
flashcart and boots it through [Pico Loader](https://github.com/LNH-team/pico-loader).
When you exit the booted app you land back in DSi Link, ready for the next
build. No more pulling the SD card out of the cart.

- Works with WPA/WPA2 networks saved in the DSi's Advanced Setup (connections
  4–6), because it runs in DSi mode with BlocksDS's DSWiFi.
- Each file arrives as `<name>.part`, is checked for size and CRC-32, and only
  then replaces `<name>`. The previous copy is kept as `<name>.bak`.
- Senders for Windows (PowerShell) and any system with Python 3.8+.

> **Status:** the transfer and save path are covered by host tests. Booting
> through Pico Loader has not yet been confirmed on hardware; if it misbehaves
> on your console, send with `--no-run` / `-NoRun` and start the file from
> Pico Launcher.

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
   `Homebrew/Dev/`, wherever DSi Link itself lives.

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

Options: `--name` / `-Name` saves under another file name, `--no-run` /
`-NoRun` saves without booting, `--port` / `-Port` changes the port.

On the DSi, **A** boots the last received file again and **START** exits to
Pico Launcher.

## Safety

DSi Link listens on TCP 17494 only while it is open, and there is no
password, so use it on a network you trust. It writes only plain `.nds` file
names (letters, digits, space, dot, dash, underscore; up to 63 characters)
into `Homebrew/Dev/`, files up to 32 MiB, and refuses to overwrite itself.

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
```

This builds `tests/link_check.c` with any available C compiler (gcc, clang, cc
or Visual Studio), runs the wire-format self-test, and sends a file from each
available sender to a stand-in receiver on 127.0.0.1. It then checks the
captured bytes against the receiver's own parser.

## Licence

Zlib; see [LICENSE](LICENSE). The Pico Loader header layout in
`source/main.c` is transcribed from Pico Loader (Zlib, LNH team).
