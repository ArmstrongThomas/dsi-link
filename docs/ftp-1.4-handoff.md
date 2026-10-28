# DSi Link 1.4 FTP candidate — 2026-10-28

The local worktree was clean before this change. No GitHub writes were
performed. The existing Pico Loader direct-boot problem remains
outside this change.

On 2026-10-28, the user authorized hardware testing at 192.168.1.165. The exact
candidate below was sent through the existing DSLK receiver on TCP 17494 as
`Homebrew/Dev/Received/DSiLink-1.4-test.nds` (save only). The device acknowledged
`OK SAVED`, 298,496 bytes, CRC-32 `BF4843C6`. This confirms transfer/storage
acknowledgment, not execution or FTP hardware acceptance. The running app was
not replaced; launch the saved candidate manually from Pico Launcher.

## Upstream assessment

Checked [mtheall/ftpd](https://github.com/mtheall/ftpd) and its current
[NDS platform source](https://github.com/mtheall/ftpd/blob/master/source/nds/platform.cpp).
ftpd does have an NDS build. Its platform calls `Wifi_InitDefault(INIT_ONLY)`
and builds against devkitPro DSWiFi; it does not provide the BlocksDS DSi-mode
initialization required here for Advanced Setup WPA/WPA2 connections. Running
an NDS binary on a DSi is distinct from enabling the DSi Wi-Fi driver.

[BlocksDS's Internet-mode guide](https://blocksds.skylyrac.net/dswifi/md_documentation_2internet__mode.html)
documents `WIFI_ATTEMPT_DSI_MODE`; dsi-link already used this before 1.4.
The pinned SDK's `nds/system.h` documents `setCpuClock(true)` as 134.06 MHz.
1.4 requests that clock only in DSi mode and displays the register state.

The FTP module is an independent C implementation of protocol behavior, not a
copy/port of ftpd source. dsi-link retains its Zlib licence. This candidate
implements the common passive file-management operations, not all ftpd features.

## Implementation and verification

- FTP TCP 5000 alongside the unchanged DSLK TCP 17494 wire format. Version 1.4
  is a client label change, not a build-sender protocol change.
- Single anonymous FTP control session, passive IPv4 data sockets, card root
  `fat:`, cooperative ARM9 service with periodic cothread yields. No pthreads
  or simultaneous filesystem workers. The SDK ARM7 Wi-Fi core remains intact.
- FTP sessions exclude build-receiver processing and boot. A queued sender can
  time out while FTP owns the card; disconnect FTP first.
- Staged/synced uploads with rollback attempt, reserved recovery filenames,
  normalized paths, detected return-path/ancestor protection, bounded control
  and data buffers, idle deadline and matching control/data peer addresses.
- Host FTP integration suite passes against the actual C service with Python
  ftplib, including round-trip bytes, download resume, directory operations,
  metadata, EPSV/PASV, abort, reset/disconnect cleanup, parser recovery,
  recovery-file preservation and return-path protection.
- The same suite passes with AddressSanitizer and UndefinedBehaviorSanitizer.
- Existing receiver parser/CRC checks and Python/PowerShell loopbacks pass.
- Pinned BlocksDS v1.24.0 Docker build passes with warnings treated as errors.
- Candidate `DSiLink.nds`: 298,496 bytes, DSi-enhanced unit code `0x02`.
  SHA-256: `b676ce505a4751e2fc435c0179cae57e1291771faf155155c96a70447c84d9cc`.
- `host/ftp_benchmark.py` passes a 1 MiB checksum-verified round trip against
  the host service. Host timing is not a DSi throughput measurement.

See README for supported operations and limitations, including no TLS,
active FTP, upload resume/append, compression or multiple FTP sessions.

## Hardware acceptance still needed

Launch the exact candidate in DSi mode on DSi LL/DSpico. Confirm the mode/clock
line and connection through a saved Advanced Setup network. Use one passive
FTP connection to browse, round-trip a temporary file, rename/delete it, cancel
an upload and reconnect. Check controls remain responsive during transfers.
Close FTP and confirm an existing build sender still saves a CRC-verified NDS.
Run the 8 MiB benchmark three times with the same card, AP and distance; compare
upload/download medians before making performance claims. Test long transfers
and the fast clock with the actual DLDI card driver. No hardware pass or speed
improvement is claimed by the host tests and successful ROM build.
