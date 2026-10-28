# DSi Link 1.5.2 multi-session candidate — 2026-10-28

The user observed FileZilla repeatedly trying another connection while the old
one remained open; X released the old session and allowed recovery. 1.5.1's
immediate rejection exposed the conflict but did not accommodate the workflow.

1.5.2 supports four control sessions. Each owns its directory, authentication,
parser/reply buffers, idle timer, passive/data sockets and queued command.
The main loop polls all sessions with a rotating starting slot. There are no
preemptive threads or filesystem workers running concurrently. One shared
32 KiB buffer services a single active file transfer/listing. Other transfer
commands queue, and unrelated file mutations receive 450 while the card is
owned. ABOR, USER, QUIT or disconnect clears that session's queued command.
An extra connection beyond four receives 421; logs label sessions S1–S4.

This addresses the demonstrated browsing/transfer control-session conflict,
not an established fix for every possible Wi-Fi failure. Large queued transfers
or directory listings can still hit a client's wait timeout. Configure one
simultaneous file transfer and at most four site connections in FileZilla;
the browsing/control connection no longer needs to be evicted with X. The
original build receiver and boot remain blocked while any FTP session is open.

Pinned BlocksDS v1.24.0 build and ASan/UBSan integration checks pass. New tests
cover four successful connections plus immediate fifth-session rejection,
independent directories, a responsive browsing/control connection during an
upload, queued transfer completion, mutation exclusion and queued cancellation.
Existing FTP integrity, abort, resume, log and password-exclusion tests pass.

Candidate `DSiLink-1.5.2-test.nds`: 301,568 bytes.
SHA-256: `4ee6e7ac43427b3d1030e02fbf413b75fce624f9dd998665a024df3a83471da6`.
Receiver 192.168.1.165:17494 acknowledged `OK SAVED`, CRC-32 `8D5CE9A5`,
for `Homebrew/Dev/Received/DSiLink-1.5.2-test.nds`. No auto-run requested.
Hardware multi-session/idle acceptance remains pending: launch, leave browsing
connected and perform transfers/reconnects without X; verify session labels and
that idle control sessions can coexist. Test long waits separately before
claiming general stall recovery.
