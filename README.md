# Idris6809 — Dragon 32 Emulator

A cycle-accurate Motorola 6809 emulator targeting the Dragon 32 home computer, written in C with SDL2 for display, audio, and input.

## ROM Files

Place the following ROM in the `ROMS/` directory:

- `d32.rom` (16KB) — Combined ROM, loaded at $8000-$BFFF and mirrored to $C000-$FFFF

A 32KB ROM image is also supported and will fill $8000-$FFFF without mirroring.

## Building

Requires SDL2 development libraries.

```sh
make
```

Uses `pkg-config` for SDL2 detection.

## Usage

```
./idris6809 [options]
```

### Command Line Options

| Option | Description |
|---|---|
| `--rom PATH` | ROM file to load (default: `ROMS/d32.rom`). Accepts 16KB (mirrored) or 32KB images. |
| `--cas PATH` | Load a `.cas` cassette file. Use `CLOAD` or `CLOADM` in BASIC to read it. |
| `--cartridge PATH` | Load a cartridge ROM (8KB/16KB, maps at $C000). Auto-starts via FIRQ. |
| `--load PATH` | Restore a previously saved emulator state from a `.state` file. |
| `--scale N` | Window scale factor, 1-8 (default: 3). |
| `--nosound` | Disable audio output. |
| `--headless N` | Run N frames without display then exit. Prints CPU state on completion. |
| `--debug` | Print diagnostic info (PC, reset vector) to stderr. |

### Keyboard Shortcuts

| Key | Action |
|---|---|
| **F5** | Save emulator state to `yyyymmdd-hhmmss.state` in the current directory. |
| **Escape** | Dragon BREAK key. |
| **Tab** | Dragon CLEAR key. |
| **Backspace** | Dragon LEFT cursor. |

### Examples

```sh
./idris6809                                    # Boot with default ROM (ROMS/d32.rom)
./idris6809 --rom myrom.rom                    # Boot with alternate ROM
./idris6809 --cas game.cas                     # Boot with cassette loaded
./idris6809 --cartridge CARTS/game.rom         # Run cartridge (auto-starts)
./idris6809 --load 20260325-143022.state       # Resume from saved state
./idris6809 --scale 4 --nosound                # Larger window, no audio
```

## Architecture

```
main.c
  └── dragon.c          (machine orchestrator: reset, run loop, timing)
        ├── cpu6809.c    (cycle-accurate CPU core)
        ├── memory.c     (address decoding, ROM/RAM banking, cartridge ROM)
        ├── sam.c        (MC6883 SAM — clock, video addr, memory config)
        ├── vdg.c        (MC6847 VDG — video rendering)
        ├── pia.c        (2x MC6821 PIA — keyboard, sound, cassette, joystick)
        ├── cassette.c   (cassette tape FSK simulation)
        └── savestate.c  (save/load emulator state)
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

Dragon 32 memory map with:
- 32KB RAM with ROM overlay at $8000-$FEFF (switchable via SAM TY bit)
- Cartridge ROM overlay at $C000-$FEFF (8KB mirrored or 16KB)
- I/O region decoding for PIA0 ($FF00), PIA1 ($FF20), and SAM ($FFC0)
- Interrupt vectors always read from ROM ($FFF0-$FFFF)

### SAM (`sam.c`)

MC6883 Synchronous Address Multiplexer:
- Bit-per-register set/clear interface at $FFC0-$FFDF
- Video display offset and page address calculation
- CPU rate selection (0.89 MHz normal / 1.78 MHz high speed)
- ROM/RAM mode switching via TY bit

### VDG (`vdg.c`)

MC6847 Video Display Generator:
- Text mode (32x16) with internal 5x7 character ROM
- Semigraphics 4 and 6 modes
- All color graphics modes (CG1, CG2, CG3, CG6)
- All resolution graphics modes (RG1, RG2, RG3, RG6)
- Scanline-accurate rendering with PAL timing (312 lines, 57 cycles/line)
- Frame sync interrupt generation to PIA0 CB1

### PIAs (`pia.c`)

Two MC6821 Peripheral Interface Adapters:
- **PIA0** ($FF00-$FF03): 8x8 keyboard matrix scanning, joystick comparator, HSYNC/FSYNC interrupt inputs
- **PIA1** ($FF20-$FF23): 6-bit DAC sound output, single-bit sound, VDG mode control, cassette interface
- Full register model (DDR/data selection via control bit 2, interrupt flags, CA/CB control)
- Interrupt routing: PIA0 CB1 (FSYNC) -> CPU IRQ, PIA1 CB1 (CART) -> CPU FIRQ

### Cassette (`cassette.c`)

Bit-level FSK simulation for `.cas` file loading:
- Converts .cas bytes to 1200/2400 Hz square wave on PIA1 PA0
- Cycle-accurate waveform generation (per CPU instruction)
- Motor control via PIA1 CA2
- Audio output of cassette signal during loading

### Save State (`savestate.c`)

Full emulator state serialization:
- Saves RAM, CPU registers, PIA/SAM/VDG state, cassette position
- Binary format with magic number and version validation
- ~66KB state files with timestamp filenames

### Frontend (`main.c`)

SDL2-based frontend:
- 768x576 window (256x192 native at 3x integer scale, configurable)
- Keyboard mapping from SDL scancodes to Dragon matrix positions with shifted key remapping
- Audio output via SDL audio callback with low-pass filter and DC blocking
- Frame throttling to real-time (~50 Hz PAL)

## Timing

PAL Dragon 32 timing:
- Master crystal: 14.218 MHz
- CPU E-clock: ~0.89 MHz (14,218,000 / 16)
- 312 scanlines per frame, 57 cycles per scanline
- 17,784 cycles per frame at ~49.97 Hz

## Key References

- MC6809E datasheet (Motorola) -- instruction timing tables
- MC6883 SAM datasheet
- MC6847 VDG datasheet
- MC6821 PIA datasheet
- "Inside the Dragon" by Duncan Smeed & Ian Sommerville
- Dragon 32 technical reference / service manual
- XRoar emulator source (Ciaran Anscomb)

## License

See [LICENSE](LICENSE) for details.
