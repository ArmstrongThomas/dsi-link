# Changelog

## 1.5.3 — 2026-10-28 (candidate)

- Request automatic Wi-Fi recovery after an aborted control connection or an
  explicit network-down/unreachable/reset socket error. Wait for other active
  transfers and queued replies to finish, then use the accepted X reconnect
  path with a 30-second cooldown. No unsupported lwIP teardown.
- Ordinary peer resets, peer timeouts, QUIT and idle expiration do not trigger
  radio recovery. Failed reconnect attempts remain available for manual X retry.
- Decode control errors in the log. Pinned newlib error 113 is ECONNABORTED.
- Reject AUTH/PBSZ/PROT explicitly as unsupported TLS operations before login,
  instead of the misleading "Log in first" reply to FileZilla's probes.
- Linux fault-injection tests cover recovery requests, transfer/reply deferral
  and normal-reset exclusion under ASan/UBSan. The user reports successful
  FileZilla use without manual X recovery on 1.5.3. A confirmed automatic
  recovery after a reproduced hardware stall remains pending.

## 1.5.2 — 2026-10-28 (candidate)

- Allow four independent FTP control sessions so FileZilla browsing and transfer
  connections can coexist without the single-session rejection/retry loop.
- Rotate bounded polling across sessions; queue data transfers and listings
  behind one shared 32 KiB buffer/card worker. File mutations from other
  sessions receive 450 during a transfer. Abort/disconnect cancels queued work.
- Keep directories, login/parser/reply state and passive sockets per session;
  label log entries S1–S4. A fifth connection receives immediate FTP 421.
- Host/sanitizer tests cover independent directories, browsing/control traffic
  during an upload, queued transfers, queued cancellation and capacity limits.
- Hardware acceptance remains pending. This addresses the demonstrated session
  conflict, not every possible Wi-Fi disconnect. For large files, use one
  simultaneous transfer in FileZilla to avoid its timeout while queued.

## 1.5.1 — 2026-10-28 (candidate)

- Reply immediately with FTP 421 to extra control connections while the single
  session is occupied, and log the rejection. Previously they sat silently in
  the listener backlog. A hardware check reproduced a five-second greeting
  timeout on a second connection while the first remained responsive.
- Enable TCP keepalive on control sessions (30-second idle, five-second probe
  interval, three probes), logging any unsupported option. This helps detect
  dead peers; it is separate from the unchanged 120-second application timeout.
- Distinguish client QUIT, control EOF, socket errors and idle timeout in logs.
- Move the log navigation hint to the bottom screen. Use 23 top-screen log
  rows and 31 columns, avoiding the bottom-right cell's console auto-scroll.
- Host/sanitizer tests cover extra-connection rejection without disrupting the
  original session. The reported intermittent hardware stall is not yet proven
  resolved; FileZilla must limit the site to one simultaneous connection.

## 1.5 — 2026-10-28 (candidate)

- Dedicated top-screen activity console with 128 lines of bounded in-memory
  history, wrapped paths, Up/Down scrolling and Y to follow the latest events.
- Log Wi-Fi connection attempts/results and manual retry, FTP connections,
  directory changes/listings, file operations, transfer completion/cancellation
  and errors. Build-receiver messages share the same log, including file names,
  incoming sizes and checksum-verified saves.
- Keep status/controls on the bottom screen; refresh the log only when dirty
  and at most ten times per second. No per-packet or progress-message flood.
- Sanitize log control characters and exclude passwords and routine metadata
  polling. Logs stay in RAM and are not written to the card.
- Host checks cover wrapping, history rollover, FTP event coverage and password
  exclusion. Hardware layout/scrolling and transfer responsiveness remain pending.

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
