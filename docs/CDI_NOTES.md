# CD-i / CD-RTOS notes for static recompilation

These are facts worked out while porting *Link: The Faces of Evil* and *Laser
Lords*. The Green Book (CD-i Full Functional Specification, May 1994) describes
the APIs but mostly omits numeric codes. The codes below were inferred from the
games' library wrappers and the parameter lists in the spec. An entry marked
*(seen)* was confirmed by actual use in Link; *(LL)* marks what Laser Lords
confirmed.

## Disc and boot

* Sectors are raw 2352 bytes, Mode 2: sync (12), header (4), subheader
  (file, channel, submode, coding, ×2), then data. Form 1 carries 2048 bytes
  and Form 2 carries 2324 (audio uses 2304).
* The disc label is at LSN 16 (`"CD-I "` at data offset 1). The application
  identifier at label offset 574 names the boot module, for example `cdi_link`.
* Real-time files (`.rtr`, `.rtf`) have file number ≥ 1. Their directory size
  counts only that file's sectors, and padding sectors use file number 0.
  Empty sectors (submode with no V/A/D bit) can still carry EOR, trigger or EOF
  and **must be processed** for play-control purposes.

## OS-9 process start (what the runtime emulates)

* The kernel copies idata into static storage and applies irefs. The first
  table is code-relative (+module base) and the second data-relative (+data
  base).
* On entry, **a6 = static storage + 0x8000**, a5 = a7 = parameter area,
  a3 = module, d5 = parameter size and d6 = total memory.
* MPW-built programs (Link): idata begins with a jump table header
  `[size, a5 offset]` followed by `dc.w seg; jmp abs.l` stubs copied to
  `a5+0x20`. `jsr N(a5)` jumps to a stub in RAM, which costs one extra
  instruction. Count it, or cycle-exact comparisons with the interpreter
  drift. Function names are present as MacsBug symbols after each function.
* Microware C programs (the Philips "bumper"): a6-relative data and calls of
  the form `movea.l #off,aN / jsr d8(pc,aN.l)`.
* Laser Lords' compiler (Microware-style, `unlk a5 / rts` epilogues) *(LL)*:
  * idata holds `jmp abs.l` stubs whose targets are relocated by the code irefs,
    and the program calls them as `jsr d16(a6)` (d16 = data offset − 0x8000).
    Library calls, including the stack check, go through these stubs.
  * Functions start with a stack check:
    `move.l d0,-(sp) ; move.l #N,d0 ; jsr stub(a6)` (or `bsr.w`) then
    `move.l (sp)+,d0 ; link a5,#k`.
  * Library wrappers are called through `lea f(pc),a2 ; jsr (a2)`.
  * Switches: `moveq #N,dK ; cmp.l dK,d0 ; bhi default ; asl.l #1,d0 ;
    move.w T(pc,d0.l),dK ; jmp T(pc,dK.w)`, with offsets relative to T.
* A game can be split into several programs that a launcher forks in turn and
  that share state through a data module *(LL)*. Laser Lords: `cdi_launcher`
  creates `cdi_launch.res` with F$DatMod and alternates F$Fork + F$Wait of
  `cdi_space` and `cdi_tribes`. Each child F$Links the data module.

## Kernel behaviour the games rely on

* **Signals** are delivered asynchronously. Games busy-wait on flags that
  their intercept handlers set, so translated code must yield at backward
  branches.
* **F$Sleep and F$Wait clear the signal mask.** The standard pattern is
  "mask, start an operation, sleep".
* **F$Sleep time units** *(LL)*: with bit 31 set, the low 31 bits are 1/256 s,
  converted to whole ticks (rounded down). A sleep of one tick or less only
  gives up the time slice, and 0 (without bit 31) sleeps until a signal. Laser
  Lords fades each title slide in 64 steps of `F$Sleep(0x80000001)` × (number
  of triggers + 3). Taken as real 1/256 s waits, the fades fall behind the disc:
  slides overrun their buffers and arrive corrupt, and the slideshow loop never
  reaches the code that polls the buttons to skip the intro.
* While an intercept routine runs, signals are masked. F$RTE restores the
  context and unmasks.
* F$Alarm `A$Cycle` (d1=2) with time bit 31 set means the time is in 1/256 s.
  Link's 16 Hz game tick is `0x80000010`.
* F$Fork + F$Wait: a forked child (for example the bumper) runs to completion
  while the parent waits. Asynchronous signal sources must target the process
  that requested them.
