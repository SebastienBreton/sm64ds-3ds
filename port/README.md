# port/ - the native PC build

> **Layout (sm64ds-3ds):** this directory now lives at the top level of the
> [sm64ds-3ds](https://github.com/SebastienBreton/sm64ds-3ds) repository and builds
> against the decomp checked out next to it as the `sm64ds-decomp/` git submodule
> (CMake cache variable `SM64DS_DECOMP_DIR`, default `../sm64ds-decomp`; the Python
> generators honour the `SM64DS_DECOMP_DIR` environment variable). Paths such as
> `src/`, `include/`, `config/`, `extracted/` below are relative to that decomp
> checkout; the 3DS build itself is driven by `../build.sh`.

This tree hosts the PC port of the decompiled game: the same `src/` sources
compiled for the host, a platform seam instead of DS hardware, and its own
build that never touches the byte-matching pipeline.

## Rules

- **`src/` stays the byte-verified source of truth.** The port compiles files
  FROM `src/`; it never edits them for host convenience. A file that needs a
  host-side change gets that change behind `SM64DS_PLATFORM_*` guards or a
  seam header, reviewed like any other src change.
- **Logically-correct-but-unmatched implementations live in
  `port/unmatched/`, never in `src/`.** The NDS hybrid build keeps selecting
  delinked ROM bytes for those functions; the PC build selects these.
- **32-bit host first.** The recovered ABI assumes 4-byte pointers. x64 comes
  after struct recovery makes layout host-independent.

## Build locally on Windows

The current port is a suite of native smoke executables, not yet a standalone
playable game. It requires Visual Studio with the **Desktop development with
C++** workload and its x86 tools, plus CMake, Ninja, Python, and the normal local
ROM setup described in the repository README.

Generate the gitignored asset catalog once from your own cartridge dump, then
build and run every gate:

```powershell
python tools/asset_catalog.py generate sm64.nds
port\build-port.cmd
```

`build-port.cmd` discovers current Community or Build Tools installations of
Visual Studio instead of assuming a version-specific path. It configures a
32-bit Release build under `build\port\`, builds all smoke runners, and runs all
of them through CTest. A successful exit therefore proves both link closure and
the runtime assertions listed below.

## Gate ledger

Each gate is a slice manifest + a smoke binary that proves one seam with
game data. `build-port.cmd` builds all of them into `build\port\`.

| Gate | Smoke | What runs on host |
|---|---|---|
| 1 | `smoke` | types, fx math, matrices, Timer, Fader hierarchy |
| 2 | `smoke_heap` | ExpandingHeapAllocator, 5,000-op torture |
| 3a | `smoke_roots` | SetupRootHeap + the Memory:: layer (game global heap) |
| 3b | `smoke_fs` | SharedFilePtr over the catalog card seam, LZ77 cross-checked |
| 4a | `smoke_gx` | the interrupt-driven display-list pump, byte-equal vs harness |
| 4b | `smoke_model` | the whole Model pipeline: load, rebase, VRAM upload, materials, render (Mario, textured) |
| 4c | `smoke_anim` | Animation/UpdateBones recursion (the Mad Piano, posed) |
| 4d | `smoke_soak` | every compatible catalog model loads and renders with zero faults |
| 5 | `smoke_frames` | the fiber frame loop: game-shaped frames, the piano attack in motion |
| 5b | `smoke_soak_anim` | every compatible model+BCA pair animates and renders with zero faults |
| 6 | `smoke_oam` | the 2D sprite engine: OAM emit, affine params, upload to mapped OAM |
| 6b | `smoke_oam` | OBJ scan-out: the game's sprite placements become pixels |
| 7 | `smoke_modelanim` | ModelAnim: the game owns frame progression (speed, loop wrap) |
| 8 | `smoke_clsn` | collision: the octree walk answers ground queries over level KCL |
| 9 | `smoke_actor` | the actor framework: spawn, init, behave, render through vtable dispatch |

Supporting machinery: `tools/hostgen.py` (MMIO transform into the build
tree; src/ is never edited), `tools/romdata.py` (ROM constants from the
dsd-extracted image; Nintendo bytes stay out of git), `tools/ntr_manifest.py`
(fs manifest for the ntr backend), the `ntr/` platform layer (see its
README), and `hal/` (seams, bridges, and the storage for DS BSS globals).

Grow a gate by adding files to its `slice_gate*.txt` and assertions to its
smoke; `tools/host_frontier.py` reports how much of `src/` syntax-compiles
for the host so the next subsystem is a measurement, not a guess.

## Why the emulator is not here

The hybrid (tangOS-SM64DS) stays the verification harness: it proves
functions byte-for-byte against real hardware behaviour. The port is the
destination. Benchmarks on 2026-08-02 showed natives inside the hybrid are
performance-neutral (~2% of the emulated clock), which is the measured
argument for building the port rather than growing the hybrid further.

## Frontier baseline (2026-08-02)

First full sweep of `tools/host_frontier.py` (cl /Zs, 32-bit, all of src/):

    compiles for host: 11,005 / 11,222  (98.1%)

Refined the same day: the C2054 x98 bucket decoded as the asm-hatch TUs
(`asm void`, mwccarm syntax) -- DS hardware operations that SHOULD not
compile on a host. CP15 cache management is a host no-op, block copy/fill is
memcpy, context switch is the threads seam. The tool now classifies them as
HAL-owned rather than as syntax defects, which makes the honest frontier:

    compiles for host: 11,005 / 11,125  (98.9%)   + 97 asm TUs -> HAL backlog

120 real failures remain: generic syntax x44, C2732 extern-C linkage
contradictions x19, undeclared identifiers x13, unknown types x11, the rest
single digits. The takeaway stands, stronger: the port's work is platform
seams and link closure, not mass source repair. Re-run after each seam
lands; the number should only go up.

### MAP startup and cutscene integration checkpoint

The original dMap_c and eight map/OAM helpers now link into smoke_player.
Its 8-byte base profile is checked against the local overlay and ARM9 registry;
it is not interpreted as a 28-byte actor profile. The 17 player-icon pointers
are imported separately from adjacent RTTI, with each relocation checked against
the shared native ov1 descriptor island. No DS pointers or executable bytes
are copied as callable native functions.

`build3ds/validation/stage-map-profile/` confirms MAP InitResources and its
after hook complete after the preceding 38 actors. The next failure is missing
DEMO actor 352 (PC=0, factory call LR=0x0010def8, R5=0x160), not a completed
Stage or game loop. The Azahar log is preserved beside that run.
`build3ds/validation/map-common-registry/` passes three full registry cycles.

`sm64ds_demo` is now linked and registered in production. It includes the
original resource initializer, state PMFs and original callback implementations.
`unmatched/demo_models.cpp` bridges the 0x60/0x84 model layouts and destructor
slots; the generator checks their ROM slot order. Level-dependent shared handles
resolve through the currently resident, explicitly allowed level overlay.
The generated descriptors exclude RTTI and executable code, and the 13-entry
texture list stops before the adjacent RTTI word at 0x0210bd5c.

`build3ds/validation/demo-resources-complete-init/` passes all 29 model/animation
resource variants for three cycles, with 120 animation updates per animated
variant, exact heap recovery after each actor and zero Azahar unmapped accesses
or exceptions. This exercises resources and animation advancement, not complete
cinematic choreography or rendered frames. The failed first fixture (missing
Toad initialization/spawn profile context) remains in `demo-resources/`.

`build3ds/validation/stage-demo-profile/` passes DEMO initialization and reaches
43 completed actor initializations. It then times out in func_020510a4 (sound
heap state restoration). Earlier sound-heap null accesses persist because the
original sound boot is not integrated yet. See diagnosis.json and the preserved
compressed Azahar log. This is not a successful Stage construction or game loop.
Dependency inventory remains in `build3ds/validation/demo-compile/dependencies.json`.

The updated registry test also passes three cycles in
`build3ds/validation/demo-common-registry/`, including the new DEMO profile.

### Sound heap and SDAT boot integration

The explicit `sm64ds_sound_boot` target now compiles 24 objects, including
original sound-system initialization, solid-heap creation, SDAT INFO/FAT loading,
32 sequence-player settings, resource reset callbacks and boot storage. It is
not linked into the production Stage boot yet: its stream worker and capture
setup (`func_020506fc`, `func_02049cd8`) still require native implementation.
The default sound path and nine parameter pairs are read from the supplied ROM;
SDAT owner storage is 0x88 bytes and stream scratch is 0x1000 bytes.

`smoke_sound_heap_archive` validates three 1MiB sound heaps against the real
SDAT, eight save/allocate/restore cycles each, exactly one payload destructor
per rollback, preserved INFO/FAT pointers until full reset, callback clearing
on reset, file close and exact root-heap recovery (8388512 bytes). Initial
PASS evidence is `build3ds/validation/sound-heap-archive/`.

The same probe with `SOUND_GROUP_PROBE=1` exercises the original group loader.
It loads all three cached files of group1, then fails explicitly on original
sequence-data invalidation command0x1b during heap teardown. No unmapped access
or CPU exception occurs. Evidence: `sound-group-command-audit/`, including the
exact pointer range in the queue diagnostic. This is not a passing group
lifecycle, audible audio validation, or a Stage boot fix by itself.

The resource cache must be registered with the native sound service, and the
three original invalidate commands must preserve their distinct sequence,
bank and wave semantics before sound boot can be enabled. Local call sites:
`func_020515d4` -> `func_02051420` -> command0x1b (sequence/SSAR),
`func_02051514` -> `func_020513e4` -> command0x1c (bank),
`func_020514b4` -> `func_020513a8` -> command0x1d (wave).
The original bank/wave unlink helpers must remain part of teardown.

### ARM7-derived sound invalidation

`build3ds/audit_sound_invalidation.py` executes the supplied ARM7 image's three
selector loops using Unicorn. It verifies the real command jump table, range
end inclusivity, inactive-player exclusion and PCM-versus-PSG selection. Only
downstream player/channel stop calls are intercepted; this is not a complete
ARM7 mixer simulation. Evidence: `validation/arm7-sound-invalidation/reference.json`.
Image-offset routines: command0x1b ->0x6068 (current track cursor),
0x1c ->0x600c (bank pointer), 0x1d ->0x4148 (PCM sample pointer).
Player stop0x592c closes tracks through0x59bc; its relocated tail call resolves
to image0x5ab0, which changes live channel envelopes to release via0x4518.
PCM sources are wave header+12, as confirmed by image0x6584 before0x4628.

The native mixer retains original resource addresses beside its parsed copies.
Sequence/bank invalidation releases the affected players; wave invalidation
kills only matching PCM channels, including pending starts and release tails.
Retired owners remain alive until their channels finish. Full resource removal
also clears copied bank wave links, mirroring the original ARM9 unlink helper.
Explicitly invalidated wave slots reject future notes while malformed initial
bindings still report errors. No blanket stop-all or fake acknowledgment is used.

`native-sound-invalidation/` passes real title-sequence cursor/bank boundary,
wave-channel, pause and resource-reuse checks with exact root recovery.
`sound-group-invalidation/` now passes three original group1 load/free cycles
(the previous command0x1b failure), each with three cached files and eight heap
rollbacks. Both logs have zero unmapped accesses or CPU exceptions.
`sound-invalidation-regression/` preserves the existing title PCM hash b1442dcc
and passes track controls, shared variables/status, pause, 32 replacements and
tail ownership with exact root recovery.

Remaining: connect original SDAT cache loads to native resource registration;
adapt stream/capture processing and its worker; then activate complete sound
boot before Stage construction. This checkpoint does not validate audible
Stage audio or remove the production Stage's missing sound-boot initialization.

Follow-up `native-sound-invalidation-unlink/` also resumes the live sequence for
65536 samples after wave removal: no further PCM channels start. The final
`sound-invalidation-unlink-regression/` repeats the original command suite
after that change; title hash and root recovery remain unchanged.

### Original SDAT cache playback

`audio/sound_archive.cpp` preserves the allocation/read/callback path of
`func_02051634` and registers each successfully read file with its exact size.
Registration survives until queued invalidation executes; it is not removed
early by the heap callback. Production replaces only this loader boundary.
`sound-cache-playback-final/` passes three original group1 + title55 load/play/
free cycles through the original IPC node pool and native decoder. All cycles
produce nonzero PCM and recover the archive memory exactly; native service
shutdown also recovers the session mixer and its persistent output buffer.
The test warms that session allocation before measuring per-archive recovery.
PCM hashes differ across cycles because the shared hardware clock continues.
Stream/capture worker adaptation and complete Stage sound boot remain pending.

The next component, `audio/sound_worker.cpp`, replaces the SDK worker-thread
startup with a cooperative consumer of the unchanged original message queue.
`sound-worker-queue/` validates the original direct callback ABI and 128 queued
blocks over 16 ring wraps, idle polls and repeated initialization. This is only
in `sm64ds_sound_boot`: the production capture clock/audio pump does not yet
invoke it. No claim of continuous capture or audible Stage audio follows from
this queue test. The remaining direct sound-boot dependency is `func_02049cd8`
and its capture/hardware-command closure (runtime compiler helpers excluded).

### Complete sound-boot closure and channel reservations

The original capture/effect setup now compiles with the native worker: 43 boot
objects, 52 definitions and only compiler-runtime imports remain. The focused
`sound-boot-command-audit/` actually calls original `func_020133bc`, which returns
with ready=1. Submission still correctly fails on unimplemented command0x0b;
this is not successful audio boot and is not enabled before production Stage.
The pending batch is 0x16(reserve1/3),0x0b/0x0e(left PCM/capture),0x0b/0x0e(right),
0x0f(alarm),0x15(output routing),0x09(start). Scratch spans are two 2048-byte
planes, timer512, alarm first0x2100/period0x2000. Hardware capture, raw PCM,
alarm dispatch, output routing and continuous pump remain to implement.

`audit_sound_effects.py` executes the decompressed original ARM9 at its actual
0x02004000 load base; only cache flush is intercepted. Both original effects
match PCM plus persistent-state hashes on 64 blocks of 2/24/25/512 samples in
Azahar (`sound-effects-native/`), including saturation and partial blocks.

Commands0x16/0x17 now preserve strong and weak channel reservations. ARM7
`audit_sound_reservations.py` verifies lock/unlock, stop selection and allocation
with explicit versus implicit track masks. Native playback confirms immediate
stop, continued note suppression, explicit-mask override of weak reservations,
and unlock (`sound-reservations-native/`), followed by prior invalidation tests
and exact mixer heap recovery. Original sequences cannot steal the two channels
reserved by the capture setup.

`sound-reservations-regression/` also passes the original shared-command suite:
title PCM hash b1442dcc, track controls, pause/status/variables, 32 replacements,
release tails and exact root recovery. No unmapped accesses or CPU exceptions
were found in the effects, reservation or complete-boot audit emulator logs.

### Raw PCM playback for the capture path

`audio/raw_pcm.h` provides live PCM8/PCM16 DMA input, a 32-byte FIFO, initial
three-tick startup, loop/one-shot behavior and timer/volume-divider handling.
The shared mixer advances it from ARM7-clock cycles and mixes it into the same
stereo output as sequence channels. Setup command0x0b preserves native source
pointers; commands9/10 start/stop raw channels and status includes their enables.
Sequencer allocation replaces raw playback only on channels it may allocate.
Capture/alarm masks, ADPCM raw setup, manual repeat mode and hold-after-stop
remain explicit failures; boot cannot yet submit its whole capture batch.

`build3ds/reference-ds/check_raw_pcm.cpp` compares the implementation directly
with the pinned melonDS SPUChannel (interpolation disabled), including live RAM
changes while FIFO data is pending. It caught and corrected premature FIFO
prefetch at start. The final run covers 256 parameter combinations and 1,024,000
sample/enable comparisons. Build with the existing core archive and platform
shim, using the same clang++ flags as build-runner.sh but replacing runner.cpp
with check_raw_pcm.cpp. Evidence: reference-ds/raw-pcm-reference.log. This is
an emulator-SPU comparison, not a physical-DS audio measurement.

`raw-pcm-native/` passes native packed-command setup/start, independent left/right
channels, live source changes through DMA buffering, stop/status and exact root
recovery in Azahar. Complete capture and final output routing are still pending.

`raw-pcm-native-final/` repeats this on the final build and checks that an
unsupported hold-after-stop request fails without changing either channel.
`raw-pcm-regression/` preserves title hash b1442dcc and all previous sequencer
control/lifetime checks. The pinned reference core is commit
b86390e4428bf38ce4c1ce0e9ca446d6d25955e8; no core changes are required by the oracle.

### Capture DMA and hardware output routing

`audio/raw_capture.h` implements the live destination, 16-byte DMA FIFO,
PCM8/PCM16 formats, looping/one-shot and timer sharing with PCM channels1/3.
Command0x0e decodes the original capture packet; commands9/10 start/stop
captures independently of playback and shared status reports their enables.
Command0x15 selects mixer/channel1/channel3/sum output per side and controls
whether channels1/3 contribute to the capture mixer. Unsupported alternative
capture sources and addition mode fail explicitly.

`reference-ds/check_raw_capture.cpp` uses the unchanged pinned melonDS core
to compare destination RAM and enable state after every step: 72 combinations,
288000 checks, including 4/8/12-byte buffers, partial FIFO bursts, looping,
one-shot and different timer periods. Build like check_raw_pcm.cpp; evidence
is reference-ds/raw-capture-reference.log.

`raw-capture-native/` validates the entire native source mix -> stereo capture
buffers -> PCM playback chain in Azahar, direct output selectors, exclusion of
playback from capture (no feedback), capture-only stop, status and heap recovery.
This does not yet execute the original sound boot batch: command0x0f/alarm
start remains unsupported. Next: advance its timer from the same audio clock,
dispatch the original tag-checked func_0205b470 callback and drain the native
stream worker at each deadline, then test original boot before Stage/menu.

`raw-capture-regression/` retains title PCM hash b1442dcc and passes all previous
sequencer controls, variables/status, replacements/tails and root recovery.

### Original sound boot, alarms and surround

Commands0x0f/9/10 now configure/start/stop eight tagged alarms advanced by the
same integer ARM7 clock as PCM and capture. Rendering splits at each deadline;
the original func_0205b470 validates tags and invokes the original streaming
worker outside the mixer heap scope. sound-alarm-clock validates ordering,
self-stop, source writes, identical PCM/events for 1 versus1024-frame chunks,
and exact heap recovery.

Original func_020133bc is now linked into the Stage target and its boot path.
sound-boot-mode-switches-final passes original SDAT title playback through the
complete mixer/capture/PCM/alarm/worker chain:63 callbacks,zero callback errors,
mode switches0->1->2->0,and exact heap recovery after shutdown.

Command0x13 applies the original surround attenuation to channels selected by
mask0xfff5, excluding capture playback1/3. audit_sound_surround.py compares2560
cases with original ARM7 image routine0x3450:hashaca1b49b. The literal is0x7fff.
sound-surround-native-final passes the same formula hash plus live stereo
capture/playback at surround0x3000, routing, stop and exact heap recovery.

Stage now passes the earlier missing sound-heap initialization failure.
stage-complete-sound-boot reaches Model::GetVramOffset and fails while loading
file691 (last loaded resource: obj_dokan.bmd). stage-vram-diagnostics confirms
texture bank mask3 and palette mask0x30; arenas have42864/17848 bytes free.
MODEL_VRAM_TRACE logs each original allocator request without changing it.
Full Stage startup,
menu visual parity and production NDSP output are still unverified.

### Embedded particle archive integrity

stage-model-trace shows that the oversized2351760-byte request comes after
valid BMD uploads. The embedded SPA resource beginning at02075f14 was only
imported through02078000, an internal symbol inside its payload. Its header
and46 texture-record sizes establish the actual end02082128 (49684bytes).
stage_tables.py now imports this complete span, preserves binary resource
bytes despite false pointer matches, and rejects truncated texture records.
particle-archive/linked-resource.json verifies the linked ELF payload is
byte-identical to the ROM, SHA256920804d76c21e5e147c9ec0d85f32e3648416586711a1ca2708d0668241e62fb.
stage-particle-archive now passes the original Stage factory in Azahar and
returns a non-null Stage object. All46 particle textures allocate successfully.
This gate constructs the level only; it does not run the gameplay frame loop.

### Direct Stage session setup

The direct Stage probe now calls original SetNumPlayers(1), as StartFile does.
Without this, LoadEntranceObjects explicitly cleared the newly spawned player
from data_0209f394 because the session presence flag was zero.
stage-single-player-session passes Stage construction and reduces invalid
reads from22 to3; camera/door/minimap/script null-player accesses disappear.
The remaining reads are in the audio path and still require diagnosis.
This build also incorporates the menu agent GXSTAT status-register fix.

### BootScene audio baseline before Stage/menu

SOUND_ARCHIVE_TRACE confirmed INFO/FAT were freed by the first scene group
restore after standalone func_020133bc. The real BootScene continuation
func_0201a2f8 calls Sound::LoadInitialGroup(0) to save the common heap baseline.
The direct Stage probe now does the same. stage-audio-baseline passes with
zero UnmappedAccess entries and stable INFO across groups1,0,3,37 and sequence70.
run.py now preserves azahar-log.txt and records unmapped_access_count alongside
the program-level result, which alone does not prove memory safety.

### First native Stage frames

PLAYER_STAGE_FRAMES=N extends the direct Stage gate through the original
func_02044120 scheduler, graph callback and func_020132d8 audio update.
It uses gx_begin_frame to retain hardware state while clearing geometry and
renders PCM at the diagnostic original30 Hz step (not a60fps claim).
stage-first-frames passes3 frames with3079 triangles at frame2 and zero
UnmappedAccess. The capture is visually incorrect; original frame preparation
func_02019390 and PPU composition are being integrated before longer runs.

sound-boot-group-baseline-final passes group switches3,37,2 while preserving
INFO/FAT, title playback and modes0->1->2->0,63 callbacks,zero invalid accesses
and exact heap recovery. Its queue barrier is nonblocking when scene group
changes have already drained the original boot commands.

### Stage scheduler and projection diagnostics

The direct Stage loop now includes original func_02019390 frame preparation
and composed PPU output. stage-120-frames completes 120 scheduler/audio frames
with zero UnmappedAccess and 4661 triangles on frame119. This is a diagnostic
execution gate: the captured scene is still visually incorrect and is not a
playability or performance result.

The CPU-only weak cstd::fdiv replacement omitted DIVCNT side effects required
by PerspectiveW's subsequent direct divider writes. Production now links the
original fdiv and fdiv_async through hostgen. projection-divider verifies mode1,
the 64-bit quotient, and every element of a nondegenerate perspective matrix
against the original integer formula. This restores the menu's horizontal
extent. Stage has an additional, independent issue: camera eye and target stay
at zero, making LookAt degenerate after the initial frame; scaleW remains0x100.

The zero camera came from55 spline descriptors whose Elem pointers were still
DS addresses. These addresses fall inside mapped DS RAM, so reading the empty
mirror did not produce UnmappedAccess. level_scripts.py now relocates these
typed pointers and validates descriptor count/payload bounds. Packed commands
remain byte-preserved, with their existing original startup fixups.
stage-camera-paths/linked-paths.json verifies all55 linked pointers and complete
path payloads against the local ROM. stage-camera-paths-120 passes120 frames,
zero invalid accesses, nonzero camera coordinates and a nondegenerate view.
Black character silhouettes remain; menu and Stage share the GX lighting path.

stage-reference-600 now completes600 frames, including the original script
transition from020890a0 to02088a94. PLAYER_STAGE_REFERENCE captures each script
at timer120 with camera JSON as well as PPU/GX images. compare_stage_camera.py
compares against untouched retail DS RAM captures in reference-ds/cutscene:
both look-at/eye vectors and all12 view-matrix elements match exactly in both
scripts. This establishes the camera, not pixel-perfect rendering. The direct
Stage bootstrap also requires func_0201a054's four light/material writes;
Stage::BeforeInitResources intentionally skips the Scene graphics reset.

Further cinematic parity checks (October 6):
- The diagnostic now includes the original VBlank display callbacks (19144 and
  19100 after 190b8). This restores the opening letter's text upload.
- GX captures now clip at the depth planes, respect polygon/texture alpha,
  suppress repeated translucent polygon IDs and honor translucent depth writes.
  The clip matrix is combined in fixed12 before transforming vertices. Integer
  viewport positions and integer pixel sampling remove the letter's lateral
  staircase. `stage-letter-lateral-edges/letter-comparison.json` matches all four
  original DS clip vertices exactly; `gx-letter-final` checks the captured DS
  rectangle's sides, alpha, clipping and depth in Azahar without invalid accesses.
- The 2D renderer supports the intro's extended affine BG and extended palette,
  with alpha compositing of the top two raw layers. Replaying the two retail DS
  video snapshots reproduces all 98,304 lower-screen pixels. This is a renderer
  oracle, not a claim that live Stage state is already identical. The live map
  also needed the original `__sinit_ov002_0210804c` startup translation.
- Menu selection now reaches Stage through the original factory and destruction
  sequence. The shared model table needed its ROM capacity of 100, rather than
  64: this transition uses 68 entries. The 120-frame handoff includes a visible
  introduction, but does not yet establish free gameplay or save-and-quit.

The collapsed intro clouds are now fixed as well: `data_02099f88` is the ROM's
unit-scale vector, not BSS. Initializing it and the adjacent texture dimension
table restores billboards using `Render(NULL)`. The 28 linked bytes match the
local ARM9 data (SHA256 `ea06593ec5e2c08bc3b47915ed7cef629758db275be5f31021ce8a5a1b6c86fa`).
`stage-cloud-unit-scale-600` passes 600 frames, with clouds visible behind the
letter and Lakitu, no invalid accesses, and the exact camera/letter comparisons.

Current limitations include further lifecycle and level coverage, and full-game
performance/stereo validation. The fixed-size
CPU captures are diagnostic; their improved alpha raster is not yet a complete
validation of the high-resolution GPU presentation path.

The isolated `smoke_gpu_alpha` diagnostic now bypasses legacy Player bootstrap.
`gpu-alpha-isolated` passes the same 10 regions/90 pixels with zero oracle error
and zero Azahar unmapped accesses. This covers alpha/depth behavior, not a full
scene or stereo/performance validation. The real menu-to-Stage lower framebuffer
at timer120 also matches all 49,152 pixels of the DS reference (see
`reference-ds/gpu-alpha/minimap-handoff.json`).

`original-loop-16` executes the original func_020197b8 under cooperative VBlank:
16 ticks, eight native updates, no invalid accesses. Its focused single-player
link traps either wireless dispatcher if reached; those branches are not yet
linked into this probe. It does not establish a complete boot or game loop.

Decoded SWAR archives are now shared between mixer owners using the same native
resource. Weak cache entries retain no samples after player/release-tail teardown;
native resource invalidation evicts entries before addresses can be reused.
`wave-cache-shared` holds eight simultaneous owners: the first allocates 512,472
bytes and subsequent owners roughly 4,850 each, then verifies fresh decoding on
invalidation and exact root-heap recovery over three cycles. This addresses the
duplicated PCM allocations found during the long menu-to-introduction run without
increasing its eight-MiB game heap.


The experimental `PLAYER_NATIVE_COMPOSITE=1` presentation renders geometry to
256x192 before composing DS BG/OBJ layers and enlarging both physical screens.
`native-composite-outline-pass` verifies all 169 sampled letter rows, transparent
holes, palette colour order, layer priority and the 400x240 framebuffer transfer.
`native-gpu-alpha-initialized` verifies ten GPU samples including virgin coverage,
repeated translucent polygon IDs, alpha MAX, mixed textures and translucent depth.
These focused tests do not establish full-scene parity or acceptable performance;
the first full introduction attempt timed out after 100 seconds (~2.3 fps).

Renderer geometry, decoded texture cache and clipping scratch now use a malloc
allocator outside the original eight-MiB game heap. The decoded cache evicts old
entries above eight MiB while preserving textures borrowed by the current frame.
`renderer-heap-isolation` passes 40 unique 256x256 texture revisions and 1,500 extra
clipped triangles with just 65,520 bytes remaining in the game heap; that amount
stays constant and the cache reaches but does not exceed its budget. This targets
the allocation failure found after the intro reload, not complete lifecycle proof.

The decoded VRAM cache now validates complete encoded data, compressed index
words and the palette range used by each format, using exact snapshots instead
of a sampled hash. The previous key returned stale pixels in 363 mutation-test
comparisons. All 2,444 expanded comparisons now pass, including palette entry255,
all compression modes, restoration and immutable pixels borrowed by triangles;
ASan/UBSan also pass. Snapshots count against the existing eight-MiB budget.
`exact-vram-cache-heap` repeats the native pressure test with 65,520 bytes free
in the game heap and a bounded 8,257,536-byte renderer cache. The original-loop
GPU/NDSP probe `exact-vram-cache-profile` passes with zero unmapped accesses.
See `build3ds/reference-ds/gpu-alpha/vram-texture-cache.md` from the workspace
root for scope and performance evidence; this is not full-game validation.

Immutable decoded textures now carry a never-recycled version into GPU batches,
avoiding full RGBA hashing on every presentation. External and dynamic buffers
retain their respective content/update paths; dimensions and polygon alpha are
separate GPU cache key fields. `gpu-texture-versions` passes21 native mutation
images, including resizing dynamic buffers and restoring old VRAM contents.
`texture-version-profile` passes60 presentations/30 updates with NDSP and zero
unmapped accesses: mean texture phase6.720→1.401 ms (0.715 ms after the first
image), total presentation62.111→55.160 ms. This short mono probe does not prove
60 fps gameplay, stereo performance or bounded GPU cache memory.

Prepared PPU candidates now survive changes to colour-effect registers.
Engine B retains its original front colours and recomputes effects from those,
avoiding cumulative rounding. All357 full-frame comparisons pass, including
192 effect-only cache hits across both engines and window masks. The native
`ppu-effect-cache-profile` passes with GPU/NDSP and zero unmapped accesses:
candidate rebuilds34→4, PPU33.100→18.760 ms, presentation55.160→40.211 ms in
the same short intro probe. Geometry-dependent composition remains expensive;
this does not establish60 fps gameplay.

Prepared alpha composition now uses exact packed six-bit channel arithmetic
(4,194,304 scalar-oracle comparisons). Uniform2D brightness fades bypass
unneeded candidate/priority reads and combine brightness LUTs with exact
intermediate rounding. All357 whole-frame comparisons still pass. The native
`ppu-color-profile` passes GPU/NDSP with zero unmapped accesses and reduces
PPU18.760→17.183 ms, mainly on EngineB fades; presentation remains39.652 ms
in this short mono scenario, so substantial work remains for60 fps.

GPU texture retention now targets8MiB with LRU eviction after the previous GPU
queue completes. All materials for the current stereo frame stay pinned, even
if its active working set exceeds that target; the next frame trims old entries.
Allocation failure no longer silently skips textured draws. GPU-cache metadata
uses the host allocator. `gpu-texture-budget-host` passes41 revision frames,
an oversized33-material stereo frame and budget recovery with zero unmapped
accesses; the original game heap remains at65,520 free bytes throughout.

GPU frame completion now uses explicit command-list flushing, retaining the
existing per-resource uploads and adding a LCD flush for the interactive viewer
console. `PLAYER_FULL_GPU_FLUSH=1` selects Citro3D's previous whole-linear-heap
flush for comparison. The archived flush suite passes fullscreen/stereo,
21 texture mutation images,24 shadow samples and two GPU/NDSP loop profiles,
all without unmapped accesses. Overall Azahar presentation remains about40ms
in that short A/B sample; no global speedup or hardware-console result is claimed.


The actual front-facing intro exposed a culling regression not covered by
initial double-sided quads: the native offscreen Y reflection reverses winding.
Native culling now accounts for that reflection. `native-composite-culling`
checks both visible front faces and rejected back faces, plus the same exact
letter coverage and fullscreen transfer. `original-loop-native-letter-fixed`
advances the original loop for 240 VBlanks/120 updates and captures the real
intro at script timer120 with letter, text and clouds visible, without unmapped
accesses. Its `ORIGINAL_LOOP_CAPTURE_ONLY=1` option skips presentation outside
the target frame for rapid image comparisons; audio/simulation still advance.
This option refuses NDSP real-time output and is not a performance benchmark.

The fixed intro comparison reports 326/326 exact letter-edge samples, all 1,592
text-mask pixels identical, cloud-mask IoU 0.974–0.989 with centroid displacement
below 0.53 pixels, and a pixel-exact DS lower screen. The physical 400x240 top
capture is exactly the nearest-neighbour enlargement of the native framebuffer.
See `original-loop-native-letter-fixed/letter-comparison` for measured scopes.

After BG ordering/unused-effect optimizations, mono composition reuse and LCD
index precomputation, `original-loop-native-optimized-profile` passes 60 display
ticks with NDSP output and zero unmapped accesses. This remains only 4.82 fps:
GPU/readback 12.65ms, PPU 141.57ms, LCD transfer 19.32ms on that intro sample.
The prior unoptimized 240-tick run failed its audio activity check at 2.3 fps;
that failure has not been silently reclassified. Full-game 60fps is not achieved.

`original-loop-native-letter-stereo` passes the capture-only 240-tick run with
the slider forced to one. Both physical eye buffers are 400x240 and differ at
40,095 pixels. This verifies distinct stereo output, not perceptual calibration,
full-game stereo, or performance. Native composition remains an opt-in mode.


PPU scanout now traverses visible planes front-to-back and stops after the two
nearest candidates needed for blending. When no alpha effect is selected it
samples only the front candidate. `ppu-before-front.bin` and
`ppu-after-visible.bin` capture all 55 host cases and compare byte-for-byte;
the two independent DS BG3/bottom references also remain pixel-exact.
`original-loop-visible-ppu` passes the same 60-tick GPU+NDSP sequence with no
unmapped accesses. PPU time falls from 141.57 to 82.50ms and observed cadence
from 4.82 to 6.88fps. These improvements do not achieve the 60fps requirement.
The remaining cost calls for further 2D sampling/composition work and eventually
avoiding synchronous CPU composition in the main display path. Existing PPU
feature limitations (including window/OBJ special modes) remain unverified.

BG palette conversion and extended bank resolution now run once per scanout.
All 55 output snapshots remain byte-identical to the pre-optimization baseline,
and both DS lower-screen fixtures still have zero differing pixels. The measured
benefit is modest: `original-loop-palette-ppu` passes with PPU78.53ms and6.99fps
on the same60-tick intro sample, rather than the previous82.50ms/6.88fps.
Further per-pixel CPU tweaks alone have not established a route to60fps; cached
2D plane generation and GPU-side composition require investigation alongside
the original game's simulation cost. No complete-game performance claim follows.


A prepared-2D API now separates BG/OBJ sampling from each eye's GX insertion.
It retains the two nearest candidates and the original priority/second-target
rules. `PLAYER_PREPARED_PPU=1` enables exact-memory-snapshot caching in the native
compositor; palette, VRAM, OAM and relevant register changes invalidate it.
Snapshots use HostAllocator, outside the original game's heap. Engines without
GX resolve their effects at preparation time. The original compositor remains
available as an independent comparison path.

The host suite now compares165 complete frames between direct, prepared and
cached composition, including three overlapping BGs, OBJ/GX ties, holes and
selected blending targets. It separately mutates each snapshot category and
checks cache invalidation. Both original DS lower-screen fixtures remain exact.
This is functional evidence for supported PPU modes, not new coverage of bitmap,
window or special OBJ modes. ARM build passes; performance is measured separately.

The first prepared-cache benchmark was slower (101.16ms PPU) because conservative
whole-region snapshots compared unused memory. Active BG map/character spans are
now collected, merged and keyed by address/size; OBJ memory is included only
when enabled. Palette and relevant register changes remain in the exact key.
`original-loop-prepared-active-spans` passes with86 cache hits/34 misses,
68.97ms PPU and reported8.64fps over60 intro ticks. This still misses60fps.
`original-loop-prepared-letter` then advances240 ticks and produces upper,
lower and physical400x240 captures byte-identical to the previous direct path.
See its `prepared-vs-direct.json`; this capture-only run measures fidelity only.


The CPU GX raster now recognizes shadow polygon mode3. ID0 builds a stencil
from depth-failing volume fragments without drawing colour; nonzero-ID shadow
polygons require that mask and respect the receiver's opaque/translucent ID.
This removes the extra dark volume walls reported in the Yoshi comparison.
`menu-shadow-mode3-fixed` passes3600 Stage frames with zero invalid accesses,
unchanged camera/player state and112 changed pixels localized around the feet.
Twelve synthetic shadow/ID/depth cases and165 PPU comparisons also pass.
The opt-in hardware shadow implementation and its bounded tests are described
below; this CPU capture alone does not establish GPU shadow correctness.


`PLAYER_GPU_BLIT=1` replaces CPU LCD pixel conversion with tiled256x256 textures
and GPU nearest-neighbour fullscreen quads for upper-left/right and lower output.
The sampling offset matches the integer DS enlargement exactly. The
`native-gpu-blit-stereo-grid` probe checks every400x240 upper-left/right and
320x240 lower pixel using distinct colour patterns: zero mismatches. Letter
coverage/front-face tests also pass, with zero unmapped accesses.
`original-loop-gpu-blit-profile` passes60 GPU+NDSP ticks: transfer8.77ms versus
21.37ms in the prior active-span cache run, with reported9.62fps. This remains an
opt-in intermediate path, not the full-game60fps target.

### Native GPU shadow masks

`PLAYER_GPU_SHADOWS=1` applies the preflighted polygon-ID/stencil plan to native
geometry in each eye. `gpu-shadow-reset-buffer` passes12 hardware mask/ID/depth
cases; `gpu-shadow-alpha-regression` passes10 existing transparency samples.
The fullscreen mask reset explicitly dirties Citro3D buffer state when switching
VBOs, preventing the preceding geometry from being reused for the clear.
Unsupported wireframe, depth-equal and stencil-tag capacity cases currently
return failure before either eye is drawn; automatic CPU fallback is unfinished.
These tests do not establish whole-game shadow support or60fps.

`gpu-shadow-stereo` additionally passes all12 cases for each eye (24 checks).
`menu-shadow-gpu-fixed` reaches3600 Stage frames with zero invalid accesses and
renders frame3567 on the GPU: the extra dark volume sides are absent, the floor
shadow remains, and simulation/player/camera state matches the CPU reference.
This is a single-presentation diagnostic, not a throughput test.

### PPU tile rows and composition cost

Background scanout now reuses decoded eight-pixel tile rows within one synchronous
scanout. Prepared composition resolves six-bit brightness into lookup tables and
uses a nearest-layer-only path when effects are disabled.165 before/after frames
remain byte-identical (32,440,320bytes),10 window cases pass, and the Wanted DS
fixture remains exact. `original-loop-compose-fast-profile` passes60 native GPU+
NDSP ticks with zero invalid accesses: PPU57.321ms versus70.659ms before these
changes, including composition A10.233ms versus18.322ms. Reported9.71fps is still
far from60fps; this short intro measurement does not prove whole-game throughput.

### Prepared cache source ranges

The PPU snapshot now covers only potentially visible supported OBJ source ranges,
while retaining complete OAM/matrix state in the key. Extended BG palettes cover
mapped physical slots, including E/F/G OR combinations and EngineB bankH.
384 sprite-range cases and28 palette-source mutation cases pass, including writes
that must invalidate and unrelated writes that must preserve a valid cached image.
165 whole-frame before/after images remain exact;10 window cases also pass.
`original-loop-palette-spans-profile` passes60 GPU+NDSP ticks with no invalid
accesses: PPU33.135ms versus57.327ms before narrowing these ranges, reported12.83fps.
The same86 cache hits/34 misses remain. This is still far below the requested60fps.
`PLAYER_PROFILE_PPU=1` optionally reports preparation phases using a caller-supplied
clock; profiling is disabled by default and does not alter render decisions.

### Playable scene profile after the PPU cache changes

`yoshi-gpu-profile` completes3627 Stage updates with zero invalid accesses. Its
capture3567 matches the previous validated GPU scene exactly (both logical
screens, geometry, physical upper screen and same-frame CPU image). It presents
30 gameplay frames and30 paused frames; excluding initial capture/allocation I/O,
29 gameplay presentations average43.976ms and30 pause presentations56.310ms.
Mixed GPU/PPU/transfer averages are13.971/18.090/11.600ms. This mono, one-present-
per-update diagnostic is not a60Hz game loop or a stereo throughput proof.
