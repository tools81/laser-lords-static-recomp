# Laser Lords — CD-i static recompilation

A native Windows build of *Laser Lords* (Spinnaker / American Interactive
Media, 1992) for the Philips CD-i. The game's 68000 program modules are
translated ahead of time into C. A runtime supplies what the code expects from
a CD-i player: the CD-RTOS (OS-9) kernel, the CD file manager, the video
decoder (presented with Direct3D 11 at any resolution) and the ADPCM audio
processor.

The project was built from the CD-i static recompilation template that came out
of the *Link: The Faces of Evil* port. The changes it needed are listed under
"Porting notes" below.

## Installing

Download the latest release from the
[releases page](https://github.com/tools81/laser-lords-static-recomp/releases).
You need your own copy of the CD-i disc as an image (the `.cue` and its
`.bin`, raw 2352-byte sectors). It is not included.

* **Installer** (`LaserLords-<version>-Setup.exe`): installs for the current
  user, without administrator rights, by default to
  `%USERPROFILE%\Games\Laser Lords`. One page asks for the folder that holds
  the disc image. Start menu shortcuts are made for both editions.
* **Portable** (`LaserLords-<version>-portable.zip`): unzip anywhere (a USB
  stick works) and run `laserlords_hd.exe` or `laserlords.exe`. Put the disc
  image in the `rom` folder, or pick it when the game asks at its first start.

Either way the game is portable. Everything it writes stays in its own
folder: `saves\` (NVRAM saves, quicksave slots, display and HD settings, the
log) and `laserlords.ini`. Nothing goes to AppData or the registry, and the
graphics shaders are built into the executables. To move the game, move the
folder. The shortcuts pass `--portable`, which changes nothing because the game
is always portable; it is accepted so wrappers can state it. Both executables
are self-contained (the C runtime is linked in) and need Windows 10 or later,
64-bit.

`laserlords.ini` (next to the executables):

| Setting | Meaning |
|---------|---------|
| `rom=` | The disc image: its folder, or the `.cue`/`.bin` file |
| `update_check=` | `1`: at start, offer a newer release when there is one (default); `auto`: install it without asking; `0`: never check |

**Updates.** At start the game asks GitHub for the latest release. If it is
newer, the game offers to install it, downloads the portable zip and quits.
A small script copies the new files over the program's folder, keeping
`saves\` and `laserlords.ini`, and starts the game again. `--no-update` skips
the check for one run.

## Building and running

Requirements: Windows 10+, Visual Studio 2022 (C compiler), CMake 3.19+ and
Python 3.8+. No third-party libraries.

1. Put the disc image in `rom/`: `Laser Lords (USA).bin` + `.cue` (raw
   2352-byte sectors).
2. Run `build.bat`. It extracts the program modules from the disc, recompiles
   them into `gen/`, builds with MSVC and copies the two executables to the
   project root:
   * `laserlords.exe`, the **original edition**: the game as the CD-i showed
     it.
   * `laserlords_hd.exe`, the **HD edition**: Cartoon HD / Cartoon HD+
     upscaling, run-ahead and sprite interpolation (see below).
3. Run either one. It looks for the disc image given by `rom=` in
   `laserlords.ini`, then in a `rom` folder next to the program; you can also
   pass the path to the `.cue` or `.bin` as an argument. If it finds none, it
   asks for it.
4. To make a release, set the version in `VERSION` and run `package.bat`
   (`tools\package.ps1`). It builds, then writes
   `dist\LaserLords-<version>-portable.zip`,
   `dist\LaserLords-<version>-Setup.exe` (needs Inno Setup 6:
   `winget install JRSoftware.InnoSetup`; script `installer\laserlords.iss`)
   and `dist\SHA256SUMS.txt`. Publish the release with the tag `v<version>`
   and these files attached. The update check looks for the asset ending in
   `-portable.zip` in the latest release of `update_repo` (`game.json`).

Saved games (the CD-i's NVRAM), quicksave slots, the HD settings, the log and
debug dumps are kept in `saves/` next to the program.

### Controls

| CD-i | Keyboard | Gamepad (XInput) |
|------|----------|------------------|
| Thumbpad / pointer | Arrow keys (hold Shift to move faster) | Left stick or D-pad |
| Button 1 | Z, Space or A | A |
| Button 2 | X, Ctrl or S | B or Y |
| Both buttons | C or Enter | X or Start |

| Key | Action |
|-----|--------|
| F2 | Quicksave to the current slot |
| F3 | Next slot (1 to 10; shows the slot's date, or empty) |
| F4 | Load the current slot |
| F5 | Invincibility on / off |
| F6 | Sprite interpolation on / off (HD edition, default on) |
| F7 | Run-ahead: 0, 1, 2 or 3 fields (HD edition, default 2) |
| F8 | Upscaler: Cartoon HD+ (default), Cartoon HD, off (HD edition) |
| F9 | Cycle the aspect mode |
| F10 | Cycle the filter (sharp / smooth / CRT) |
| F11 or Alt+Enter | Toggle fullscreen |
| Tab (hold) | Run at full speed |
| Esc | Quit |

Command-line options: `--fullscreen`, `--windowed`, `--size WxH`,
`--aspect N`, `--filter N`, `--gdi` (software presenter), `--invincible`.
HD edition: `--runahead N`, `--interpolation on|off`,
`--upscale hdplus|hd|off` (they override `saves/hd_settings.ini` for one
run).

Messages (slot saved, invincibility on, ...) appear at the bottom of the
picture.

* **Quicksave** (both editions) stores the whole machine in
  `saves/quick_01.state` to `quick_10.state`. Slots work across sessions and
  can be saved anywhere, including on the ship and during the intro. NVRAM
  saves are separate files and are not part of a slot.
* **Invincibility** (both editions): the player loses no energy in combat.
  The enemies are not affected. Deaths the story or a trap causes directly
  (they set the energy to zero without a hit) still happen.
* **Cartoon HD** enlarges the picture 4x (to 1536x960) with an edge-directed
  2x step applied twice, in the manner of Hyllian's xBR. Flat fills stay flat
  and the staircase outlines of the 384x240 art become smooth curves.
  **Cartoon HD+** adds inking (outlines darkened a little so they stay bold)
  and an adaptive sharpen.
* **Run-ahead** hides the game's reaction time. After every field the whole
  machine is snapshotted, the next fields are run with the same input, the
  last of them is shown, and the snapshot is restored. Only the real fields
  are heard. The sound and the game are identical to a run without it.
* **Sprite interpolation** shows motion at 60 pictures a second. The game
  draws a new picture of its characters every 6 to 8 fields. While a picture
  is shown, the emulation looks ahead (from a snapshot) for the next one. On
  the planet scenes the game keeps a clean copy of the background, so the
  characters are lifted out exactly and drawn along their path: the old pose
  for the first half of the interval, the new pose for the second. Nothing is
  blended, and poses change when they did originally. Elsewhere (the ship,
  the intro), and when the scene itself scrolls, parts that moved as a whole
  are found by a block search and moved instead.

## What works

All five program modules on the disc are recompiled:

| Module | Base | Role |
|--------|------|------|
| `cdi_launcher` | `0xC0000` | Boot program. Plays the bumper, then alternates between the two game modules. |
| `cdi_tribes` | `0xC6000` | Planet surfaces: exploration, conversations, trading, combat. |
| `cdi_space` | `0xE8000` | Title, intro animation, ship bridge, hyperspace, save/load. |
| `cdi_nvrui` | `0xF2000` | Player's NVRAM file manager (see the limitations). |
| `cdi_bumperanim` | `0xFE000` | American Interactive Media logo animation. |

The game plays from the AIM logo through the title, intro, ship bridge and
planets, with music, speech and sound effects. Pressing a button at any point
in the title and intro skips to the ship bridge. Saving and loading to NVRAM
work.

Verification:

* Headless sessions driven by scripted and random input produce **bit-identical
  frames and audio** in the recompiled build and in the pure interpreter
  (`--interp`). The longest comparison covered 14,000 fields (about 4 minutes of
  play across several planets).
* Four random-input sessions of 40,000 fields each (about 11 minutes of play
  each) ran without errors.
* HD edition: a 12,000-field session with run-ahead 2 and interpolation on
  gives **audio identical** to the original edition with the same input, so
  the snapshots restore everything the game depends on. Quickloading a slot
  gives RAM byte-identical to the moment it was saved. A 40,000-field HD
  session with saves, loads and invincibility ran without errors. With
  everything on, an HD frame takes about 11 ms on average (16.7 ms is
  available).
* `insn_test` (2,032 per-instruction differential tests) and `alu_test`
  (12,600 reference vectors) pass.

### Known limitations

* **The NVRAM file manager draws nothing.** `cdi_nvrui` is the player's own
  "NVRAM full" screen. It draws with the UCM drawmap and text functions
  (`SS_DM` $51, $54), which the runtime doesn't implement. It only runs when a
  save doesn't fit in the 8 KB NVRAM. Laser Lords keeps a single 732-byte save
  file, which it deletes and rewrites on each save, so in normal play this
  screen never appears.
* **Disk Save and Disk Load** (the CD-i floppy drive) report "Limited Drive
  Configuration", as they do on a player without a drive.
* **Sprite interpolation is not perfectly even.** The game sometimes redraws
  a step in two pictures a few fields apart (the character, then the
  background it uncovered). The character's movement is spread over the part
  of the interval in which it moves, so a short hold remains between some
  steps.

## Layout

| Path | Contents |
|------|----------|
| `game.json` | Modules to recompile and their load addresses |
| `tools/cdi_extract.py` | Green Book filesystem reader. Writes program files and `_app.txt` (`--all` also dumps the real-time streams). |
| `tools/m68k.py` | MC68000 decoder |
| `tools/os9mod.py` | OS-9 module header, idata and irefs |
| `tools/analyze.py` | Code discovery (recursive descent plus the patterns below) |
| `tools/gen.py` | 68000→C generator, flag liveness, dispatch tables, `gen/game_config.h` |
| `tools/config.py` | `game.json` loader |
| `tools/gen_tests.py` | Per-instruction differential tests |
| `tools/bmp2png.py` | Converts frame dumps to PNG |
| `runtime/cpu.c` | Dispatcher and run loop |
| `runtime/interp.c` | 68000 interpreter, for RAM code and the `--interp` reference mode |
| `runtime/alu.c` | Shifts, division and BCD with exact flags |
| `runtime/os9.c` | OS-9 kernel: memory, modules, data modules, processes (fork/wait/exit), signals, alarms, events, paths, NVRAM, CSD |
| `runtime/cdfm.c` | CD file manager: reads, and real-time SS_Play with PCB/PCL/CIL |
| `runtime/video.c` | UCM display control (FCT/LCT), MCD212-style renderer, cursor, pointer |
| `runtime/audio.c` | ADPCM levels A/B/C, sound maps with gapless SM_Out, attenuation |
| `runtime/render_d3d11.c` | GPU presenter: aspect modes and sharp/smooth/CRT filters |
| `runtime/host_win32.c` | Window, input (keyboard and XInput), waveOut, pacing, hotkeys, scripted and fuzz input |
| `runtime/frontend.c` | Frame loop and presentation; quicksave and invincibility keys; HD edition run-ahead, interpolation lookahead and messages |
| `runtime/state.c` | Machine snapshots (every module provides its part), quicksave slot files |
| `runtime/cartoon.c` | Cartoon HD / HD+ upscalers, worker threads |
| `runtime/sprites.c` | Sprite interpolation from the planet scenes' sprite layer |
| `runtime/motion.c` | Sprite interpolation elsewhere (block motion search) |
| `runtime/cheats.c` | Invincibility (a `game.json` hook on the damage routine) |
| `runtime/edition_orig.c`, `edition_hd.c` | Which edition an executable is |
| `runtime/find_image.inc` | Finding the disc image, `laserlords.ini`, the program folder |
| `runtime/update.c` | Update check and installer (GitHub releases) |
| `runtime/version.rc.in` | Version information of the executables (from `VERSION`) |
| `installer/laserlords.iss` | Inno Setup script (asks for the disc image folder) |
| `tools/package.ps1`, `package.bat` | Release packages: portable zip, installer, checksums |
| `test/` | Instruction differential tester and ALU reference vectors |
| `docs/CDI_NOTES.md` | CD-i facts and API codes worked out during the ports |

## game.json

```json
{
  "title": "Laser Lords",
  "exe_name": "laserlords",
  "update_repo": "tools81/laser-lords-static-recomp",
  "modules": [
    {"file": "auto", "base": "0xC0000", "extra_entries": [], "a5_table": "auto"},
    {"file": "cdi_tribes", "base": "0xC6000", ...},
    ...
  ]
}
```

* **`update_repo`**: the GitHub repository whose latest release the program
  checks at start (empty: no update check).
* The **first module** is the program the player boots. `"auto"` reads its name
  from the disc label's application identifier (`extracted/_app.txt`).
* **Other modules** are programs the game loads at run time with F$Load or
  F$Fork. Each one is recompiled for its `base` address, and the runtime places
  it there. Ranges must not overlap and must stay below `0x140000`, where the
  heap starts. Program modules that are *not* listed still run, slowly, in the
  built-in interpreter.
* **`extra_entries`** are code addresses the analyzer can't discover, as
  absolute hex values (`base + offset`). None were needed for this game.
* **`a5_table`**: `"auto"` detects programs built with Apple MPW-style
  toolchains, whose calls go through `jsr N(a5)` (here: `cdi_nvrui`).
* **`hooks`** (top level): `{"address": "c_function"}`. When the function at
  that address is called, `int c_function(void)` in the runtime runs first.
  If it returns nonzero the function returns at once, as if it had executed
  `rts`. The generator and the interpreter both apply it. Laser Lords hooks
  the damage routine for invincibility.

The analyzer finds:

* call and branch targets;
* switch tables, with the bound from `cmpi` or from `moveq #N,dK ; cmp.l dK,dX`;
* MPW A5 jump tables;
* **a6 stub tables**: `jmp abs.l` stubs in initialized data, relocated by the
  code irefs and called as `jsr d16(a6)`. Every Laser Lords module except the
  launcher reaches its library through these;
* code pointers stored in initialized data (code irefs that aren't stubs);
* Microware `move.l #off,dN / jsr (pc,dN.l)` calls;
* F$Icpt handlers;
* `lea f(pc),aN ... jsr (aN)` calls along a path;
* `lea`/`pea` function pointers that start with `link`, `movem` or the
  compiler's stack check (`move.l d0,-(sp) ; move.l #N,d0 ; jsr/bsr check`);
* unreferenced functions in the gaps between known code (library routines that
  are only reached through pointers built at run time).

To see what the analyzer found, run `python tools/analyze.py [module index]`.
It writes a disassembly to `build/listing_<module>.asm`. Functions without
MacsBug symbols are named `module+offset` in logs and backtraces.

## Debugging workflow

* **Find what fails.** Unimplemented OS calls and SetStat/GetStat codes are
  logged to `saves/recomp.log`. Fatal errors also show a 68k register dump and a
  stack backtrace with function names.
* **Debug options:**
  * `--trace` / `--trace3` log syscalls with callers, signals and disc activity
    (`--trace3` adds every sector).
  * `--headless --frames N --dump K` runs fast without a window and writes every
    Kth frame as a BMP. When it stops after N fields it also writes
    `saves/video_state.txt`: the active FCTs and LCTs, the CLUT and the
    transparency registers.
  * `--script "field:key:dur,..."` scripts input (keys `1 2 L R U D`). For
    example, `--script "1300:1:10,2600:1:10"` skips the title and beams down to
    Luxor.
  * `--fuzz SEED` drives the game with random input.
  * `LL_DUMP_RANGE=A-B` writes every presented frame from A to B as a BMP.
  * Script keys `S`, `N`, `O` and `I` press F2, F3, F4 and F5 at that field.
  * A headless run that stops after `--frames N` also writes
    `saves/ramdump.bin` (the 16 MB address space).
  * `LL_INTERP_LOG=1` (HD edition) logs how each picture change was
    interpolated.
* **Check run-ahead and snapshots.** A headless HD run with `--runahead 2`
  must give the same `--wav` as the original edition with the same input;
  anything a snapshot misses shows up as a difference.
  * `--wav out.wav` records the audio.
* **Check the translation.** Run the same headless session twice, once with
  `--interp`, which executes everything in the interpreter, and compare the
  frame dumps and WAVs. Headless runs are deterministic: emulated time is
  counted in instructions, and the clock uses a fixed epoch. Use separate
  folders for the two runs, since each writes to the `saves/` folder next to its
  exe. Any difference points to a translation bug.
* **Unit-level checks.** `python tools/gen_tests.py` and
  `python test/gen_alu_vectors.py` generate test sources. Then build and run the
  `insn_test` and `alu_test` targets:

  ```
  cmake --build build\cmake --config Release --target insn_test alu_test
  build\cmake\Release\insn_test.exe "rom\Laser Lords (USA).bin" 200
  build\cmake\Release\alu_test.exe
  ```

* Code that jumps into RAM is not an error. It runs in the interpreter. Jumps to
  *undiscovered* addresses inside a recompiled module are fatal. Add those
  addresses to `extra_entries`, or teach `analyze.py` the pattern.

## Porting notes

What Laser Lords needed beyond the template (details in `docs/CDI_NOTES.md`):

* **Analyzer:** the a6 stub tables, idata code pointers, the stack-check
  prologue, the `moveq`/`cmp` switch bound, `lea`/`jsr (aN)` tracking and the gap
  scan listed above. Together they raised coverage of `cdi_tribes` from 41% to
  94% of its code area. The rest is data and strings.
* **Generator:** `jsr d16(a6)` stub calls go straight to the target while the
  stub in RAM still points there. Otherwise they go through the dispatcher.
  `F$Event` call sites are entry points, so a wait interrupted by a signal can
  restart.
* **Kernel:** `F$Sleep` time units (1/256 s converted to whole ticks, and a
  one-tick sleep only gives up the time slice). Without this, the intro's slide
  fades ran slow, slides arrived corrupt, and the intro couldn't be skipped.
  Also `F$Event` (with the video driver's `line_event`), `F$DatMod`
  (the shared `cdi_launch.res` module), real link counts for `F$UnLink` and
  `F$UnLoad` (the launcher unloads in a loop until the call fails), module links
  held by forked processes, closing a child's paths and alarms when it exits,
  and per-process expected A5 for MPW-built children.
* **CD file manager:** `SS_Abort` completes the PCB and sends its signal. A
  sector arriving for a PCL that is still full is dropped instead of ending the
  play.
* **Video:** matte registers act in position order and op 0 means "no change",
  not "end of list". Matte flags carry over from line to line within a field
  (the planet screens' frame depends on it). Registers that belong to one
  display channel ignore writes from the other channel's tables.
* **Audio:** `SM_Out` plays the range the soundmap descriptor describes (loop
  start/end groups and play address), which the game edits to pick a sound
  effect out of its sound bank.
* **Processes:** a forked program runs in the same run loop as its parent
  (`F$Wait` switches to it, its `F$Exit` switches back), and blocking calls
  (`F$Sleep`, `Ev$Wait`) return to the run loop and execute their trap again.
  No host call stack is held across fields, so the whole machine can be
  snapshotted between any two fields (run-ahead, quicksave). Every system call
  site is an entry point for that reason.
* **UCM:** `DC_Nop` (clears a block of a line control table; the modules use it
  to clear what the previous module left in the shared tables) and minimal
  `DM_Creat`/`DM_Close` bookkeeping.
* **CPU speed:** `CPU_IPS` is 355,000 instructions per second instead of the
  template's 4M. The player's walking and climbing are limited by how fast the
  68070 draws, and this rate matches a recording of the original. Build with
  `/DCPU_IPS=n` to try other values.