* Common system call numbers: F$Link 00, F$Load 01, F$UnLink 02, F$Fork 03,
  F$Wait 04, F$Chain 05, F$Exit 06, F$Send 08, F$Icpt 09, F$Sleep 0A, F$ID 0C,
  F$CmpNam 11, F$Time 15, F$UnLoad 1D, F$RTE 1E, F$TLink 21, F$DatMod 25,
  F$SetSys 27, F$SRqMem 28, F$SRtMem 29, F$Event 53, F$Alarm 56,
  F$SigMask 57, F$SRqCMem 5C, I$Dup 82, I$Create 83, I$Open 84, I$Delete 87,
  I$Seek 88, I$Read 89, I$Write 8A, I$ReadLn 8B, I$WritLn 8C, I$GetStt 8D,
  I$SetStt 8E, I$Close 8F.
* Error codes: E$EvFull A5, E$EvBusy A6, E$EvntID A7, E$BMode CB,
  E$MemFul CF, E$UnkSvc D0, E$EOF D3, E$PNNF D8, E$KwnMod DA, E$MNF DD,
  E$DevBsy FA.
* **F$UnLoad** (a0 = name) drops one link and fails with E$MNF once the module
  is gone. The Laser Lords launcher calls it in a loop until it fails *(LL)*.
  F$Fork takes a link on the child's module, which is released when the child
  exits. A child's open paths and alarms go away with it.
* **F$DatMod** *(LL)*: a0 = name, d0 = data size, d1 = attr/rev, d2 = access
  (bit 15 set: d3/d4 hold extra parameters). Returns a1 = data, a2 = module
  header, a0 past the name. The data starts at M$Exec, which is 0x34 here.
* **F$Event** *(LL)*: d1.w = function. Link 0 (a0 name → d0 id),
  UnLnk 1, Creat 2 (d0 value, d2.w wait increment, d3.w signal increment,
  a0 name), Delet 3, Wait 4 (d0 id, d2 min, d3 max → d1 value), WaitR 5,
  Read 6, Info 7, Signl 8 (bit 15 of d1 = all), Pulse 9, Set A, SetR B
  (d2 value → d1 old). A woken waiter adds its wait increment.
* **`line_event`** belongs to the video driver, and programs Ev$Link it without
  creating it *(LL)*. The bumper waits for value 1 with range (1,1) and counts
  wakeups to pace its animation. The runtime sets it to 1 every field, with a
  wait increment of −1.

## Devices and the CSD

* Games read `/nvr/csd`. It holds text Device Status Descriptors of the form
  `type:name:params` ending in CR. Types:
  * 1 is the CD control unit (`/cd`);
  * 2 is the audio processor (`/ap`);
  * 3 is video (`/vid`, with `LI="525"` or `"625"`);
  * 4 is NVRAM (`/nvr`, `SZ#8`);
  * 5 is the pointer (`/ptr`, `CL="d"` for a manoeuvring device).
* GetStat 0x43 on `/nvr` returns free NVRAM bytes in d2 *(seen)*. Laser Lords
  runs `cdi_nvrui`, the player's file manager, only when the save doesn't fit
  *(LL)*.

## CDFM (`/cd` files)

| SetStat | Code | Notes |
|---------|------|-------|
| SS_Play | 0x2D | a0 = PCB *(seen)* |
| SS_Abort | 0x31 | *(seen)* |
| SS_Seek | 0x30 | |
| SS_Enable / SS_Disable / SS_Cont / SS_Stop / SS_Pause | 0x39 / 0x38 / 0x37 / 0x36 / 0x33 | probable numbering |

* **PCB:** Stat.w, Sig.w, Rec.l, Chan.l, AChan.w, then CIL pointers for video,
  audio and data.
  * PCB_Sig is sent on EOR, trigger and finish.
  * Rec is decremented on each EOR, and the play ends when it reaches 0.
* **PCL:** Ctrl.b, res, SMode.b, Type.b, Sig.w, Nxt.l, Buf.l, BufSz.l (in
  sectors), Err.l, res.w, Cnt.l.
  * When the buffer fills, Ctrl bit 0 is set, the CIL entry advances to Nxt,
    and PCL_Sig is sent.
  * A sector for a PCL that is still full (an overrun) is lost, and the play
    continues *(LL)*. Ending the play with an error instead made the Laser
    Lords title skip itself. With correct F$Sleep timing the game keeps up and
    no overruns happen.
* **SS_Abort** completes the play: PCB status and PCB_Sig, as at the end
  *(LL)*. Laser Lords waits for that signal after aborting.
* CILs can hold garbage in slots for channels that aren't selected *(LL)*.
* Plays run at 75 sectors per second (1×). Audio sectors whose channel is in
  AChan go to the audio processor. Otherwise they go to the audio CIL.

## Audio (`/ap`)

* SS_SM = 0x3B. These are SetStat unless marked GetStat:
  * Creat 0 (coding, groups) returns the id and a0 = data;
  * Out 1 (id, a0 = asynchronous status block);
  * Off 2;
  * Close 3;
  * Info 0 is GetStat and returns a0 = descriptor.
