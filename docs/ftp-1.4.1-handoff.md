# DSi Link 1.4.1 candidate — 2026-10-28

**Superseded by 1.4.2. Hardware retry failed:** DSWiFi reported it cannot be
disabled while lwIP is active. The shutdown approach described below is invalid
for this Internet-mode application. Do not deploy 1.4.1 again.

Adds the user-requested X Wi-Fi retry control to the 1.4 FTP candidate. X
explicitly cancels a transfer, closes the FTP/build sockets, disables the radio,
waits four VBlanks, calls `Wifi_Deinit()` and reinitializes with the existing
DSi-mode flags. Connection waits now have a roughly 30-second deadline. Errors
remain on screen and X is available to retry. Completed files and the last-build
path remain intact. TCP listeners request address reuse for reconnects.

The shutdown order follows the pinned SDK contract: disable Wi-Fi, wait at
least a frame or two, then deinitialize. This change handles connection failures
through an explicit user retry; it does not claim to fix their underlying cause.

The user reported that 1.4 was running on DSi at 192.168.1.165. Hardware FTP
checks on that version passed SIZE, NLST, MLSD, rename, download resume,
PASV/EPSV, SHA-256 round trip and ABOR preserving the previous destination.
The uniquely named test directory and all its files were removed, and the FTP
session was closed. On-screen clock/mode and responsiveness were not separately
confirmed by the user. These checks do not establish retry behavior in 1.4.1.

Three checksum-verified 8 MiB round trips on running 1.4:

| Run | Upload MiB/s | Download MiB/s | Upload seconds | Download seconds |
| --- | --- | --- | --- | --- |
| 1 | 0.224 | 0.425 | 35.77 | 18.84 |
| 2 | 0.224 | 0.435 | 35.64 | 18.39 |
| 3 | 0.195 | 0.430 | 41.03 | 18.60 |

Median upload 0.224 MiB/s; median download 0.430 MiB/s. Each benchmark removed
its temporary file. This is an initial hardware baseline, not a comparison
against ftpd or DS mode.

## Candidate verification

Pinned BlocksDS build and the original Python/PowerShell sender checks pass.
The FTP integration suite passes with AddressSanitizer and
UndefinedBehaviorSanitizer after the address-reuse change.

Candidate: `DSiLink-1.4.1-test.nds`, 300,544 bytes.
SHA-256: `2057cb7c20663dc14f11e3b1a5d70631a5157a18b6c8acc17aa726c5ca9f2a36`.
The running 1.4 receiver acknowledged saving this exact candidate as
`Homebrew/Dev/Received/DSiLink-1.4.1-test.nds`: `OK SAVED`, CRC-32 `10A0B93F`.
This also verifies the original build receiver still works after FTP sessions.
No auto-run requested; the user must launch the candidate from Pico Launcher.
Hardware retry acceptance remains pending: launch 1.4.1, press X while idle,
confirm reconnection/IP, then test another FTP session. Repeat after an actual
connection failure when reproducible. Verify cancellation during a test upload
preserves an existing completed file and reconnect succeeds.
See the 1.4 handoff for the FTP design and initial host checks.
