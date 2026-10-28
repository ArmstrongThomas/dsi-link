# DSi Link 1.4.2 — retry correction, 2026-10-28

1.4.1 failed hardware retry with the reported message that DSWiFi cannot be
disabled while lwIP is active. It is superseded; do not deploy it again.
The SDK Internet-mode guide explicitly states that `Wifi_Deinit()` does not
currently work after Internet-mode initialization. The 1.4.1 header-only review
missed that limitation.

1.4.2 closes application sockets and cancels incomplete transfers, calls
`Wifi_DisconnectAP()`, yields four frames and calls `Wifi_AutoConnect()` on the
existing initialized stack. It does not call `Wifi_DisableWifi()` or
`Wifi_Deinit()`. Initial initialization failures still retry initialization.
The 30-second association deadline remains. The experimental boot path also no
longer attempts unsupported full shutdown; direct boot remains unaccepted.

The user returned to running 1.4 at 192.168.1.165 for candidate delivery.
Build, delivery and hardware retry results will be recorded below.

Pinned BlocksDS v1.24.0 build passes with warnings treated as errors. Source
inspection confirms no remaining `Wifi_DisableWifi` / `Wifi_Deinit` calls.
Candidate `DSiLink-1.4.2-test.nds`: 299,520 bytes.
SHA-256: `9b90361718f5f916e5d8309801bdcc9b7d231313084363345d539e1460b5e00a`.
The running 1.4 receiver acknowledged `OK SAVED`, CRC-32 `046F024B`, for
`Homebrew/Dev/Received/DSiLink-1.4.2-test.nds`. No auto-run requested.
The user confirmed X reconnected successfully on 1.4.2 with the same IP,
192.168.1.165. The subsequent hardware FTP check passed upload, SIZE, NLST,
MLSD, rename, download resume, EPSV, SHA-256 round-trip verification, ABOR
preserving the previous destination, deletion and directory removal. The test
used 131,089 random bytes under a uniquely named temporary directory in
`Homebrew/Dev/Received`; all test files and the directory were removed and the
FTP session was closed. This accepts one idle manual reconnect followed by
working FTP on the DSi. Recovery after a genuine failed association, repeated
retry cycles and pressing X during an active transfer remain unverified.

Further user acceptance on 2026-10-28: FileZilla feels responsive for directory
navigation and file management. The user cleaned the Received directory, moved
PixelDoodle NDS files from Received to `/homebrew/`, uploaded the 1.4.2 test
binary to `/devtools/`, and performed additional file operations successfully.
An unexplained disconnect left FileZilla unable to reconnect; pressing X on the
DSi restored the service and FileZilla recovered and uploaded the file. This
accepts manual X recovery after a real disconnect, but does not identify or fix
the original disconnect/reconnect failure. User described directory navigation
as noticeably more responsive than 3DS ftpd, clarifying that the initial "10x"
was an exaggeration; no controlled comparison was performed. Next investigation:
detect lost/stale sessions so recovery
does not require X. Do not assume the uploaded copy became the running path.
