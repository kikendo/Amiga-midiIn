# midiIn - C port (work in progress)

Port of the AmigaE sources in `../src` to C, built with the AmigaPorts / Bebbo
`m68k-amigaos-gcc` toolchain (6.5.0b tested). The AmigaE tree is untouched and
keeps building as before. The 68k mixer (`src/modules/softmix.s`) is kept as
assembler; only the E code is being replaced.

## Audio engines

Three engines, chosen at run time behind one interface:

| engine | CPU | what it is | state |
|---|---|---|---|
| AHI | 68020+ | `softmix.s` mixes 32 voices into an AHI double buffer | to port |
| Paula 14-bit | 68020+ | software mix output through all four Paula channels, no AHI needed | to port |
| Paula 4-channel | 68000+ | four hardware voices, 8-bit samples, no mixing | `src/audio/paula4.[ch]` |

## Build

    cd c
    make                # plain 68000
    make CPU=-m68020    # 68020 and up

This builds `build/paula4_demo`. Copy it to the Amiga (or an emulator hard
drive) and run it from a shell. See the header comment of
`tests/paula4_demo.c` for what you should hear and what it prints.

If the build fails, the first compiler error is the useful one.

## Paula 4-channel engine, current limits

* voices map to hardware channels; 0 and 3 are left, 1 and 2 are right, so there
  is no per-note pan
* 8-bit mono samples only, in Chip RAM (data elsewhere is copied there)
* no backward playback yet
* a sample can be at most 131070 bytes (one DMA run); longer ones are refused
* periods are clamped to 113..65535 like ProTracker

Planned next: keep samples in Fast RAM and stream them into Chip RAM, which is
also what backward playback and long samples need.
