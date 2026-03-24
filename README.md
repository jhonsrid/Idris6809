# Idris6809 — Dragon 64 Emulator

A cycle-accurate Motorola 6809 emulator targeting the Dragon 64 home computer, written in C with SDL2 for display, audio, and input.

## ROM Files

Place the following ROMs in the `ROMS/` directory:

**Dragon 64** (two ROMs):
- `d64_1.rom` (16KB) — BASIC ROM, mapped at $8000–$BFFF
- `d64_2.rom` (16KB) — System ROM, mapped at $C000–$FEFF (includes I/O hole and vectors)

**Dragon 32** (single ROM):
- `d32.rom` (16KB) — Combined ROM, loaded at $8000–$BFFF and mirrored to $C000–$FFFF

**Optional:**
- `ddos10.rom` (8KB) — Dragon DOS ROM (for disk support)

## Building

Requires SDL2 development libraries.

```sh
make
```

Uses `pkg-config` for SDL2 detection.

## Usage

```
./idris6809 [options]
  --rom PATH      Single ROM (Dragon 32: 16KB mirrored, or 32KB combined)
  --rom1 PATH     Dragon 64 ROM 1 (default: ROMS/d64_1.rom)
  --rom2 PATH     Dragon 64 ROM 2 (default: ROMS/d64_2.rom)
  --nosound       Disable audio
  --headless N    Run N frames without display then exit
  --scale N       Window scale factor (default: 2)
  --speed N       Speed multiplier (default: 1.0)
  --debug         Enable debug output
```

Dragon 32 (single ROM):
```
./idris6809 --rom ROMS/d32.rom
```

Dragon 64 (default, two ROMs):
```
./idris6809
./idris6809 --rom1 ROMS/d64_1.rom --rom2 ROMS/d64_2.rom
```

## Architecture

```
main.c
  └── dragon.c        (machine orchestrator: reset, run loop, timing)
        ├── cpu6809.c  (cycle-accurate CPU core)
        ├── memory.c   (address decoding, ROM/RAM banking)
        ├── sam.c      (MC6883 SAM — clock, video addr, memory config)
        ├── vdg.c      (MC6847 VDG — video rendering)
        ├── pia.c      (2× MC6821 PIA — keyboard, sound, cassette, joystick)
        └── acia.c     (MC6551 ACIA — serial port)
```

### CPU Core (`cpu6809.c`)

Full Motorola 6809 implementation with:
- All addressing modes (inherent, immediate, direct, extended, indexed with all sub-modes)
- Complete instruction set across all three opcode pages (page 0, page 2 via $10, page 3 via $11)
- Accurate condition code flag computation (H, N, Z, V, C)
- Interrupt handling (NMI, FIRQ, IRQ) with correct priority and masking
- CWAI and SYNC halt states
- Per-instruction cycle counting

### Memory Subsystem (`memory.c`)

Dragon 64 memory map with:
- 64KB RAM with ROM overlay at $8000–$FEFF (switchable via SAM TY bit)
- I/O region decoding for PIA0 ($FF00), ACIA ($FF04), PIA1 ($FF20), and SAM ($FFC0)
- Interrupt vectors always read from ROM ($FFF0–$FFFF)

### SAM (`sam.c`)

MC6883 Synchronous Address Multiplexer:
- Bit-per-register set/clear interface at $FFC0–$FFDF
- Video display offset and page address calculation
- CPU rate selection (0.89 MHz normal / 1.78 MHz high speed)
- ROM/RAM mode switching via TY bit

### VDG (`vdg.c`)

MC6847 Video Display Generator:
- Text mode (32x16) with internal 5x7 character ROM
- Semigraphics 4 and 6 modes
- All color graphics modes (CG1, CG2, CG3, CG6)
- All resolution graphics modes (RG1, RG2, RG3, RG6)
- Scanline-accurate rendering with NTSC timing (262 lines, 228 cycles/line)
- Frame sync interrupt generation to PIA0 CB1

### PIAs (`pia.c`)

Two MC6821 Peripheral Interface Adapters:
- **PIA0** ($FF00–$FF03): 8x8 keyboard matrix scanning, joystick comparator, HSYNC/FSYNC interrupt inputs
- **PIA1** ($FF20–$FF23): 6-bit DAC sound output, single-bit sound, VDG mode control, cassette interface
- Full register model (DDR/data selection via control bit 2, interrupt flags, CA/CB control)
- Interrupt routing: PIA0 CB1 (FSYNC) → CPU IRQ, PIA1 CB1 (CART) → CPU FIRQ

### ACIA (`acia.c`)

MC6551 serial port at $FF04–$FF07 (Dragon 64 specific):
- TX/RX data registers, status, command, and control registers
- Stubbed for basic register access (TX buffer empty, no RX data)

### Frontend (`main.c`)

SDL2-based frontend:
- 512x384 window (256x192 native at 2x integer scale)
- Keyboard mapping from SDL scancodes to Dragon matrix positions
- Audio output via SDL audio callback with ring buffer
- Frame throttling to real-time (~50 Hz PAL)

## Debugging

- CPU trace mode: per-instruction disassembly with register state
- PC breakpoints: pause emulation and dump state on hit
- Memory hex dump via key shortcut

## Timing

PAL Dragon 64 timing:
- Master crystal: 14.218 MHz
- CPU E-clock: ~0.89 MHz (14,218,000 / 16)
- 312 scanlines per frame, 57 cycles per scanline
- 17,784 cycles per frame at ~49.97 Hz

## Key References

- MC6809E datasheet (Motorola) — instruction timing tables
- MC6883 SAM datasheet
- MC6847 VDG datasheet
- MC6821 PIA datasheet
- "Inside the Dragon" by Duncan Smeed & Ian Sommerville
- Dragon 64 technical reference / service manual
- XRoar emulator source (Ciaran Anscomb)

## License

See [LICENSE](LICENSE) for details.
