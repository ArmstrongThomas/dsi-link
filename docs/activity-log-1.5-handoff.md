# DSi Link 1.5 activity-log candidate — 2026-10-28

The top main-engine text console now displays application/FTP events. The
existing sub-engine bottom console keeps status and controls. There are 22
visible log lines, a fixed heading and a scroll-control hint. The last 128
32-column lines stay in a bounded RAM ring. Paths wrap, control characters are
replaced, Up/Down scroll and Y returns to following the latest events. Log
redraws are dirty-driven and capped at one every six main-loop frames.

FTP uses an optional callback independent of the DS console. Events include
listener/client lifecycle, control socket errors/timeouts, directory changes,
listings, transfer start/result/cancellation, rename, mkdir/rmdir and delete.
Protocol errors are included except unsupported optional-settings chatter.
Passwords and raw authentication commands are never passed to the callback;
routine NOOP/metadata polling is omitted. No packet-level or progress spam.
Build-receiver status/messages use the same ring, including incoming names and
sizes, failure reasons and CRC-verified saves. Wi-Fi status and the connected
IP appear in the log. This does not fix the unresolved disconnect issue.

Verification: pinned BlocksDS v1.24.0 build passes. Host integration tests pass
with AddressSanitizer/UndefinedBehaviorSanitizer, including log wrapping,
sanitization, rollover, FTP event coverage and password exclusion. Existing
Python and PowerShell build-sender loopbacks pass. No hardware layout or
scrolling/performance acceptance is claimed yet.

Candidate `DSiLink-1.5-test.nds`: 301,568 bytes.
SHA-256: `287e118db9fb2382da7c2f0144cf101de633b1c0cf98cbce481d0992cf2a42b7`.
The running receiver at 192.168.1.165:17494 acknowledged `OK SAVED`, CRC-32
`F9FFED77`, for `Homebrew/Dev/Received/DSiLink-1.5-test.nds`. No auto-run.

Hardware check: launch from Pico Launcher, confirm log on top and controls on
bottom. Browse directories and round-trip a disposable file in FileZilla; check
paths wrap, successful actions/results appear, errors are readable and transfer
responsiveness remains acceptable. Scroll back while events arrive and use Y
to return live. Check X reconnect logs and subsequent FTP, and send a build to
check the build-receiver log. Logs are volatile and never saved to the card.
