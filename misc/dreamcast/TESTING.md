# Dreamcast Port — Testing Checklist

Use this checklist when debugging, auditing, or validating changes to the Dreamcast build.

## Build

```bash
source /path/to/kos/environ.sh
make dreamcast-assets   # optional: download texture + audio zips
make dreamcast
```

Expected outputs: `ClassiCube-dc.elf`, `ClassiCube-dc.iso`, `ClassiCube-dc.cdi`

Prerequisites:
- KallistiOS toolchain with `environ.sh` sourced
- Run `make dreamcast-assets` — downloads texture/audio zips and generates `IP.BIN` if `makeip` is installed
- Or manually: `misc/dreamcast/IP.BIN`, `misc/dreamcast/classicube.zip`, optional `misc/dreamcast/audio.zip`

CI reference: `.github/workflows/build_dreamcast.yml` (`ghcr.io/classicube/minimal-kos:latest`)

## Emulator (Flycast)

### Serial / debug output

In Flycast, enable SH4 serial output to see `Platform_Log` messages (modem status, SD init, crashes):
- Options → Advanced → enable serial output / connect to stdout
- Or launch from terminal with serial redirected

On real hardware, use a Dreamcast serial cable or coders cable with `dc-tool -x` / dcload.

### Test matrix

| Test | Steps | Pass criteria |
|------|-------|---------------|
| Boot | Load `ClassiCube-dc.cdi` or `.elf` via dcload | Reaches launcher menu |
| Serial log | Enable SH4 serial / stdout in emulator | Boot messages visible, no crash spam |
| Single-player | Start local game | World loads, movement and block place/break work |
| Direct connect | Enter IP:port on DC connect screen | Joins server or shows dialog on failure |
| Multi-controller | Map 2+ virtual controllers to ports A-D | All connected pads respond (see gamepad fix) |
| Error dialog | Trigger connection failure | On-screen dialog appears (not just serial log) |
| Audio | Enable sounds + music | Both play; no hang or divide-by-zero |
| Screenshot | Rebind in controls if needed | Default unbound (X is inventory) |

## Real Hardware

| Test | Hardware | Pass criteria |
|------|----------|---------------|
| CD boot | Burnt CDI, no SD | Game boots from disc, read-only `/cd/` path works |
| SD saves | SD adapter, FAT formatted | `/sd/ClassiCube/` created; options and maps persist after reboot |
| VMU options | No SD, VMU in any port/slot | `options.txt` loads/saves via VMU fallback |
| Modem | Dreamcast modem | PPP connects (~40 s); on-screen status during init |
| BBA | Broadband adapter | Skips modem dial; network play works |
| 4-player | 4 controllers on maple bus | Each port independently controls a player in split-screen |
| Long session | 30+ min multiplayer | No crash, no VRAM exhaustion spiral |
| VRAM pressure | Large view distance / many textures | View distance halves with chat warning; game continues |

## Regression Areas (recent fixes)

Verify these specifically after code changes:

1. **Gamepad loop** — Controller on port B/C/D works when port A is empty
2. **Socket flags** — Non-blocking connect completes; no `fcntl` side effects on other flags
3. **Background color** — Fog/sky tint updates when changing worlds or weather
4. **VirtualDialog** — `Window_ShowDialog` shows dismissible on-screen message
5. **Framebuffer flip** — Launcher/menu UI has reduced tearing (`vid_flip` after 2D draw)
6. **SD sync batching** — Saves persist after clean exit; no excessive SD wear during rapid file ops
7. **Audio poll** — Music + SFX concurrently without glitches; pause/resume music if applicable
8. **Split-screen** — 2/3/4 player viewports render in correct screen regions
9. **Gamepad hot-unplug** — Disconnecting a controller clears stuck inputs
10. **Keyboard / mouse ports** — Keyboard on any maple port; mouse not limited to port A
11. **BBA + SD** — Broadband adapter and SD card both work; saves land on `/sd/ClassiCube/`
12. **SD boot skip** — SD-only boot skips ~40 s modem dial; START at boot also skips modem
13. **Scissor / split-screen** — Viewport regions clip geometry correctly; scissor disable restores full-screen
14. **VSync** — Frame pacing stable when vsync enabled in options
15. **Audio empty buffers** — Looping sounds and rapid SFX do not stall the snd_stream callback
16. **VMU any slot** — Options load/save on VMU in any maple port/slot
17. **Selection box edges** — Multiplayer selection regions show wireframe edges (KOS pvrline path)
18. **Multi-stream audio** — Music + SFX concurrently without stalling (all streams polled each frame)
19. **Skip-modem option** — Set `launcher-dc-skipmodem=true` in options.txt; boot skips modem dial
21. **Boot on-screen log** — Modem/SD/BBA status visible on CRT during init (before launcher)
23. **Split-screen scissor** — Viewport clip resets each 3D frame; no bleed from prior player region
24. **Keyboard unplug** — Disconnecting maple keyboard releases held keys
25. **Spawn bind** — Set spawn is B+START; START alone sends chat
27. **Coloured direct PT** — Map overlay / coloured UI quads use store-queue direct path
28. **Line batching** — Selection box edges deferred to OP list (no PT list interruption)
29. **Texture offset asm** — Cloud/sky scroll offset without shifting vertex buffer each draw

## Known Limitations (not test failures)

- `SetColorWrite` not implemented on PVR2 (same as several console backends)
- No native file picker
- Read-only when booting from CD without SD
- Modem init can block boot ~40 seconds on CD-only boots (hold START at boot or use SD to skip)

## Reporting Issues

On crash, note:
- Emulator vs real hardware
- SD / VMU / modem / BBA configuration
- Serial log or on-screen register dump (crash handler shows R0–R15, PC, SR, PR)
- Steps to reproduce

Post to ClassiCube Discord or forums with the above details.
