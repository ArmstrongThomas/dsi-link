# Boot handoff: debugging notes

Status as of 2026-10-21: sending and saving work on hardware; booting the
received file through Pico Loader does not. Use `--no-run` / `-NoRun` and
start the file from Pico Launcher until this is fixed.

## Test setup

- DSi LL (Japanese), DSpico with Pico Launcher (development build) and Pico
  Loader, DSi mode, Wi-Fi from DSi Advanced Setup (WPA).
- Test ROM: Pixel Doodle 0.15.0 standard, 334,336 bytes, CRC-32 4820B952.
  It boots normally from Pico Launcher, including the copy DSi Link saved.

## What the boot check screen showed

| Field | 1.2 | 1.3 |
|---|---|---|
| Card driver | `DSpico DLDI`, type `PICO`, magic OK, size 2^14 | same |
| Driver pointer passed | live driver at `0x02000B00` | startup snapshot at `0x020271C0` |
| Pico Loader API | 3 | 3 |
| ARM7 entry (from `picoLoader7.bin`) | `0x06000414` | `0x06000414` |
| ROM path | `/Homebrew/Dev/Received/PixelDoodle.nds` | same |
| Return path | `/Homebrew/Dev/DSiLink.nds` | `/Homebrew/Dev/Received/DSiLink.nds` |
| Mode | DSi | DSi |

The 1.3 return path came from argv, so **Pico Launcher passes the launched
homebrew its own path** in argv.

## What happens after pressing A

Every attempt (1.0, 1.2, 1.3) ends the same way. The screen changes, then
DSi Link's own startup screen appears with `SD CARD NOT AVAILABLE`, from the
copy named as the return path. Pixel Doodle is never seen.

So something boots from the card, which means the ARM9 jump into
`picoLoader9.bin`, the ARM7 start through `resetARM7()`, and Pico Loader's own
card access all work. What comes up afterwards cannot use the card.

## What has been ruled out

- **Paths and header layout.** The return path in the same header arrives
  intact, and the ROM path is in the same format.
- **The driver copy.** 1.3 passed a snapshot taken before DSi Link started the
  driver; the result was identical. Pico Loader's `dldi_init()` copies the
  driver from the pointer first thing, before any ROM load or RAM clear
  (pico-loader `arm7/source/fat/dldi.cpp`).
- **The received file.** The same file boots from Pico Launcher.
- **Wi-Fi activity.** 1.3 disables the radio before the handoff; no change.

## Leading theory

Pixel Doodle exits immediately (`return 1` from `main`) if its first
allocations fail, and exiting goes through Pico Loader's bootstub to the
return path. The relaunched DSi Link then fails `fatInitDefault()`. Both
early failures point to the console being left in a state Pico Loader does not
expect, rather than to the files or paths. Candidates:

1. **`resetARM7()` is not equivalent to Pico Launcher's ARM7 code.** Pico
   Launcher's ARM7 turns sound off, disables IRQs, writes `dldiDriver`, and
   jumps straight to `entryPoint` (`arm7/source/picoLoaderBootstrap.cpp`).
   What BlocksDS's stock core does on `resetARM7()` has not been read yet. It
   may reset or reload ARM7 state, touch SCFG or WRAM, or expect a bootstub
   layout.
2. **SCFG / memory configuration.** DSi Link runs in DSi mode with BlocksDS
   defaults. Pico Launcher's own state at handoff (SCFG_EXT, main RAM size,
   WRAM/MBK settings) may differ, and Pico Loader may rely on it.
3. **ARM9 state.** DSi Link disables the MPU and caches before jumping;
   Pico Launcher's libtwl runtime may leave the MPU configured differently
   (VRAM executable, DTCM location).

## Next steps

1. Read BlocksDS libnds `resetARM7()` (ARM9 side and the ARM7 core's FIFO
   handler) to see exactly what the ARM7 does.
2. If it differs, start the ARM7 another way: place a few instructions of
   ARM7 code in VRAM D that mirror Pico Launcher's `pload_start()` (IRQs off,
   jump to `entryPoint`) and point `resetARM7()` at that stub instead.
3. Compare SCFG_EXT9/SCFG_EXT7 and REG_WRAMCNT between DSi Link and a Pico
   Launcher handoff; show them on the boot check screen.
4. Check whether Pico Loader writes a log file (it calls `initLogger()`).
5. Fallback that always works: exit to Pico Launcher (`exit(0)`) after saving,
   so the received file is one tap away.
