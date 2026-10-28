# DSi Link 1.5.3 recovery candidate — 2026-10-28

User photos of 1.5.2 show a control receive failure (113), disconnect, and zero
FTP clients while FileZilla times out on new connections. The pinned newlib
headers identify 113 as ECONNABORTED, not Linux's EHOSTUNREACH mapping. The user
confirmed the device remained in that stalled state. Probes outside the sandbox
to TCP 5000 and 17494 both timed out; ping timed out / reported host unreachable.
This establishes loss of both services in the observed state, not just an FTP
control-session capacity problem. It does not identify the driver/AP/root cause.

The preceding session-capacity change therefore does not resolve all stalls.
The 530 log entries may be FileZilla's pre-login TLS probes; 1.5.3 explicitly
returns 502 for AUTH/PBSZ/PROT instead of requiring login for unsupported TLS.

1.5.3 requests recovery on ECONNABORTED, ENETDOWN, ENETUNREACH or ENETRESET
from control send/receive. Ordinary ECONNRESET/ETIMEDOUT, EOF/QUIT and the
120-second idle deadline do not restart Wi-Fi. Recovery waits for all active
file transfers and queued protocol replies to complete. Main uses the same
socket cleanup, AP disconnect, four-frame wait and autoconnect as X, keeping
lwIP initialized. Manual/automatic reconnect sets a 30-second cooldown; a
failed reconnect stays on the error screen with X available. A silent network
drop without a qualifying control error still requires X. This is a bounded
workaround, not proof that every abort represents a local link failure.

Pinned BlocksDS build passes. Linux tests inject ECONNABORTED into a chosen
control socket and verify recovery readiness, preservation/completion reply for
another session's upload and exclusion of an ordinary peer reset. The host
harness simulates listener restart only; it does not emulate radio recovery.
All FTP/event-log integration checks pass with ASan/UBSan. Test-only receive
substitution disables host fortify recv rewriting so the fault reaches the
instrumented service; this does not change native production build settings.

Candidate `DSiLink-1.5.3-test.nds` SHA-256:
`30b3c1323f93f8ae7f0fff211cb17000094aece6e5b52476463fa30fdabe5ca2`.
The user confirmed manual X reconnected the stalled device. The exact candidate
was then sent to 192.168.1.165:17494 as
`Homebrew/Dev/Received/DSiLink-1.5.3-test.nds`. The receiver acknowledged
`OK SAVED`, 301,568 bytes, CRC-32 `8B546B24`. No auto-run requested.
The user subsequently reported 1.5.3 feels good in FileZilla and has required
no X reconnect or other console intervention during use. This accepts the
reported normal-use experience, without establishing the duration of the run
or whether an automatic recovery actually occurred. Explicit observation of
automatic recovery after a reproduced network stall remains pending.

The user then supplied a FileZilla log showing successful directory/file moves,
directory creation and repeated listings, plus four successful uploads of
1,509, 836, 15,043 and 9,404 bytes. Separate transfer connections logged in and
completed uploads while browsing continued. Disconnections and one "Connection
closed by server" were followed by successful connection/login and listing;
the excerpt contains no failed transfer or connection timeout. The FileZilla
log alone does not distinguish client session cleanup, server idle expiration
or an automatic radio recovery, so the cause of those closes is not asserted.