* **SM_Out on a busy processor queues the new soundmap** to start when the
  current sector finishes. Games double-buffer music this way and pace
  themselves on the completion signal (ASY_Sig), so completions must be
  sample-exact.
* **Soundmap descriptor** (SM_Info returns it): +0 id, +2 loop start group,
  +4 loop end group, +6 loop count, +10 buffer address, +14 buffer size,
  +26 number of groups, +30 coding, +32 play address *(LL)*. Laser Lords points
  +10 at its own sound bank and, before each SM_Out, sets +2/+4 to one
  effect's groups and +32 to that effect's first group. SM_Out plays groups
  +2..+4 starting at +32.
* `_sm_creat(path, coding, groups, &data)` stores the data address through its
  fourth argument *(LL)*.
* SS_SC = 0x3D, SC_Atten d3: bytes 2 and 0 are the direct paths (L→L, R→R),
  bytes 3 and 1 the cross paths. 0xFF is off.
* ADPCM coding byte:
  * bits 0-1 select stereo (01 = stereo);
  * bits 2-3 select the rate (00 = 37.8 kHz, 01 = 18.9 kHz);
  * bits 4-5 select the sample size (00 = 4-bit, 01 = 8-bit).

  A sound group is 128 bytes: 16 parameter bytes (copies at 4..11) followed by
  112 data bytes. The filter coefficients are the XA ones.

## Video (`/vid`)

* SS_DC = 0x56 (d2 = function):

  | Code | Function | Code | Function |
  |------|----------|------|----------|
  | 0 | CrFCT | 0xA | WrLI |
  | 1 | RdFCT | 0xB | DlLCT |
  | 2 | WrFCT | 0xC | FLnk |
  | 3 | RdFI | 0xD | LLnk |
  | 4 | WrFI | 0xE | Exec |
  | 5 | DlFCT | 0x10 | Nop |
  | 6 | CrLCT | 0x11 | SSig |
  | 7 | RdLCT | 0x12 | Relea |
  | 8 | WrLCT | 0x1B | PRdLCT |
  | 9 | RdLI | 0x1C | PWrLCT |

  Seen: 0, 2, 4, 6, 7, 0xA, 0xC, 0xD, 0xE, 0x10, 0x11 and 0x1C. 0xF and 0x13
  are probably Intl and SetCmp; they're ignored.
* **DC_Nop** (0x10) *(LL)*: d3 = LCT, d4 = line, d5 = column, d6 = lines,
  d7 = columns (the same layout as WrLCT). It fills that block with no-ops.
  Laser Lords' launcher owns LCT 1 and 2, and each game module writes into
  them. On start-up a module clears columns 0-6 of what the previous one left
  (column 7 holds the line start addresses). Ignoring it left the planet
  screen's fade-out (plane A weight 0 from line 14) in the bridge's table, and
  that blacked out the monitor frame around Zendo's transmissions after
  visiting a planet.
* LCT line numbers are in UCM coordinates: normal-resolution tables hold half
  the lines. "P" variants use physical lines.
* SS_GC = 0x52: Pos 0, Show 1, Hide 2, Ptn 3, Col 4, Org 5, Blnk 6.
  Coordinates are high resolution (768×480).
* SS_PT = 0x59: Coord 0 (GetStat) returns d0 = buttons (bit 15 = active) and
  d1 = H:V. SSig 1, Relea 2, **Pos 3, Org 4** (note this order).
* SS_DM = 0x51 (drawmaps): DM_Creat 0 (→ a0 = descriptor, id at +0),
  DM_Close 0xC (d3 = id) *(LL)*. SetStat 0x54 fn 4 is used by `cdi_nvrui`
  (text output, not identified).
* Video interrupts come only from DCP instruction `$60`. DC_SSig counts them.
* DCP opcodes:

  | Opcode | Meaning |
  |--------|---------|
  | 10 | NOP |
  | 20 | link (driver-internal) |
  | 40 | display line start |
  | 60 | interrupt |
  | 78 | display parameters (RMS bits 0-1, 10 = run-length) |
  | 80-BF | CLUT entry within the current bank |
  | C0 | coding: CM0 bits 0-3, CM1 bits 8-11, NM bit 19, CS bit 22 |
  | C1 | transparency: TA bits 0-3, TB bits 8-11, bit 23 = no mix |
  | C2 | plane order |
  | C3 | bank |
  | C4 / C6 | transparent colour |
  | C7 / C9 | mask colour |
  | CA / CB | DYUV start values |
  | D0-D7 | mattes |
  | D8 | backdrop |
  | D9 / DA | mosaic |
  | DB / DC | ICF (0-63) |

