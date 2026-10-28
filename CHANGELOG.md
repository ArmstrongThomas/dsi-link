# Changelog

## 1.4.2 — 2026-10-28

### Added

- Passive FTP on port 5000 over the existing BlocksDS DSi-mode connection,
  including saved Advanced Setup WPA/WPA2 networks. Supports directory browsing,
  uploads/downloads, download resume, rename/delete and directory management.
- Staged uploads that sync before replacing a destination, cancellation cleanup
  and protection for the detected application return path. Incomplete recovery
  files are preserved after power loss rather than silently overwritten.
- Nonblocking socket handling, bounded transfer work and cooperative cothread
  yields. One FTP session owns card access at a time; the existing build sender
  remains available after FTP disconnects.
- X to cancel transfers and reconnect Wi-Fi without exiting, with a roughly
  30-second connection timeout and reusable TCP listener addresses.
- Fast DSi ARM9 clock request with on-screen mode/clock reporting.
- FTP integration tests and a checksum-verified throughput benchmark tool.

### Fixed

- Corrected the local 1.4.1 retry candidate, which attempted unsupported Wi-Fi
  teardown while lwIP was active. Reconnection now retains the initialized stack.

### Verified

- Pinned BlocksDS v1.24.0 ROM build; FTP integration tests with AddressSanitizer
  and UndefinedBehaviorSanitizer; existing Python/PowerShell sender checks.
- DSi hardware FTP uploads/downloads, SHA-256 integrity, PASV/EPSV, listings,
  download resume, rename/delete, directory operations and abort preserving the
  previous file. The original build receiver also works after FTP sessions.
- User accepted FileZilla file management and reported responsive navigation.
  X recovered both an idle reconnect and a real disconnect that had prevented
  FileZilla from reconnecting; a subsequent upload completed.
- Three 8 MiB hardware round trips on the 1.4 candidate: median upload
  0.224 MiB/s and download 0.430 MiB/s. No measured comparison with 3DS ftpd.

### Known limitations

- An unexplained disconnect required X to restore FileZilla access; its cause
  and automatic recovery remain unresolved.
- Anonymous plain FTP, passive IPv4 only, one session. No upload resume/append,
  active FTP, TLS or MODE Z. Paths are restricted to printable ASCII.
- Repeated retry cycles, failed initial association recovery and X during an
  active transfer still need hardware checks. Direct Pico Loader boot remains
  experimental and unaccepted.

The FTP service is independently implemented; no ftpd source was copied and
the project retains its Zlib licence. The existing DSLK wire format is unchanged.
