# DSi Link 1.5.1 diagnostics candidate — 2026-10-28

The user accepted 1.5's activity console but reported intermittent idle-like
stalls after roughly five seconds, recovered with X. FileZilla had no explicit
connection limit. This does not match the server's 120-second idle timeout.

Tests against running 1.5 at 192.168.1.165:
- One FTP session passed three NOOP checks separated by six-second idle periods.
- A second connection while the first was open timed out after five seconds
  without a greeting. The first session still answered NOOP. Both were closed.
  This reproduces a silent backlog wait compatible with the reported symptom,
  but does not prove the cause of every intermittent stall or Wi-Fi failure.

1.5.1 accepts/rejects extra connections promptly with FTP 421 while preserving
the existing session. Rejections are logged, directing the user to set the site
connection limit to one. It also enables control TCP keepalive (30/5/3 seconds,
interval, probe count) and adds explicit QUIT/EOF/error/idle-timeout diagnostics.
The application idle timeout remains 120 seconds. This does not add parallel
sessions or reset Wi-Fi automatically on an idle session.

The top log uses 23 rows of 31 columns with no footer. Its controls hint is on
the bottom screen, and the bottom-right top-console cell is never written,
avoiding automatic console scroll when printing a full-width last line.

Pinned BlocksDS build and ASan/UBSan host tests pass, including extra-connection
rejection while the primary session remains usable, log wrapping and password
exclusion. Hardware behavior of this candidate still requires acceptance.

Candidate `DSiLink-1.5.1-test.nds`: 302,592 bytes.
SHA-256: `55ba1782cefbe19995a65fa84435828fb5ce72d0f34a27099c0ef562aeadbf1a`.
The existing build receiver at 192.168.1.165:17494 acknowledged `OK SAVED`,
CRC-32 `A2F92BCA`, to `Homebrew/Dev/Received/DSiLink-1.5.1-test.nds`.
No auto-run requested. Launch manually, configure one FileZilla connection,
check the last log row, then repeat idle/reconnect operations. Do not describe
the reported stall as fixed until those hardware checks establish it.