* **Register ownership:** the two display channels share the register file,
  but each register can only be loaded from its own channel *(LL)*.
  Channel A: C0, C1, C2, C4, C7, CA, D0-D8, D9, DB. Channel B: C6, C9, CB, DA,
  DC. C3 and CLUT loads work from both. Laser Lords' plane B FCT contains
  `C4000000`. Applying it would clear plane A's colour key and hide every
  portrait behind solid red.
* **Mattes:** each region register with an operation acts when the beam reaches
  its position. Op 0 means "no change" and does not end the list *(LL)*. Laser
  Lords leaves `D0000000` in matte 0 and the real command in matte 1.
  The matte flags keep their value from one line to the next and are cleared
  only at the start of the field *(LL)*. The planet screens set flag 0 at x=0
  on line 0, then from line 14 reset it at x=64 and set it again at x=702. The
  flag is still set at the start of each line, so the frame (plane B) shows
  outside that window. Clearing the flags on every line let the scene cover
  the frame's left column and top band.
* RL7: a byte with bit 7 set starts a run of colour b&0x7F whose length is the
  next byte (0 = to the end of the line). Otherwise the byte is a single pixel.
* DYUV deltas: 0,1,4,9,16,27,44,79,128,177,212,229,240,247,252,255.
* Colours are in studio range (black 16, white 235).
* Link draws sprites with **compiled sprite routines loaded from disc into
  RAM**. They run in the interpreter.

## Timing model used by the runtime

* Emulated time counts executed 68k instructions (`CPU_IPS` per second).
  Laser Lords uses 355,000 *(LL)*. The game's 2-tick cyclic alarm (50 Hz at the
  CD-i's 100 ticks per second) would allow a player step every 20 ms, but on
  hardware the 68070 needs longer to draw one. Compared with a recording of
  the original, the ladder climb is 8 lines every 0.1 s (one step every
  6 fields) and the walk is 8 px per pose with a new pose every ~0.12 s. At
  the template's 4M the climb ran 5x and the walk 1.8x too fast. 355k gives
  the climb exactly and the walk within about 3%. The intro slideshow still
  keeps up with the disc at this rate (no PCL overruns).
* Video fields (60 Hz), CD sectors (75 Hz), alarms and soundmap completions are
  scheduled on that clock. Pacing to the wall clock happens once per field.
* Translated code yields only at backward branches (`BUDGET`), and the
  interpreter yields by the same rule. That keeps `--interp` runs comparable
  frame for frame.
* The run loop stops between fields (`frontend.c`). Nothing that belongs to
  the machine lives on the host call stack at that point *(LL)*:
  * A forked child runs in the same loop as its parent. `F$Wait` saves the
    parent's registers and switches to the child; the child's `F$Exit`
    restores the parent and returns from its `F$Wait` with d0 = pid and
    d1 = status. (The template ran the child in a nested run loop inside
    `F$Wait`.)
  * `F$Sleep` and `Ev$Wait` that have to wait advance time to the next event
    and return their own trap address, which runs again after the scheduler
    (the trap and what it waits for are kept per process). Every `os9` call
    site is therefore an entry point.
  * So `state.c` can snapshot the machine between any two fields: RAM up to
    the highest heap address handed out, registers, and each module's part
    (kernel, CDFM, audio, video). Run-ahead and quicksave use it.
  * Speculative (run-ahead) fields must not change host files: `I$Create`,
    `I$Delete` and `I$Write` on `/nvr` wait in their trap until a real field
    runs them.

## Laser Lords game data used by the runtime

All in `cdi_tribes` (static storage at a6, from its `F$Icpt` call):

| Offset | Size | Meaning |
|--------|------|---------|
| -22336(a6) | long | Player energy (the blue bar, 500 per segment) |
| -22340(a6) | long | Maximum energy |
| -22382(a6) | byte | Lives (the hearts, 9 at the start) |
| -22094(a6) | long | Set to 1 when the player dies (the main loop waits for it) |
| -22042(a6) | long | Damage of the current attack |
| -22494(a6) | long | Buffer whose first 320x160 bytes are the clean background of the scene window (window: x 32-351, lines 14-173) |

* `func_0dbd0c` (0xDBD0C) takes d0 from the energy (d1 = 0) or from the
  maximum (d1 = 1). Below zero it sets the energy to -1 and the death flag.
  Its only caller is combat (0xD9A38). The invincibility hook returns from it
  at once when d1 = 0.
* Other code sets the energy to -1 and the death flag directly (story and
  trap deaths); invincibility does not cover those.
* The scene's characters are drawn into plane A over the background copy, so
  plane A pixels that differ from the copy are exactly the sprites. The CLUT
  is reloaded during the field (the bottom panel changes entries 0-2 at line
  175), so sprite interpolation takes each index's colour from the picture.
