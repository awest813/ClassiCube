The dreamcast build requires an initial bootstrap program (Initial Program) named IP.bin

To generate a custom IP.bin, compile https://github.com/Dreamcast-Projects/makeip

Then run: makeip misc/dreamcast/ip.txt misc/dreamcast/IP.BIN -l misc/dreamcast/boot_logo.png

Optional disc assets (not committed — copy before `make dreamcast`):
- misc/dreamcast/classicube.zip  → texpacks/default.zip on disc
- misc/dreamcast/audio.zip        → audio/default.zip and audio/classicube.zip on disc

---

For more details about IP.bin, see https://mc.pp.se/dc/ip.bin.html

---

See also:
- PLAN.md    — port roadmap and known issues
- TESTING.md — manual test checklist for emulator and hardware

Direct connect screen options (persisted in options.txt):
- launcher-dc-username
- launcher-dc-ip
- launcher-dc-port
- launcher-dc-mppass (stored securely)