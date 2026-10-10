# Native 3DS port: current implementation

The current executable is `sm64ds_viewer.3dsx`, not the complete game. It uses
recovered model, animation, texture, and collision code, with a native Citro3D
renderer. Mario, Luigi, Wario, Yoshi, Mad Piano and three stage models are
available. Player gameplay, stage actors, progression, menus, saves and audio
are not integrated. Do not interpret the capture runner's PASS as completion
of the game.

## Rendering work validated on 2026-10-05/06

- 400×240 stage projection fills the upper screen with the correct 5:3 aspect.
- Separate perspective and orthographic stereo displacement; convergence follows
  the orbit camera distance, including collision corrections. The CPU comparison
  image is monoscopic. Stereo can be forced for headless capture.
- Camera segments use the recovered KCL collision walk and stop before walls.
- Switching to a stage without collision no longer retains the previous spawn.
- Static stage geometry is decoded once on stage entry, with owned texture
  copies; only the camera projection changes every frame. Dynamic character
  animation still runs through the recovered model pipeline. This is specific
  to the viewer's static stages, not a cache for animated stage objects.
- Texture resolution is shared between batches and both eyes within a frame.
- Before/after static-geometry caching: 24/24 eye captures pixel-identical.
  Three stages improved from ~29.92 to ~59.82 fps in Azahar, with stereo enabled.
  These are emulator measurements of the viewer, not full gameplay/hardware
  performance claims.
- All 14 character/animation combinations produced distinct left/right images.
  The stereo comfort and display on physical 3DS hardware remain unverified.

## Local iteration

Tools live in the workspace's `build3ds/` directory. They use the installed
`/opt/devkitpro` toolchain and the supplied Azahar bundle. Prefix commands with
`rtk proxy` per the workspace instructions.

```
bash build3ds/build.sh sm64ds_viewer_3dsx
python3 build3ds/run.py build3ds/cmake/sm64ds_viewer.3dsx \
  --env SM64DS_FORCE_3D=1 --env SM64DS_AUTO_LEVELS=1 \
  --env SM64DS_BENCHMARK_FRAMES=60 --env SM64DS_PROFILE=1 \
  --output build3ds/validation/levels --timeout 180
```

The runner serializes access to the SD card, terminates Azahar before and after
each run (SIGKILL fallback), restores env.txt/autoexit, records fresh direct
framebuffer captures, and fails on FAIL or timeout. Without AUTO_LEVELS, the
viewer captures all character clips through both GPU and CPU paths. Logs and
BMPs are retained under the selected output directory. No screen recording is
needed.

`build.sh` with no target builds every gate and returns the real CMake failure.
The actor gate now links; its runtime validation is tracked below. The old build
script silently exempted its failures. `test_all.sh` now fails when any gate is missing or fails.

## Remaining integration steps

1. Resolve `smoke_actor`'s actual source/link closure and validate spawn,
   initialization, behavior, rendering and teardown in Azahar. The existing
   mega-character stub is only a test seam, not a gameplay implementation.
2. Integrate actual Player, stage loading/spawn data, camera and input. Compare
   movements and collisions against the supplied European DS ROM in melonDS.
3. Integrate level actors, transitions, missions/stars, menus, saves and audio.
4. Validate full progression and timing; preserve original simulation timing
   while rendering at 60 Hz. Audit stereoscopy, HUD and bottom-screen UI.
5. Run sustained performance, memory, reload and gameplay regression checks.

The user's reference ROM is in Downloads, named
`0022 - Super Mario 64 DS (Europe) (En,Fr,De,Es,It).nds`.
Existing user changes in the decompilation and HAL predate this work and must
be preserved.

## Actor integration checkpoint

The first actor build revealed duplicate native/generated `ActorBase.cpp`
objects and GCC C1/C2 variants crossing slice boundaries. The manifest now
uses the generated ActorBase once, and the complete actor slice union is
filtered for constructor/destructor variants. CMake now tracks slice content
changes as configure dependencies. Recovered actor, Player, collision, matrix and allocator sources now compile.
Player's local C extern declarations need an opt-in hostgen adaptation: aliases
preserve raw link names while keeping each recovered call-site declaration.
`test_hostgen_gcc.py` compiles and links representative conflicting declarations
with devkitARM. Raw-word soft-float conversions pass twelve boundary checks in
`test_softfloat_abi.py`; physical 3DS execution of those checks remains pending.

Native factory veneers explicitly forward r2/r3 (spawn parameter and kind),
which the original tail-call assembly preserves but a fresh GCC compilation of
the recovered C++ did not. Initialization member-function tables are generated
by the native C++ compiler. Earthquake's camera call likewise keeps all three
register arguments. Original recovered sources are unchanged for these fixes.

The actor gate now links with no unresolved symbols. Original sound command
queue code is compiled; IPC MMIO is generated through hostgen, and any attempted
ARM7 FIFO send fails explicitly because a native audio backend is still missing.
This avoids falsely acknowledging commands and hanging in completion loops.
The initial Azahar lifecycle run spawned and initialized the sign and executed
four behavior ticks, but rendered zero triangles. The cause included an old
Model vtable filler overwriting GCC's real table (which has ABI header words).
GCC now retains its compiler-owned table; MSVC's synthetic table has corrected
method slots. The subsequent Azahar run passes: InitResources=1, four behavior
ticks, Render=1 and19 emitted triangles. Evidence:
`build3ds/validation/actor-native-vtable/log.txt`. The test still does not cover
teardown, a real Player, full processing-list scheduling, or audio.

`build3ds/link_closure.py` emits raw-symbol diagnostics in
`smoke_actor-closure.json`. No new no-op gameplay stubs were added. Compiling
Player and exercising a sign actor do not mean Player gameplay is runnable.

## DS ground reference (2026-10-06)

A headless runner of the original melonDS core, using the supplied ROM, lives
in `build3ds/reference-ds/`. Its framebuffer captures, RAM and save state provide
an independent DS reference, without desktop screen recording.

The original Stage::RenderModel uses scale 125; Player translates world
positions by 1/8. Thus the viewer's unscaled stage model uses **1000 world units
per model unit**, and the character scale is **1/125**. The previous 16 and
1/112 factors were incorrect. The native raycast converts world endpoints
with >>6 and restores world hit coordinates with ×64; the old port override
had removed those conversions. Both the override and collision tests now use
the DS semantics. The character's tiny Fix12 scale still quantizes to 1/128;
removing this remaining ~2.34% scale error needs higher precision normalization.

Original DS Yoshi resting position: (-1200,254,6800). Castle grounds now tests
that precise position first. `SM64DS_REFERENCE_DS=1` selects Yoshi there.
Azahar log: `DS spawn floor: hit=1 y=254.000000 expected=254.0`.
The collision gate passes 25 probes, 8 hits, 8 upward normals. Viewer stereo
still measures ~59.82 fps with Yoshi displayed. Artifacts:
`build3ds/validation/ds-reference-world/` and `collision-world-units/`.
The DS diagnostic now includes an awake Yoshi state at the same coordinates.
Reference mode also uses the native camera position, target, vertical FOV and
180-degree facing for the first grounds capture. Full bone/body matrix
composition replaces a scalar-only head transform that could invert the head
when the actor turned. `build3ds/validation/ds-reference-camera/comparison.png`
shows the aligned viewpoints before the final FOV correction. The native GX
projection measures fy=675/256 after camera interpolation; reference mode now
uses that final value. The 3DS has a wider horizontal field and missing
stage actors (trees/signs); animation phase and character scale still differ,
so this is not a claim of pixel-identical gameplay. All 14 standalone character
left-eye captures remain pixel-identical after the head-matrix composition fix.

Mario has a separate reference camera, extracted from `mario-spawn.state`.
Use `SM64DS_REFERENCE_DS=1` plus `SM64DS_REFERENCE_MARIO=1`; his native focal
length is605/256. `build3ds/validation/mario-ds-reference/comparison.png` compares
the supplied DS game's diagnostic Mario state to the 3DS viewer. Both place
Mario at(-1200,254,6800) with zero sinking offset. The final capture shows no
large foot penetration; pose phase, 2.34% scale quantization, missing shadow and
missing stage actors remain visible limitations. Measured stereo fps:59.82.

## Factory integration

The default smoke_actor path now uses the original actor factory and
initialization Process dispatch instead of calling the constructor/init slot
manually. Real dActor_c BeforeInitResources/AfterInitResources and the death
state query now replace the former HAL traps. Sublevel-part data comes from the
local ROM; death-table storage follows the original192-byte BSS extent.
The factory checks parameter preservation, alive state, context reset, and
parent/child scene linkage through dBase_c::Spawn. Behavior/render now run through native Process callbacks and the scene
scheduler, as described below. Player and full scene loading are pending.
The parent/child test exposed a48-byte matrix write into a16-byte HAL scratch
buffer, corrupting the adjacent model-resource table. `data_020a0e68` is now
12 words, matching its ROM BSS extent ending at020a0e98. The test checks both
resource pointers and their reference counts after two actors initialize.
Azahar evidence: `build3ds/validation/actor-factory-scratch-fix/log.txt`.
`SM64DS_ACTOR_DIRECT=1` retains the earlier direct-constructor diagnostic.
FATAL diagnostics now terminate the run as FAIL immediately instead of waiting
for a timeout. The launcher still stops Azahar before and after each run.

## Per-frame actor scheduler

The gate now includes native `BeforeBehavior`/`AfterBehavior` and
`BeforeRender`/`AfterRender`, their Process dispatchers, visibility/area checks,
and the native Clipper setup. The former8-byte test profile was insufficient:
the dActor constructor reads0x1c bytes, including flags and clip distances.
The test uses the complete YAJIRUSI_L profile measured at overlay98:0213c398,
with a native factory pointer, and supplies a visible area/camera.

Pause, hidden area, explicit hiding and behind-camera rejection are checked,
then visibility is restored and rendering verified. The original scheduler
`func_02044120` now walks cleanup/init/behavior/render lists and reconciles the
scene tree using native compiler-encoded callback tables. Two registered sign
actors emit38 triangles on the first verified scheduler frame; neither actor
is invoked manually during that scheduler pass. Extended validation passed120
frames in Azahar, with38 triangles every frame, phase reset and stable
shared-file reference counts. Evidence:
`build3ds/validation/actor-scheduler-120/log.txt` and `run.json` (binary SHA256).

This does not yet test actor destruction, Player gameplay, stage loading or
full rendering integration into the interactive viewer. The pre-existing
shadow and mega-character test seams also remain incomplete.

## Player integration checkpoint (2026-10-06)

`smoke_player` is a separate native integration target derived from the validated
actor target. It installs the ROM's Player profile with native function addresses
and retains the 0x768-byte Player layout (compile-time assertion). Its intended
first runtime gate is actual InitResources at the measured DS spawn. It is **not
yet linked or runnable**: the current closure report has228 unresolved symbols.
Original recovered sources remain unchanged; source enrollment is in
`slice_gate_player.txt`. More movement/collision/camera/particle dependencies
now compile. Recovered inline DS assembly in InitResources is translated to
its equivalent20-byte clear in `unmatched/player_init.cpp`. The POD Vector3
cleanup callback explicitly retains the ROM's empty-destructor semantics;
GCC had optimized away the addressable copy needed by array allocation.

`tools/overlay_bss.py` generates ov002 storage from its configured section
bounds and symbols. All1165 BSS names retain exact relative offsets within
0x3800 bytes; Player::ST_WAIT and data_ov002_02110154 share the same address.
This was checked against the compiled ARM object's symbol table and section
size (`build3ds/player-bss-validation.txt`). Storage alone does not initialize
Player resources or state callbacks. The next required step is native binding
of the PMF tables and execution of `__sinit_ov002_021019d0`, plus the remaining
source/data closure. Raw DS code pointers must never be copied as native calls.
No Player emulator run was attempted after these failed link attempts.

### Native Player state callbacks

The Player static initializer `__sinit_ov002_021019d0` is now compiled and
called by the integration gate before spawning. `tools/player_callbacks.py`
decodes its216 nonvirtual PMF constants from the local overlay, resolves each
ROM address to a function symbol, and lets GCC encode native member pointers.
Member declarations bind directly to the native function symbols, with no
forwarding wrapper that could lose extra callback arguments. Unsupported
adjustments/virtual entries or an unexpected table count stop generation.
All216 compiled ARM data relocations were checked against the ROM target
mapping (`build3ds/player-callback-validation.txt`). This verifies bindings,
not execution of these states.

The newly enrolled state functions now compile, including a port-only copy of
St_HoldLight_Main that moves a scalar declaration before a goto without moving
its assignment. Static palette initialization func_ov002_020e6ad4 goes through
hostgen so its DS register access uses translated I/O. Source/link closure is
still incomplete; see the fresh `smoke_player-closure.json` for authoritative
remaining symbols. No Player executable or gameplay pass is claimed.

### Player command/settings storage and regression

The four0x18-byte command records at DS0209f498..0209f4f8 now preserve
all field-symbol offsets in one native block (`unmatched/player_globals.cpp`).
The current-player index, destructor-registration head and null PMF also have
storage. The mutable ROM settings02092110..0209213c are extracted locally as
one44-byte block; all11 word aliases were verified against the compiled ARM
object. No guessed initialized values or independent overlapping arrays are used.

Two further source compatibility issues are handled in the port: the terminal
ret0 goto in func_ov002_020e2ea0 becomes return0, and the missing declaration
for func_ov002_020c0cbc is supplied to its caller. Additional source closure
compiles; remaining link symbols are recorded in smoke_player-closure.json
(currently374). DS runtime assembly for func_01ff99a4 and func_01ffa344 needs
native adapters with the actual register ABI, not direct compilation of mwcc
assembly blocks. The Player target remains unlinked.

The shared ROM-data generator change was regression-checked by rebuilding and
running smoke_actor in Azahar: native factory, initialization, visibility gates,
and120 scheduler frames with38 triangles/frame all pass. Evidence:
`build3ds/validation/actor-after-player-data/` (binary hash in run.json).
The runner terminated Azahar; no emulator process remained after the run.

### DS floating-point runtime adapters

`func_01ff99a4` is confirmed from its ARM instructions to be raw-word float
comparison `>` (movgt1/movle0). `func_01ffa344` truncates float words to signed
integers; it saturates out-of-range/NaN values by sign, accepts exact-2^31,
and sets the DS status word's invalid/inexact flags. Both now have native
raw-register adapters. Less-than also records the original invalid flag on NaN.
The conversion uses integer decoding to avoid undefined host casts on overflow.
Enabled DS exception traps still stop with an explicit FATAL: their dispatcher
is not implemented. Ordinary masked exceptions retain their result and flags.

Host boundary tests pass. The dedicated `smoke_softfloat`3DS executable also
passes in Azahar:15 conversion cases and the Player's int-to-float/comparison
chain. Evidence: `build3ds/validation/softfloat-native/`, with binary hash.
Azahar was terminated after the run. Both symbols now resolve in the Player
link;372 other symbols remain. `test_all.sh` includes both the new arithmetic
gate and the still-unbuilt Player gate, so it cannot report all gates passing
while Player integration is missing.

### Native overlay read-only data

`tools/overlay_rodata.py` now extracts ov002's5452-byte rodata section into
the build directory and preserves85 symbol offsets. Its900 pointer words
are native relocations:899 target the existing overlay BSS, and one targets
the separately extracted12-byte material name at0210d6d0. Every target is
validated against the relocation map and original word; unknown, foreign or
code targets are rejected. No raw DS pointer is used as a native address.

The compiled ARM object was checked independently: all non-pointer bytes match
the ROM, all85 symbol offsets match, and all900 relocation targets resolve to
the original intended symbols. Evidence: `build3ds/player-rodata-validation.txt`.
This supplies the Player's numeric and resource-reference tables. The current
Player link has333 remaining symbols; no Player runtime test is possible yet.
The neighboring mutable .data area mixes PMFs, numeric values and pointers;
it must not be imported wholesale without the same relocation/ABI review.

### Player mutable tables and additional callbacks

The callback generator now includes14 additional direct action PMFs referenced
outside the static initializer; all230 compiled relocations match their ROM
function targets. `tools/player_constants.py` imports15 explicitly reviewed
mutable spans (976 bytes) while excluding the DS profile, raw vtable and RTTI.
All32 pointers are rebound to native data symbols; compiled non-pointer bytes
match the ROM (`build3ds/player-constants-validation.txt`).

Additional recovered functions compile, but Player still cannot link (359
current unresolved symbols). The newly exposed DS file-system, crash-screen,
reset and IPC dependencies require native-platform review; indiscriminately
compiling more DS kernel code will not supply a functional3DS backend. No
Player emulator run has been attempted with an incomplete executable.

### Native synchronous file streaming

The HAL now supplies FS_InitFile, path-to-file-ID (func_0205d644), open-by-ID
(func_0205d568), FS_ReadFile and FS_CloseFile. The open-by-ID adapter explicitly
accepts the two-word ID passed in r1/r2; recovered variadic C relied on the
original register/stack layout. Public FSFile start/end/current offsets are
preserved, so original func_0205d368 remains the seek implementation. Native
stdio reads extracted FAT files without entering the DS card/IPC queue. The
native archive token is opaque; it is not an original DS archive pointer.
Only this synchronous interface is covered; arbitrary DS archive providers,
asynchronous file commands and the sound mixer are not implemented here.

Azahar smoke_fs passes path/open/seek/partial-read/EOF/close/error/reopen guards,
plus the original SharedFilePtr raw/LZ77/refcount tests. It also opens the actual
4,456,832-byte sound_data.sdat (FAT117), reads its SDAT header, seeks to INFO at
45776 and checks independent ROM header bytes. Evidence:
`build3ds/validation/fs-native-sdat/` with binary hash. Azahar was closed after
both runs. This validates sound-archive access, not audible game audio. The
Player target remains unlinked; its last report contains358 unresolved symbols.

### Native animation destruction and script fader declarations

Enrolling the actual ModelAnim2 destructor supplies its GCC vtable. A native
lifetime test then exposed the old fatal ModelBase destructor placeholder in
ctor_bridge.cpp. It is removed; slice4b now compiles the recovered destructor
and Deallocate veneer. Azahar smoke_modelanim passes16 empty construction/
destruction cycles with verified0x50/0x68 animation offsets and0x78 object size,
then the existing animated-pose/wrap test and16 populated model/animation
lifetime cycles. Evidence: `build3ds/validation/modelanim2-loaded-destruction/`.
The earlier intentional failure is retained in `modelanim2-native/`; it is not
a passing result. Shared BMD/BCA resources remain alive throughout the loaded
cycles, so this is not yet a complete asset unloading/refcount test.

A port-only ProcessKuppaScript copy uses shared FaderColor/FaderBrightness
headers instead of its incompatible local shadow class. Color remains at0xc;
virtual fade methods now use their real one-argument signatures. The original
script source is unchanged. Both phantom two-argument symbols disappear from
the link report. Additional initialization BSS uses configured extents, and
Player action callback bodies are enrolled. Player remains unlinked with339
unresolved symbols; no full gameplay or script-runtime result is claimed.


### Native crash diagnostics and script dispatch bindings

ShowCrashScreen/InitCrashScreen now report engine phase, last resource, pending
actor and iterator to the native log. Repeated identical snapshots are throttled.
The DS VRAM/BG crash display is excluded from Player; full register/heap dumps
remain unported. Crash() invokes the diagnostic through a weak hook and retains
its fatal abort. Actor regression passes120 scheduler frames and diagnostic
reporting (`validation/actor-native-diagnostics-final`). The deliberate Crash()
run (`validation/actor-native-fatal-diagnostics`) correctly returns FAIL with a
caller address and no PASS. Both runs terminated Azahar afterward.

The24 PMFs used by func_ov002_020f7d74 now bind directly to native function
symbols with the dispatcher's argument signature, without wrappers or DS code
addresses. Their recovered bodies compile. All24 compiled relocation targets
and zero adjustments match the local ROM (`script-callback-validation.txt`).
This is script dispatch infrastructure, not proof of runtime camera/cutscene
behavior. The Player animation table at0210a7e8 adds19 IDs; its span ends before
the vtable header at0210a834. All1052 imported bytes and32 data relocations pass
independent ELF/ROM checks (`player-constants-validation.txt`).

Latest Player link:271 unresolved symbols. Player has not yet linked or run;
full gameplay, progression, audio and complete-game60fps remain unverified.


### Constructed native faders and synchronous palette transfers

The script-global FaderColor at data_0209b294 now uses its real C++ constructor
and compiler-native virtual table, reproducing the corresponding portion of
__sinit_02073e6c. It starts with interpolation4096, speed0, color0 and retains
its0x10-byte layout. Original Fader/FaderBrightness/FaderColor methods and
native destructors supply virtual dispatch. The full ARM9 initializer is still
not ported; this change initializes only the named color fader.

A dedicated smoke_fader3DS gate exercises black/white16-frame transitions,
reverse and instantaneous fades, endpoint queries and both DS register banks.
It also verifies master brightness and backdrop palette reload through CPU
and synchronous DMA branches. Register adapters translate DS pointers through
ntr::xlate. Halfword transfer adapters preserve the original byte-count behavior
(CPU copies pairs while offset<size; DMA copies floor(size/2) pairs). They are
synchronous copies, not a complete asynchronous DMA controller implementation.

Azahar result: PASS in build3ds/validation/fader-native-palette, with binary
hash and log. Azahar terminated after each run. This proves fader state and
register/palette behavior, not final compositing into the3DS scene. The new gate
is included in test_all.sh. Additional recovered Player/script/math functions
compile; the latest Player closure has258 unresolved symbols. No Player binary
or full-game runtime validation is available yet.


### Message state and ROM tables

message_bss.py supplies the contiguous ARM9 message region0209d64c..0209e64c:
4096 bytes with51 symbol aliases, including60 character widths and960 glyph
words. Offsets remain shared rather than allocating overlapping arrays
independently. This is storage only; message/font resource initialization is
still required before drawing. The world RNG is initialized to1, matching
__sinit_02074e44 and its direct seed setter; its original destructor is empty.

message_data.py imports0208ee44..0208f274 (1072 bytes,18 symbols), covering
message numeric/character tables. The three MSG_GEN_TEXT_FUNCS entries are
rebound to native functions and their recovered sources compile. Unexpected
relocations are rejected. Independent compiled ELF checks verify every table
byte (excluding the three relocated words), every symbol offset and all three
function targets, plus the complete BSS extent and aliases. Evidence:
build3ds/message-data-validation.txt. No ROM data is added to source files.

Latest Player link:201 unresolved symbols. The change has object-level evidence
only; dialogue rendering and Player runtime remain unverified. No emulator was
launched for this unlinked target.


### Particle callback ABI and shared star markers

The recovered base/simple/edStarKira particle callback methods now compile in
the Player target and supply native vtables/type information. Their tracking
record helpers are enrolled; particle emission still depends on the unlinked
func_0204c584 closure. Native size assertions verify8-byte simple callbacks and
0x310-byte star callbacks. cstd::div is compiled through hostgen so DS divider
register accesses use the existing MMIO backend. Save-data rabbit/collection
count functions used by generated message text also compile.

STAR_MARKERS and data_0209f40c now alias the same48-byte native array of12
pointers. The corresponding type array data_0209f37c occupies12 bytes. Compiled
ELF evidence is recorded in build3ds/player-star-state-validation.txt. This is
storage/layout validation, not proof that stars or particles function in-game.
Player still cannot link:196 unresolved symbols in the latest closure report.


### Native camera state/script callbacks and level tables

camera_callbacks.py binds77 nonvirtual PMFs used by __sinit_02073a24 and
func_02008550 to compiler-native callbacks with direct symbol labels. No
wrappers disturb argument registers. It also preserves668 bytes of camera BSS:
the dispatch flag,19 state pairs and39 script entries (21 configured aliases).
The actual state initializer is enrolled and called by smoke_player; the script
dispatcher retains its original lazy initialization. All newly referenced camera
callback bodies compile. This does not yet prove runtime camera behavior.

Independent ELF/ROM validation confirms all77 function targets and adjustments,
and every BSS offset (build3ds/camera-callback-validation.txt). Voice bases and
the52-entry SUBLEVEL_LEVEL_TABLE are generated from the local ROM; all60 bytes
and the named alias are checked in build3ds/level-table-validation.txt.

The rebuilt actor regression passes120 scheduler frames after shared ROM data
changes: build3ds/validation/actor-after-camera-tables. Azahar was terminated.
The Player integration target still fails linking with190 unresolved symbols;
newly exposed camera functions/tables remain to integrate. No camera/Player
runtime or complete-game performance claim follows from the actor regression.


### Camera calculation closure and numeric parameters

Thirty additional camera/math functions compile in the native Player target,
including collision adjustment, dialogue camera state capture and interpolation.
The pointer-free camera spans02086e84..6ecc,02086ed8..6ee4,02086ef0..6f7c and
02086fcc..751c are generated from the ROM. These deliberately exclude RTTI,
vtable headers, vtables and level-camera entries carrying pointers. The
extractor rejects any relocation found inside a numeric span.

Independent ELF comparison verifies all1584 bytes,40 symbol offsets and absence
of relocations (build3ds/camera-constants-validation.txt). These are compiled
parameters, not a tested in-game camera. Player still fails linking with144
unresolved symbols. No unlinked or stale Player executable was run.


### Camera path/math closure and script input storage

The recovered dot product, vector comparison/normalization, camera adjustment,
path-loop query and spline interpolation functions now compile. The normalization
helper's apparent temporary output is intentional: func_0203ce80 normalizes its
source in place before copying it. Original sources remain unchanged by this
integration step.

Script actor-count/frame/transition globals use their configured four-byte BSS
extents. Two16-byte blocks preserve four per-player input records and their five
byte/halfword aliases. Compiled ELF evidence:
build3ds/player-script-input-validation.txt. This supplies storage, not native
3DS input polling; controller-to-game synchronization remains to implement and
verify in the full loop. Latest Player link has126 unresolved symbols; no
Player executable was produced or tested in Azahar.


### Sound state defaults and long-sound storage

ROM generation now preserves the complete120-byte sound state/default region
0208e420..0208e498, replacing the isolated current-bank byte. Ten interior aliases
share its storage, including the ten{playerID,maxSeq} defaults. Every byte and
alias offset matches the ROM; no pointers occur in this span. Evidence:
build3ds/sound-state-validation.txt.

Missing four-byte sound state and scene-script flag fields are allocated with
their configured BSS extents. The0x564-byte long-sound slot table at0209b53c is
also allocated, and smoke_player calls recovered __sinit_02074da8/func_02011a5c
to initialize64 slot flags and16 bookkeeping words. This is not the SDAT archive
object and does not implement the mixer or audible playback.

The rebuilt actor regression passes120 frames in Azahar after the shared-data
change (validation/actor-after-sound-state). Azahar terminated after the run.
Player remains unlinked with102 unresolved symbols; its new initializer has
compiled but has not executed in a Player integration test.


### Particle emission/math and scene state integration

Recovered particle emission, perpendicular-basis and random-direction helpers
now compile for Player. The shared four-byte BSS particle RNG preserves both
original symbol names; the eight-byte basis-vector region matches the ROM.
Evidence: build3ds/particle-data-validation.txt. The host-generated 16-bit
normalizer routes divider, square-root and IME accesses through the DS I/O HAL.
smoke_particle_math passes seven integer-reference vectors (axes, signed,
diagonal and extreme components), each with both IME states, in Azahar:
build3ds/validation/particle-native-math. Azahar was terminated after the run.
This verifies direction math, not rendered particle effects.

Scene BSS generation preserves 356 bytes and 52 aliases at0209d454..0209d5b8,
including the fade pointer and file state. Compiled offsets verified in
build3ds/scene-state-validation.txt. Storage does not replace scene initialization.
Ending-dialogue preparation, sequential BG tile setup and sub-screen offset
functions also compile; their game execution remains unverified. The BG0 screen
pointer accessor uses host-generated register access.

Fresh Player link closure is85 unresolved symbols (previous checkpoint102).
Player remains unlinked; no Player executable or complete gameplay was tested.
Original recovered source files were not edited by this integration step.


### Player HUD resource setup and sequence-transition state

Native Player now compiles the recovered HUD resource initializer020f5fe4,
its six component resets, sub-screen tile transfers, palette loaders and
VRAM bank register helpers. Direct DS registers in these new hardware-facing
functions are routed through hostgen/MMIO. No HUD rendering is claimed yet.
The LoadFile entry and its file/archive branch bodies also compile; their
archive mounting, file-ID and byte-copy dependencies still need integration.

Six sequence-transition/streaming fields now have reviewed native BSS storage:
two track IDs, two fade rates/targets/current values,12-byte callback context,
and192-byte scratch (232 bytes total). Object-file sizes and NOBITS placement
verified in build3ds/audio-transition-state-validation.txt. This supplies the
actual storage used by the recovered routines; it does not implement audio output.

Fresh Player closure now has94 unresolved symbols after expanding HUD/file
loading dependencies. All newly enrolled functions compile, but Player still
fails linking. No Player binary was run and no new emulator validation is
claimed for this checkpoint. Next frontier includes archive catalog pointers,
CpuCopy8 native implementation, file-ID/open/read adapters, and VRAM bank state.


### Checked game file adapters and native byte copies

The FS HAL now supplies game-level file-ID construction, checked open and
checked read.02018dc4 explicitly receives the two-word NativeFileID aggregate;
it cannot reuse the recovered variadic &b trick, which assumes r1/r2 spill
adjacently. Native ROM archive identity is used by0209d3b4. Failed checked I/O
reports FATAL and aborts; DS error-screen/thread-sleep handling is not ported.

CpuCopy8 copies exact byte counts through native address translation, preserving
adjacent bytes. Physical DS halfword-bus restrictions do not apply to the mapped
native buffers. smoke_fs now links ntr and verifies288 source/destination
alignment and length combinations, zero-length null pointers and a translated
VRAM destination with sentinel bytes. The actual SDAT file opens through the
game-level adapters and its header/INFO offset match expected values.
PASS: build3ds/validation/fs-game-adapters-copy; Azahar terminated after the run.
Existing raw/LZ77, refcount and stream regressions also pass in that run.

Original archive lookup/load/unload bodies and explicit-argument heap veneers
are now enrolled and compile. Archive mounting/unmounting and native catalog
pointer reconstruction remain necessary. Fresh Player link closure:88 unresolved
symbols. No Player executable, complete gameplay or audible audio is validated.


### Native archive catalog and checked pathname opening

archive_data.py reconstructs the13 mutable NARC descriptors (260 bytes),
replacing their26 DS name/path addresses with native string relocations. The
generator rejects unexpected initial loaded pointers, ranges or relocation
maps. Compiled ELF comparison verifies all numeric fields, pointer destinations
and exact string bytes against the supplied ROM. Archive mount index (-1) and
LZ77 marker/padding are imported through romdata.py. Evidence:
build3ds/archive-data-validation.txt. This is not a runtime archive-mount test.

The native checked pathname adapter02018d98 resolves the catalog path and uses
the checked ID opener. smoke_fs opens /ARCHIVE/arc0.narc, verifies its NARC
header and closes it, alongside the existing copy, raw/LZ77, refcount and SDAT
checks. PASS in build3ds/validation/fs-archive-path; Azahar ended after the run.
This validates whole-file opening, not mounted-member access.

Player still fails linking with84 unresolved symbols. Native archive mount/
unmount and member streams remain to implement, as do the other Player state
and hardware dependencies. No full gameplay,60fps gameplay or stereo completion
is claimed.


### Native memory-backed NARC mounting and member streams

FS HAL now implements0204ee40/0204ee10 with a native mounted-archive registry,
validated NARC block/member bounds and the recovered +0x50/+0x54/+0x58 raw/FAT/
image pointer layout. ID opens use the mounted object identity and member index.
Memory streams retain FSFile start/end/current offsets, so the game's original
seek function runs unchanged. Closing decrements archive ownership; unmount with
open files aborts explicitly to prevent the original heap-release caller from
freeing live stream memory. Duplicate owner mounts, invalid indices and stale
archive identities are rejected. Name-based member lookup is not implemented;
this seam supports the numeric member/direct-pointer routes used by LoadFile.

Azahar smoke_fs reads all198 members of the supplied arc0.narc:316042 bytes with
FNV-1a22c6ae26, independently computed with ndspy. Every member is also rewound,
checked through its native direct-pointer layout, closed and checked for invalid
post-close reads. Duplicate open, end-exclusive index, unmount/remount and stale
identity checks pass, alongside existing FS regressions. Evidence:
build3ds/validation/fs-narc-members. Azahar terminated after the run.

This validates one real archive through the native FS surface, not the complete
LoadArchive/LoadFile gameplay chain or all13 archives. Player still fails linking
with82 unresolved symbols. Full gameplay remains untested.


### Reviewed Player/scene state storage

Added22 configured four-byte BSS slots used by Player/scene flags, counters and
resource pointers; character/script actor unique-ID tables (4 and16 entries);
the48-byte inverse view matrix;30 course star bitfields; crash-key state and
collision-processing list head. Original constructors/scene/save loading must
populate this storage before use. In particular, a zero inverse matrix or null
level-resource pointer is not considered valid initialized gameplay state.

The versus-star and visibility arrays each cover four player bytes and preserve
the interior +1 labels. They stop before existing area/camera and neighboring
state objects, avoiding the misleading16-byte declaration in one recovered TU.
Compiled ELF validation confirms29 objects/258 bytes plus two4-byte aliased
blocks: build3ds/player-state-storage-validation.txt. These are storage checks,
not Player behavior tests. No emulator run was made for the unlinked target.

Fresh Player closure now has51 unresolved symbols, predominantly remaining
constant tables, platform state and reset/IPC functions. Full Player linking,
initialization and gameplay remain outstanding.


### Numeric Player/scene and cutscene tables

ROM generation now includes312 reviewed ARM9 bytes: character exit-wipe types,
next-level overrides,128 sequence-volume entries, two camera parameter pairs,
four mutable health values and scene ID/padding. These spans have no configured
relocations. Overlay2 constants add96 bytes: a mutable cutscene selector, three
vectors and14 four-byte animation/texture-index records. The records contain
indices/flags, not raw pointers.

Compiled bytes match the supplied ROM, and new overlay spans have no native
relocations: build3ds/player-scene-numeric-validation.txt. The overlay constant
generator now covers1148 bytes (existing32 pointer relocations unchanged).
Fresh Player closure has39 unresolved symbols. These data checks do not verify
runtime camera, health, transitions or cutscene playback. No Player executable
was produced; full gameplay remains outstanding.


### Scene color fader and collision-result destructor table

The scene color fader at0209f5e8 is now a native FaderColor object, reproducing
its portion of __sinit_02074edc (constructor, valid vptr, interpolation4096,
speed0,color0). smoke_fader checks these defaults and four virtual fade steps,
then runs the existing black/white,brightness and palette-copy checks.
PASS in build3ds/validation/fader-scene-native; Azahar terminated afterwards.
This still verifies register/palette behavior, not final frame compositing.

The DS dBgPi table at02099368 aliases the native GCC vtable address point
_ZTV5dBgPi+8. Compiled-object inspection confirms D1 then D0 slots as in ROM;
no hard-coded DS code address is copied. Evidence:
build3ds/player-collision-vtable-validation.txt. Native collision behavior in
Player has not run. Fresh Player closure has37 unresolved symbols; reset/IPC
and other platform adaptations remain outstanding.


### Input records, shared VRAM state and Player destructor selection

platform_bss.py preserves the36-byte current input record, four36-byte player
records and32-byte VRAM-bank/saved-bank state with31 exact aliases. Six scalar
session/input slots use their configured four-byte BSS extents. Compiled ELF
checks cover212 block bytes plus24 scalar bytes:
build3ds/platform-state-validation.txt. Native HID polling and initialization
remain required; these are storage checks, not working input validation.

The full linker log also exposed duplicate Player D0/D1/D2 and vtable definitions:
both recovered D0 and D1 files define the same C++ destructor under GCC. The
native target now retains D1 only (GCC emits all variants); MSVC selection is
unchanged. Fresh linker output confirms no multiple definitions remain.
Player still has25 unresolved symbols and no executable was run. Missing symbol
count alone never established that other linker errors were absent.


### Embedded shadow models and session/actor state

The apparent ARM9 data020ad560 reference belongs to overlay1's cylinder shadow
BMD (overlay0 shares the address range but contains different data). shadow_data.py
imports both built-in cuboid/cylinder models, their material/bone/display-list
records and numeric geometry:880 ROM bytes,22 native data relocations and8 BSS
bytes. Main-image names alias the actual overlay1 header labels. The extractor
rejects pointers outside the reviewed spans/BSS; compiled-byte,relocation and
alias checks pass in build3ds/shadow-data-validation.txt. Rendering and native
model setup remain untested; this is not proof that shadows now appear.

Added840-byte global session state and three4-byte Luigi/door fields with
configured zero-initialized extents. Evidence:
build3ds/session-actor-state-validation.txt. Their boot/actor initializers must
still supply nonzero runtime state. Fresh Player link has20 unresolved symbols;
no Player executable or new emulator run was produced at this checkpoint.


### Native key resource table bindings

key_resources.py imports84 bytes of overlay89 resource tables and replaces15
ROM addresses with native symbols, including shared references into overlays2
and85. Eight key resource records and the shared overlay85 model record retain
their8-byte SharedFilePtr layout. A dedicated resource initializer calls the
explicit-argument native file-ID constructor with the original handles from
__sinit_ov089_021328d4 and the relevant overlay85 initializer entry. smoke_player
calls it after its existing initializers. It is not the complete key actor
initializer (PMF dispatch/actor profile setup remain separate).

Compiled ELF validation checks every table byte outside relocated slots, all15
targets,three table offsets and nine8-byte records:
build3ds/key-resource-validation.txt. Initialization and key loading/unloading
have compiled but not executed. Fresh Player closure has17 unresolved symbols;
no Player executable or emulator run was produced for this checkpoint.


### Packed level/camera scripts and native bytewise fixups

level_scripts.py imports11204 bytes at0208751c..0208a0e0, preserving all named
script offsets. Six ordinary selection-table addresses become native relocations.
The packed command/numeric bytes are retained verbatim; treating incidental
aligned pointer-like words as relocations would corrupt instructions. The exact
bytewise address-fixup body of __sinit_02073e6c is copied under a native name,
ending before the FaderColor initialization already handled by its C++ constructor.
smoke_player calls this initializer after other native state initialization.

Compiled ELF comparison verifies all unrelocated bytes, six pointer targets and
all symbol offsets. The generated initializer body is identical to the original
pre-fader body: build3ds/level-script-data-validation.txt. This validates data
and compiled initialization, not executed camera/script behavior. Player now
fails linking on12 platform/runtime symbols, principally timer,IPC and reset
paths. No Player executable or emulator run was produced at this checkpoint.


### Native monotonic clock in DS tick units

Replaced Player's raw timer-register02059650 implementation with native_clock.cpp.
Nitro timer control0xc1 uses the33,513,982Hz source divided by64. libctru's
svcGetSystemTick rate is268,111,856Hz; division by512 retains the game's tick
units (compile-time clock-ratio assertion). The desktop fallback uses steady_clock
and quotient/remainder conversion to avoid multiplication overflow on long uptimes.
No DS timer overflow interrupt or020a6438 accumulator is required for this clock.

smoke_clock passes in Azahar:10000 nondecreasing reads and10475 DS ticks over a
20ms sleep (about10473 expected). Evidence:build3ds/validation/native-clock.
Azahar terminated afterwards. This checks clock advancement, not game60fps.
Alarm dispatch still needs native adaptation, including the DS node's unaligned
64-bit fields at0x0c/0x1c/0x24; blindly using GCC's default layout shifts them.
Fresh Player closure has11 unresolved symbols. Full Player execution remains
unavailable; timer callbacks,IPC/reset paths and gameplay remain unverified.


### Alarm queue ABI correction

NativeAlarm explicitly packs the DS44-byte node at4-byte alignment. Static
assertions pin deadline at0x0c,prev/next at0x14/0x18,period at0x1c and base at0x24.
The copied insertion algorithm now uses this layout and owns the8-byte head/tail
storage. Rearming also reads the correct deadline and writes timer registers
through NTR_MMIO. The original cancel routine already uses correct32-bit fields
and is retained. Original decomp sources were not modified.

smoke_alarm_queue passes in Azahar: stable equal-deadline ordering, middle/head/
tail cancellation, head rearm requests and periodic scheduling above2^32.
Clock,IRQ and rearm functions in this unit test are deterministic test-only
fixtures. They do not replace the game's native clock or prove timer interrupts/
callback dispatch. Evidence:build3ds/validation/alarm-queue-native. Azahar ended
after the run. Player still fails linking with10 runtime/platform symbols;
full alarm dispatch and gameplay remain unimplemented/unverified.

### Native alarm callback dispatch checkpoint

`hal/alarm_dispatch.cpp` preserves the recovered alarm callback/dequeue/rearm
algorithm with the explicit 0x2c DS node layout. Timer registration has four
12-byte records. Native polling checks the monotonic DS-rate clock, CPSR IRQ
mask, IME, IE and timer enable before dispatch. A reentrancy guard prevents
nested polling from dispatching the same queue. The optional runtime hook polls
on the game thread after VBlank yields; no concurrent callback thread is used.
IRQ enable/disable now shares the translated IE register with runtime startup
instead of maintaining a separate stale word.

Azahar PASS: `build3ds/validation/alarm-dispatch-vblank`: callback arguments,
one-shot clearing, deadline wait, CPSR/IME/IE/timer masking, periodic rearm,
cancellation, reentrancy and callback execution across a real rt_run VBlank.
Regression PASS: `build3ds/validation/actor-after-alarm-dispatch`, two native
actors, 120 scheduler frames, 38 triangles/frame. Emulator terminated between
runs. This is cooperative frame-granularity dispatch, not arbitrary-cycle DS
timer emulation; busy waits without yielding are not serviced. Alarm subsystem
startup is initialized explicitly by the Player gate. Player still fails to link on remaining reset/IPC
and state dependencies; this checkpoint does not demonstrate full gameplay.

Follow-up PASS: `build3ds/validation/alarm-dispatch-abi` adds initialization
idempotence, scheduling rejection before initialization and a delay above 2^32.
The native func_0201aa50 adapter joins the original low/high argument words and
calls func_020599e8 using GCC's aligned uint64_t ABI. The original raw wrapper
would pass these using incompatible register positions. All preceding dispatch
checks still pass. Fresh Player closure: 7 unresolved references, all outside
this alarm path; executable still unavailable.

### First native Player execution

Player now links and executes in Azahar. `hal/system_lifecycle.cpp` maps the
original libc abort callback to native abort. DS cartridge reset and power
command 0x0e explicitly report FATAL and abort: these two operations remain
unsupported, not successful no-ops. This boundary removes the dependency on
reloading ARM9/ARM7 binaries into native process memory.

Runtime evidence found missing overlay static initializers 02100560 (common
item resources) and 02107298 (model 1078/texture sequence 1079). Both original
initializers are now executed before Player construction. Next failure was a
null ShadowModel vtable slot at InitCylinder. The Player target now uses the
original ShadowModel destructor, DoSetFile and model-component pool allocator,
with GCC's real vtable instead of the previous zero-filled table. This does not
yet prove that shadows render correctly.

`build3ds/validation/player-shadow-native` completed Player InitResources,
but logged Y=-2147483648 (no-ground sentinel). Its old PASS proved construction
only; it is NOT a valid grounded Player or gameplay result. The gate now rejects
that sentinel. Next step: initialize the castle grounds collision and scene
state before spawning, then advance real Player behavior and rendering. Reset,
power management, full game loop, progression and 60fps gameplay remain open.

### Grounded Player behavior/render checkpoint

The Player gate now registers castle grounds KCL (handle 1941) as a static
collider before spawn. Its ground probe returns Y=254*4096 at world
(-1200,254,6800), matching the established DS reference point. The ground and
sphere-correction overloads use the recovered game implementations; sphere
contact helpers 0203794c/3798c/379c0/379f4/37a6c are enrolled. SurfaceInfo scratch
020a0cec is a native dBgPc (0x14 bytes) with its constructor.

The collision vtable had still used MSVC ordering without the two-word GCC
header. Native GCC now uses header + D1/D0 + declared ground/line/sphere order;
MSVC ordering remains under its platform conditional. Other unimplemented
virtual slots still trap explicitly. Regression PASS in Azahar:
`build3ds/validation/collision-after-native-vtable` (25 rays,8 hits) and
`build3ds/validation/actor-after-collision-vtable` (120 frames,two actors).

Player gate PASS: `build3ds/validation/player-camera-render`: 120 native
behavior-wrapper/render iterations, 54,240 emitted triangles, position stable
at (-4915200,1040384,27852800) in Fix12. Actor behavior must run through
func_02043288: calling Player::Behavior directly skips the previous-position
snapshot and caused spurious swept-collision displacement. The test camera and
clipper are initialized so visibility gates operate normally. No flag bypass.
This test uses default CLPS fallback, a fixed test camera and no input. It does
not yet show the final framebuffer, real-time pacing, full scene scheduler,
interactive movement, stereoscopic gameplay, all level properties or completion.

### Native Player framebuffer and movement checkpoint

`smoke_player` now rasterizes its final Player::Render output with an orthographic
test projection to `player-native.bmp`. The actual framebuffer was inspected
(`build3ds/validation/player-native-image/player-native.png`): recognizable,
standing Mario; this is not the viewer's separately posed model. This capture
is 256x192 and has no drawn stage/background, so it is not a fullscreen/stereo
or visual ground-contact acceptance test.

Optional `PLAYER_MOVE_TEST=1` injects magnitude 4096 and angle zero into the
per-player processed input record (0209f4a0/4a6) after 60 idle iterations. It
never writes Player position or forces a state. PASS in Azahar:
`build3ds/validation/player-movement-bounds`: native Player moves from
(-4915200,1040384,27852800) to (-4915200,1040384,29880116), max horizontal step
36860 Fix12 (about9 world units), Y remains254 every iteration, 54240 triangles.
Capture of the moving pose also inspected. This bypasses physical button polling
and the game's input preprocessing, so HID/controller integration is still open.
No frame pacing or gameplay60fps claim follows from these120 iterations.

### Original input preprocessing and jump

The Player test now calls original Stage::CheckInput each tick. It seeds an
active normal-play session and one controller, submits DS held/pressed words
at020a0e58, and clears only the processed held/pressed accumulators beforehand.
No Player position/state or processed direction/magnitude is injected anymore.
The method accesses only globals, so this test uses its ABI entry with no Stage
instance; a complete Stage and physical HID sampling remain to be integrated.

ROM mapping arrays02075650/670/690, direction angles020756b0 and the three native
pointers at0209214c are present. `build3ds/input-map-validation.txt` verifies128
original bytes and all pointer bindings in the ELF.

PASS `validation/player-raw-pad-input`: DS D-pad down drives the same bounded
native movement as the previous processed-input test. PASS
`validation/player-raw-jump-input`: B held for15 iterations after60 idle
iterations triggers a real jump, peak Y2029568 (495.5 world), then lands at
Y1040384 (254 world) by iteration120, without horizontal displacement. The test
requires ascent and return to ground. A wider test projection captures the
airborne pose at iteration68. Audio, physical buttons, realtime pacing, drawn
level/fullscreen/stereo and full gameplay are not established by these tests.

### 3DS Player input adapter

`platform/3ds/player_input.cpp` centralizes physical HID sampling and replay.
`poll()` uses libctru hidScanInput/hidKeysHeld; `submit()` keeps DS-compatible
bits0..11, maps Circle Pad digital bits28..31 to D-pad bits4..7, calculates
newly pressed edges, clears processed button accumulators, and invokes original
Stage::CheckInput. Extra3DS-only bits are excluded. Circle Pad currently uses
digital directions; analog magnitude and touchscreen sampling remain pending.

Azahar PASS: `validation/player-circle-input-adapter` replays libctru Circle
Pad-down bits through this shared adapter, with held/pressed assertions each
frame, then verifies native movement and ground contact. PASS:
`validation/player-button-edges` verifies B edge on frame60, held until74,
release75, native jump and landing. PASS: `validation/player-hid-poll` runs
actual libctru HID polling paced by VBlank for120 iterations with neutral input.
This last result does not prove a physical user keypress or realtime gameplay
performance; the test still only captures its final image. Full scene display
and interactive acceptance remain open.

### Native Player GPU presentation

`PLAYER_GPU=1` now submits the real Player's ntr geometry to the existing
PICA200 renderer via `player_present.cpp`. Texture batches preserve DS texture
state and vertex color/UVs; capacity overflow fails explicitly. The shared
renderer has a renderer-only initialization path so the smoke entry retains
ownership of gfx/log/environment setup and shutdown. The separate viewer still
builds successfully. Projection uses the400x240 aspect ratio without stretching.

Azahar PASS `validation/player-gpu-stereo`:120 native Player behavior/render
iterations presented on GPU; measured renderer FPS59.85/59.83. Direct left and
right framebuffer captures are400x240, visually inspected with textured Mario;
image difference bounds(169,108,230,180) confirm stereo disparity at forced3D=1.
PASS `validation/player-gpu-jump`: raw B input, ascent/landing and GPU presentation
coexist (FPS59.85/59.10; intermediate CPU capture also occurs in this run).

These are character-only orthographic test scenes. They establish native Player
GPU output and stereo separation, not full level performance, perspective stereo
correctness, perceived depth on hardware, original-speed60fps simulation, or full
gameplay completion. Drawing the level and integrating the full scene loop remain
next steps. HID can be polled with PLAYER_LIVE_INPUT=1; actual user interaction
has not yet been exercised in this displayed scene.

### Castle model integrated with native Player (fixed camera checkpoint)

`PLAYER_STAGE=1` loads castle grounds BMD1943 beside KCL1941 and renders it with
the original Stage scale125 through Model::Render. A shared fixed perspective
camera and Player GPU batches produce a400x240 stereo scene. Native Player
behavior still completes120 iterations on Y254. Latest artifacts:
`validation/player-castle-culling` (218640 total submitted triangles,29.91/29.92fps).

CPU/GPU comparison exposed missing GPU back-face culling; renderer-only Player
initialization now selects GPU_CULL_BACK_CCW. The terrain no longer completely
occludes the castle. Near/far homogeneous triangle clipping in player_present
uses bounded stack arrays (no per-face allocation). An earlier six-plane heap
implementation was removed after it cost~6.65fps without fixing the scene.
Diagnostic PLAYER_DEPTH_PROBE submits
red near/green far triangles; captured center pixel is red(255,0,0), establishing
correct depth ordering for that probe.

The matrix diagnostic confirms body translation equals Player world position /8,
stage transform is identity, and Stage scale is125. The apparent floating above
the castle was caused by the test camera pitch sign: inverse view rotation put
the eye BELOW the target. The corrected row-vector rotation places the eye above
the courtyard, with an assertion on reconstructed eye Y to prevent regression.
`validation/player-camera-above-ground` CPU and GPU captures were inspected:
Mario now appears on the courtyard at world(-1200,254,6800), matching the DS
reference collision height. This is a fixed-camera visual checkpoint, not an
exact pixel comparison with the retail DS camera or validation on slopes.

Performance remains~30fps with the whole static model reprocessed every frame;
static geometry caching/other profiling remain open. No skybox, stage objects,
full Stage lifecycle or complete gameplay is established here. Numeric PASS
markers cover the Player assertions only, not complete scene appearance.

The camera guard also passes with native jump/landing in
`validation/player-stage-jump-grounded`: peak Y2029568, landing Y1040384
(Fix12; ground254). Link closure remains zero unresolved.

`validation/player-stage-render-timings` measures mean emulated-clock costs
over120 frames: Stage::Render11.953ms, Player::Render3.551ms, presentation
19.350ms (including GPU synchronization and final capture). Stage display-list
decode alone consumes most of a16.7ms frame budget; cache static geometry before
claiming full-scene60fps. Presentation time is not pure GPU execution time.

### Static castle cache and clipping fast path

The Player test now retains transformed castle geometry and owns its decoded
texels. It rebuilds on view/projection changes; PLAYER_STAGE_UNCACHED=1 supplies
the baseline, PLAYER_CAMERA_TEST=1 changes camera translation at frame60 and
asserts exactly two builds. This cache is scoped to immutable stage/materials
and fixed lights in this test, not a general cache for animated level objects.
The new gx_append_polygons only replays geometry, not GX command-stream state.

player_present bypasses polygon clipping for triangles wholly inside the two
depth planes and rejects triangles wholly outside either plane. Straddling
triangles retain the existing interpolation path. These changes together give
59.72fps before captures in validation/player-stage-clip-fastpath and
player-stage-cache-camera, versus29.91 uncached. Final reported smoothed rates
56–57 include framebuffer captures; this is not an all-game60fps guarantee.
Render means: cached stage1.641ms, Player5.606ms, presentation11.310ms including
synchronization/capture. Cache alone still ran at30fps; the clipping fast path
was also required.

Pixel-exact CPU and left/right GPU comparison against uncached output passes
for idle, jump/landing (including the airborne CPU frame), and changed camera.
Reports: validation/player-stage-cached-jump/comparison.json and
validation/player-stage-cache-camera/comparison.json. Reusable comparison:
`.venv/bin/python build3ds/compare_captures.py REFERENCE CANDIDATE --report FILE`
(run from workspace root). Zero linker unresolved symbols remains verified.

Continuous camera motion still invalidates this transformed-geometry cache;
world-space caching or another path is needed for sustained60fps while moving
the camera. Original-speed simulation at60Hz, full Stage startup, actor objects,
progression and audio remain incomplete. These captures prove optimization
fidelity within the port, not retail DS equivalence for complete gameplay.

### Level-loading prerequisites: save layout and native castle records

The save-state allocation at data_0209caa0 was only0x14 bytes with its star
bitfields allocated separately. Original func_02013e64 clears0x32c bytes, and
entrance/Player code reads character selection at+0x41 and other later fields.
It now occupies one0x32c block with aliases cab4(+14),cad2(+32),cae4(+44),
caf4(+54), all hex. This fixes out-of-bounds/alias behavior, not save-file I/O.

castle_level_data.py imports4044 bytes of overlay9 data/rodata and relocates32
reviewed internal data pointers; executable code and actor vtables are excluded.
ELF bytes were compared against the local ROM with all32 pointer replacements.
The Player gate obtains BMD1943/KCL1941 from the relocated level record and uses
its real22-entry CLPS surface table instead of the old all-zero fallback.
This makes surface properties available to original collision/Player code.

PASS validation/player-native-castle-surfaces: native save aliases, level IDs,
CLPS signature,120 Player iterations, jump peak2029568 and landing1040384,
59.72fps before captures. CPU and stereo images remain pixel-identical to the
prior jump gate at this flat spawn (comparison.json). This does not validate
all22 surface behaviors. The imported record also contains the castle object,
path, entrance and texture-animation tables for the next loader integration;
validation/castle-level-inventory.json inventories those15 category records.
Full Stage::InitResources and object spawning are still not wired into this gate.

### Original level object loader and native Camera construction

LevelObjects.cpp and its15 original category handlers are now linked. The
level_loader_data.py generator imports860 bytes including actor-ID mapping,
door mappings and camera mode tables, resolving15 native function pointers and
2 native data pointers. All bytes/pointers were checked against ELF/local ROM
(validation/level-loader-data-verification.txt). Pointer/count BSS dependencies
are allocated with their four-byte ARM9 extents. Entrance fader calls now use
the shared FaderWipe class instead of the original TU-local shadow hierarchy;
real FaderWipe constructors/destructor and MMIO-translated AdvanceFade are linked.

PLAYER_OBJECT_TEST=1 is an explicitly incomplete loader probe: it provides a
temporary scene-graph parent fixture, an entrance fader array and player/mode
state, calls original LoadObjects on global/area tables, and aborts on a missing
actor profile. It NEVER reports success for a partial level. Full Stage ownership,
fader model resources and lifecycle remain required. The original first probe
had a null scene parent, causing dBase_c::Spawn to return null for every actor;
the current fixture removes that silent no-op and reaches native factories.

The loader now creates Player191 from the real entrance and Camera332. Camera
uses a native factory with GCC vtable address point(+2), its real destructor,
InitResources/Behavior/Render/Cleanup/OnPendingDestroy, View render, and translated
G3i perspective/look-at MMIO. data_0209fc5c[0]=1 retains the entrance Player as
target. Fresh validation/player-original-object-loader-camera-state shows
Camera alive1, nonnull target/state, pos(-4915200,2662400,30720000),
lookAt(-4915200,1700773,27565773). The next explicit failure is actor353(0x161),
DOOR, profile ov100:02148164, factory src/d_a_door.c at0214589c, daDoor_c size148hex.
This is progress through initialization, not a completed scene or continuous
native camera gameplay. Door is not skipped. The default scene still uses its
fixed test camera; wiring native camera behavior/render into the frame loop is
also pending.

Regression validation/player-loader-regression passes120 frames, native jump /
landing and59.72fps before captures. CPU/stereo captures are pixel-identical to
player-native-castle-surfaces. Link closure is zero unresolved. The loader probe
correctly remains FAIL until all required actor profiles/lifecycle are integrated.

### Native doors and exit triggers initialized

Actor353 DOOR now uses original src/actors/daDoor_c.cpp and its original
__sinit_ov100_02147698 initializer. Its0x1c profile fields are taken from local
ov100:02148164. door_data.py imports19 variant records plus character mapping
(308 bytes),32 native resource pointers,15 GCC ARM PMFs and the contiguous
0x230-byte door BSS block. State callbacks preserve native member-call ABI;
no DS executable addresses are used. ELF checks are recorded in
validation/door-data-verification.txt. The shared origin vector data_020a0ebc
has its correct12-byte extent (not a single word).

Actor349 CH_SCENE/EXIT uses original src/game/actors/d_a_ch_scene.cpp and the
ov002:02108690 profile. Camera::LookAtExit, collected-star checks, Animation's
frame count, Camera::GoBehindPlayer and required save/sound helpers are linked.
StartExitFaderWipe now shares the native FaderWipe ABI adapter with entrance
wipes, replacing the previously linked TU-local shadow hierarchy.

Fresh validation/player-original-object-loader-door-exit-state reaches nine
actors with aliveState1: Player191, Camera332, three Door353(variants16,12,12),
and four Exit349 triggers. The probe then explicitly fails at MC_FLAG342
(0x156), ov009 profile02113b7c, factory daMcFlag_c_classInit, current source
src/game/actors/d_a_mc_flag.cpp. No actor is skipped. This demonstrates original
resource initialization, not door animation, level transition, cleanup, save
persistence or a full Stage lifecycle.

The probe now sets current level1 / next level-1. Its empty save leaves the
intro-completed flag clear. PLAYER_ACTIVE_SESSION=1 enables that flag before
Player initialization and exposes an independent unfinished startup dependency:
voice loading reaches uninitialized sound heap state (func_020510a4); observed
TIMEOUT/unmapped accesses in validation/player-original-object-loader-exits.
Audio heap/voice startup must be completed for resumed games; this is not hidden
by a no-op sound implementation. The default Player test still enables normal
input only after construction, so it does not cover resumed-session startup.

Regression validation/player-door-exit-regression passes native jump/landing,
~59.72fps before captures and pixel-exact CPU/stereo comparison with the prior
Player scene. The final origin-vector extent correction is additionally covered
by the fresh nine-actor initialization probe and ELF size check. Full game
completion remains unproven.

### Castle flags, tree lists and birds initialized

MC_FLAG342 now links original d_a_mc_flag.cpp and its ov009 initializer;
the four flag actors reach aliveState1. TREE286 links original d_a_tree.cpp,
its Model::LoadAndSetFile path and dCcPos_c collision class. The original
five-variant resource table is imported from ov002:0210abb8 (12 bytes).
The loader produces 21 nodes in variant4, matching the castle object table.
Only the first tree actor returns initialization success; the other20 return0
after adding their nodes, as in the original aggregation code. The probe does
not yet exercise their subsequent scene lifecycle or collision updates.

SBIRD343 links its original factory, destructor, resource initializer and state
functions. Four two-word state records use native GCC ARM member pointers with
zero adjustment, verified against the original local ov009 records. One
Metrowerks scheduling-only asm hatch is translated to equivalent C stores and
the original sound call in unmatched/bird_takeoff.c; original src is unchanged.
The tree resource bytes and bird PMFs are checked in
validation/tree-bird-data-verification.txt.

Fresh validation/player-original-object-loader-bird initializes both birds
(params0x73,0x75), four flags and21 shared tree nodes, then explicitly aborts at
SET_SE350(0x15e), the next missing profile. No objects are skipped. This remains
an initialization probe: flag/tree/bird rendering, flight, collisions, cleanup
and full Stage gameplay are not validated by this run.

Regression validation/player-tree-bird-regression passes120 iterations with
native jump/landing and59.72fps before captures. All four CPU/stereo captures
are pixel-identical to player-door-exit-regression; comparison is recorded in
validation/player-tree-bird-capture-comparison.json. Link closure has zero
unresolved symbols. Azahar was terminated after each run. These measurements
still cover the fixed-camera Player scene, not a fully populated playable level.

### Ambient emitters, butterfly and cannon initialization

SET_SE350 uses original d_a_set_se.cpp, its ROM profile and the11-entry sound
table imported from ov002:0210b498. All five castle emitters initialize. This
does not validate audible output: sound heap/voice startup and mixing remain
unfinished, and initial-save ambience is disabled by the original game logic.

BUTTERFLY336 links original actors/daBtfly_c.cpp and its initializer, eight
native member-pointer states, continuous collision updates and player-distance
helpers. unmatched/butterfly_data.cpp registers the shared IBL still model
(the resource portion of its original initializer); the IBL actor's own state
initialization is still pending. The first butterfly(param0x75) reaches alive1.

CANNON207 links original game/actors/d_a_cnn.cpp and its initializer, four
native state callbacks, camera entry/exit functions and native Bob-omb explosion
dependencies. Bob-omb's consolidated TU needs the existing hostgen local-extern
alias transform, also used by Player, to preserve local ABI declarations under
GCC; both hostgen tests pass. No original src edits were required. The cannon
preloads the water-bomb model via the resource portion of its original
initializer; the water-bomb actor itself and its state table are still pending.

validation/player-original-object-loader-cannon shows five SET_SE actors,
one butterfly and the cannon(param2) alive1, alongside prior castle actors.
It explicitly fails at MC_METALNET339(0x153), the next missing profile; it does
not skip that actor or report a playable level. The second butterfly has not
yet been reached in the object table. Cannon firing/player entry, butterfly
behavior/rendering, dynamic spawned actors and scene teardown are unverified.

validation/ambient-butterfly-cannon-data-verification.txt checks the11 sound
IDs and all12 butterfly/cannon native PMFs against local overlay symbols and
records, including zero this adjustment. The native link has zero unresolved
symbols. Full game, audio and continuous Stage lifecycle remain incomplete.

validation/player-cannon-regression passes120 native Player iterations with
jump/landing,59.72fps before captures and all four captures pixel-identical to
player-tree-bird-regression. See player-cannon-capture-comparison.json. This
regression uses the fixed-camera test scene; it does not exercise the new actors'
per-frame behavior. Emulator processes were absent after the runs.

### Castle nets, signs and water initialization

MC_METALNET339 now links original d_a_obj_mc_metalnet.cpp and resource
initializer __sinit_ov009_02112a5c, with model1681/KCL1682 handles and their
destructor registration nodes. All three nets initialize at alive1.

TATEFUDA184 uses the original actor methods with a generated native adaptation,
original resource initializer, ten native state PMFs and original32-byte CLPS.
The raw factory is copied under unmatched/signpost_factory.c with its GCC
vtable address point corrected by two words. The recovered OnAttacked1 method
falls off an int function; tools/signpost_native.py preserves the observed
ARM r0 behavior instead of invoking C++ undefined behavior. Native objdump
shows the same actor-ID gate and virtual Kill slot31 dispatch, retaining this
in r0 on the no-op branch. Original decompilation sources are unchanged.
tools/actor_pmfs.py now imports nonvirtual, zero-adjustment state pointers from
local overlay records and resolves each to a native function symbol.

MC_WATER338 links original water actor/initializer and TextureTransformer
prepare/update/set-file methods. The castle generator preserves56 BSS aliases
in the0x220-byte water resource/spawn-vector block. WATERFALL197 uses its
original factory and profile; the water actor spawns seven mist actors during
initialization. validation/signpost-water-data-verification.txt and checks10 native PMFs, CLPS bytes and BSS offsets.

validation/player-original-object-loader-water reaches all three nets, four
signs, the water plane and all seven mist emitters at alive1, then explicitly
fails at HATENA_BLOCK20(0x14), the next missing profile. The fifth sign has not
yet been reached. No missing actors are skipped. This does not validate sign
dialogues, water animation, swimming, mist rendering, interactive collisions,
cleanup or full Stage ownership. At this checkpoint the moving-background
virtual collision methods still used HAL traps; the next checkpoint replaces
that specific set of traps.

### Moving-background collision methods and lifetime

Native dBgW_KcMbg now supplies all its virtual query/transform/normal/velocity
methods and destructor. The inherited dBgW_Kc and dBgW destructors, plus
CLPS_BlockRef destruction, are native too. These replace the weak HAL traps
in the Player target. The original __sinit_0207501c initializes the shared
0x78-byte line query; data_020a0d1c and data_020a0d60 alias its result and endpoint
at offsets0x10 and0x54. The12-byte destructor registration node precedes it.

PLAYER_MOVING_MESH_TEST=1 exercises real castle KCL through the moving mesh's
ground/line/sphere virtual calls, first at identity then translated100 world
units vertically with the query translated alongside it. It compares against
the static line/sphere results, and destroys both moving colliders. Fresh
validation/player-moving-mesh-reset-query passes both translations and complete
destruction. Earlier runs exposed and led to replacement of the inherited
destructor traps; a stale-hit failure was in the direct-query test fixture,
which now uses the original query reset normally called by public DetectClsn.

The line solver returns1040768 at the reference floor while the vertical
ground solver returns1040384: a difference of384 fixed-point ticks(0.09375
world units). The transform test preserves the line solver's result exactly;
it does not establish whether that pre-existing solver difference matches the
DS. Comparison of line queries with DS execution remains required. Rotation,
scale, moving platforms carrying Player, wall push-out and arbitrary meshes
are not established by this translation test. Native link closure is zero.

Before the moving-collision additions, validation/player-water-regression
passed jump/landing at59.72fps before captures, with all four captures exactly
equal to player-cannon-regression (player-water-capture-comparison.json).
After those additions, player-moving-collision-regression also passes120
iterations, jump/landing and59.72fps before captures, with all four captures
pixel-identical to player-water-regression. Emulator processes are absent.


### Native castle question block checkpoint

HATENA_BLOCK (20), with its original profile values, native factory and original
initializer, now initializes alive=1 with castle parameter ff05. The object
loader run `validation/player-original-object-loader-hatena` deliberately fails
at the next missing profile ONEUPKINOKO (276); it is not a full-scene pass.
Original TrackStar, secret-coin collection helper, Bob-omb animation resources
and the zero Vector3s initializer are linked. `actor_pmfs.py` now accepts S8
constants and generates checked BSS aliases; ov102 state tables use native
member-function pointers. The block CLPS is imported from the local ROM.
`hatena_native.py` eliminates three missing-return paths in a generated copy
of the original actor, forwarding ARM-style return registers on its callbacks.
Original src files are unchanged by this checkpoint. Vector3s's empty ROM
destructor has an explicit native symbol because GCC otherwise inlines it.

Link closure is zero. `validation/player-hatena-regression` passes 120 native
Player iterations, peakY2029568, landingY1040384 and 59.72fps before capture.
All four captures are pixel-identical to player-moving-collision-regression,
as recorded in player-hatena-capture-comparison.json. This fixed-camera test
does not render the newly loaded block or exercise block attacks/prizes.
Full Stage scheduling, actor interactions and complete game remain pending.
Next actor: da1up_c.cpp, original initializer __sinit_ov002_02100adc, profile276;
its PMF table dc00 and data tables 020ff040/020ff050 need native import review.


### Native 1-Up and enemy base checkpoint

The original ONEUPKINOKO factory/profile276 and initializer02100adc now load
all three castle 1-Ups alive=1, param=f3. Original da1up and coin helper TUs use
hostgen's local-extern ABI adaptation (87 and20 aliases respectively), leaving
src unchanged. The common resource initializer02100560 was already active.
Fourteen native state PMFs are generated by actor_pmfs.py; the existing native
rodata imports already contain both14-byte type tables. ELF verification in
validation/oneup-native-data-verification.txt confirms all PMFs, exact type
bytes and strong native dEnemyBase constructors/destructors and update methods.
Original dEnemyBase, collision update variants, GiveCoins, Event bit functions,
and hundred-coin star marker dependencies are linked. Coin counts and required
ARM9 BSS slots have their original zero initialization and extents. Full coin,
star and enemy behavior remains untested; link closure is zero.

validation/player-original-object-loader-oneup deliberately fails at the next
missing actor MIP187, after successful 1-Up initialization. It is not a whole
scene pass. validation/player-oneup-regression passes120 Player iterations,
jump peak2029568, landing1040384,59.72fps before screenshots. Four captures
match player-hatena-regression exactly (player-oneup-capture-comparison.json).
Next: MIP187, daMip_c.cpp, overlay85; profile flags8282, render83,
verticaloffset64000,radiusc8000,clip1000000/1000000. Its key actor is229.


### Native MIP rabbit checkpoint

Original MIP187 factory, profile and initializer0212f5ec are registered.
Hostgen adapts63 local extern declarations in daMip_c.cpp without editing src.
actor_pmfs.py now supports comma-separated PMF declarations. Overlay85 base
02129020, BSS305ac..3071c and16 native PMFs cover resources, carry vectors,
and eight init/main state pairs. Key resource305d8 now uses this strong shared
BSS; key_resources.py retains a weak8-byte fallback for isolated key targets.
Original rabbit progression helpers, Message PrepareTalk/EndTalk/DisplaySaving
(the latter through MMIO hostgen), and dActor UpdateCarry are linked.
Conversation, save I/O, carrying and rabbit behavior are not yet verified.

validation/player-original-object-loader-mip reaches C_JUGEM235. Its first four
rabbits are alive0 because empty save flags lack40000000. The diagnostic
PLAYER_RABBIT_UNLOCKED fixture sets save flags40000000/80000 and selects
Player character1/param1 immediately before rabbit initialization; this does
not reconstruct Player's model or validate character switching/audio startup.
player-original-object-loader-mip-character1 confirms params140a/1602 alive1,
params3000/3601 alive0 due to character mismatch. This exercises path lookup,
model/animation setup and initial rabbit state; no rabbit update/render loop.
The intermediate mip-unlocked run used character0 and correctly had all four
inactive. All loader probes deliberately FAIL at the next missing profile235.

mip-native-pmf-verification.txt checks16 MIP,42 block,10 signpost and14 1-Up
native PMFs plus strong shared key storage in the linked ELF. Link closure0.
player-mip-regression passes120 Player frames,59.72fps before captures,
peak2029568 and landing1040384; all four captures match player-oneup-regression
exactly. Emulator processes are absent after tests. Full Stage remains pending.
Next C_JUGEM235: source d_a_c_jugem.c (resolve actual current owner first),
factory daC_Jugem_c_classInit, ov085, render90,flags0,offset64000,radius12c000,
clip1000000/1000000. Native raw factory vtable address point must be checked.


### Native Lakitu and large block checkpoint

C_JUGEM235 now uses original game/actors/d_a_c_jugem.cpp, initializer0212fa40,
22 native PMFs and BSSov85:3073c..30858. Original Camera SetPos/SetLookAt are
linked. player-original-object-loader-jugem confirms alive1,param1 and actual
native camera setup pos(-5144576,1622016,27824128),look(-7852032,3076096,27484160).
This is initialization, not continuous camera control. It also reaches the
special rabbit f708 alive1 and fifth signpost before stopping at BLOCK_LL17.

BLOCK_LL17 uses original initializer02100d44 and a generated native actor
copy. block_native.py fixes two undefined int returns: DS disassembly at
020b3788/020b37ec confirms no-op returns this, otherwise forwards Kill slot31
return register. The original src remains unchanged. UntrackAndSpawnStar is
linked. level_loader_data.py imports the effect vector, two32-byte CLPS tables,
and9 native resource-table pointers; total strict relocation count26. The
third block variant's overlay-relative CLPS is bound to castle ov9:021135f4,
so this data import is still castle-context-specific, not all-level support.

player-original-object-loader-blockll confirms block17 alive1,paramffff and
processes all12 rabbit entries, then deliberately fails at FISH344. All actors
are preserved; no missing profile is skipped. jugem-block-data-verification.txt
verifies22 PMFs,9 native data pointers, effect vector and bothCLPS against ROM.
Link closure0. player-jugem-block-regression passes120 Player iterations,
jump peak2029568/landing1040384,59.72fps before captures; all four captures match
player-mip-regression exactly. This does not draw or update the new actors.
Emulator processes are absent after runs. Next actor FISH344, then the full
loader must finish before integrating scene behavior/render scheduling.


### Castle loader completion and initial actor execution probe

FISH344 is now registered with original game/actors/d_a_fish.cpp and initializer
02147bc0,7 native state PMFs and ov100 BSS489a4..48a54. fish_resources.cpp binds
six model/animation table entries to native SharedFilePtr handles, following
ROM relocations21473a4..21473bc. Both placed fish spawners initialize alive1.
The complete original castle object list now loads (player-original-object-loader-fish).
The current fixture contains79 dActor objects,48 alive1; Camera is a View/fBase
and is separate from dActor_c::Next. These counts include pending/inactive
actors; no lifecycle deletion has yet run. No missing profile was skipped.

PLAYER_OBJECT_STEP performs a two-frame dependency probe via original behavior
Process wrappers over snapshots of alive actors. PLAYER_OBJECT_RENDER adds
original render Process wrappers, explicit Camera render, optional castle
model and a direct CPU framebuffer BMP. This is not the game's priority-ordered
scheduler, camera behavior is not yet run, collision list scheduling/destruction
and full Stage callbacks are not integrated. Many actors legitimately return0
from gates; gates must be diagnosed with real scene initialization, not bypassed.
All probes still deliberately report FAIL because full lifecycle remains missing.

player-castle-behavior-probe completes two passes. Initial render probe mistakenly
looked for Camera in actor-only Next and produced black; corrected explicit
Camera dispatch also produced black despite2065 triangles and render result1.
Root cause found: global boot projection W scale data0209ee90+44 was zero.
Original boot func0201a054 sets100; smoke startup now performs that exact store.
player-castle-projection-boot confirms nonblack output (grass visible, sky absent)
at castle-actors-native.png/BMP. This is not a correct complete game image:
Lakitu intro camera still aims away from Mario and full scene/skybox setup is
missing. Camera matrix510,1911,3587,0,3615,-1926,-4064,240,450,3530914,-82706,276390.
Next work: original Stage/main scheduling (func020197b8 calls func02044120 and
func02019404), full Camera behavior, scene initialization/visibility and render.
Do not substitute this ad hoc two-frame probe for the final scheduler.


### Original scheduler and actor destruction checkpoint

PLAYER_NATIVE_SCHEDULER runs120 cycles of original func02044120, which walks
cleanup/init/behavior/render queues then reconciles the scene graph. Original
func02043fdc and020441cc are linked, as are existing native callback tables.
The scheduler excludes the ad hoc actor-only iteration used by older probes.
Input remains neutral and there is no presentation pacing/full Stage actor.

Initial120-frame run did not clean pending actors because the synthetic parent
node was detached from the global scene graph. The fixture now calls original
func0203b438(data020a4b6c,scene_root+14,nullptr) after initializing its SceneNode.
This exposes real cleanup paths instead of suppressing them. Rooted run found
weak trap dActor BeforeCleanupResources, then dActor destructor. Manifest now
links original BeforeCleanupResources/AfterCleanupResources and D1 (compiler
emits D0/D1/D2). No original src was edited. Native definition proof is in
validation/native-scheduler-definitions.txt; every listed symbol is strong T.

validation/player-native-scheduler-destruct runs120 native scheduler cycles.
At frame0 there are79 actors/48 alive, frame1 removes31 pending actors and
leaves48/48, stable through frame119. No HAL trap or crash. Render triangles
716 initially then695. Camera remains(-5144576,1622016,27824128), so this is
not playable and not an intro-completion result. Probe deliberately exitsFAIL
until full scene lifecycle is integrated. The original Stage actor is absent;
its Behavior handles scripts/message/pause/level transition state and its render
supplies level/skybox and other scene passes. Next replace synthetic parent with
real Stage factory/init and scheduler ownership; Stage source d_s_stage.cpp
still assigns raw vtables and requires native +2 address-point adaptation.
Whole Stage initialization dependencies and platform boundaries need auditing.
Link closure0; no emulator process remains after runner completion.


### Full Stage integration in progress — current build does NOT link

Stage integration is now intentionally part of smoke_player's link graph.
unmatched/stage_factory.cpp preserves original d_s_stage construction order,
with GCC+2 vtable address points for dBase/dScene/Stage. The Stage profile is
original id3,behavior3,render6. PLAYER_STAGE_CONSTRUCT currently calls the
original creation pipeline before the old manual ground/actor fixture; it has
NOT been executed. This pipeline may initialize resources as well as construct.
Before runtime, refactor actor profile registration out of PLAYER_OBJECT_TEST
so all native profiles/initializers are available to Stage's LoadObjects, and
configure actual scene-entry globals. Do not claim this branch is operational.

Seven original Stage virtual/destructor sources are now linked. First closure
had135 unresolved symbols. Added97 source entries from that closure:26 with
literal DS MMIO addresses go through hostgen; others compile directly. Audit
saved validation/stage-source-audit.json. All compile; current link still fails
with263 unresolved symbols (fresh build3ds/smoke_player-closure.json). This is
expanded dependency discovery, not a runtime regression result. Many remaining
entries are native data (OAM,level/menu tables,BSS) requiring real relocation and
layout handling; do not zero-fill read-only data or substitute dummy functions.
Other dependencies include skybox/fog,menus,fonts,overlay/resource loading,
VRAM mapping,sound,particles,save/UI and multiplayer branches. Original overlay
loaders need native semantics, not loading DS executable code into the process.
Native Stage cleanup includes raw virtual dispatch sites that require ABI review.

No emulator was launched after Stage changes because linking is incomplete.
Latest successful runnable validation remains player-native-scheduler-destruct
from the preceding checkpoint; an old3dsx is not evidence for current sources.
Next continue closure using mapped source owners with MMIO/assembly audit,
then relocate referenced data and inspect platform boundaries before runtime.

### Stage closure checkpoint: native memory and HUD data

Second/third audited source waves compile (stage-source-audit-2.json and
stage-source-audit-3.json). Current closure is 211 unresolved symbols after
adding 48 original UI/particle/scene helpers and three MMIO-transformed helpers
in the third wave. The current executable STILL DOES NOT LINK; no emulator
run was performed. The previous runnable 3dsx is stale.

hal/half_transfer.cpp now supplies native Copy32Bytes, SDK byte fill
func_0205a588 and synchronous word DMA fill DMASyncFillTransfer. Each translates
DS pointers before accessing the native backing memory. The host check
validation/native-memory-check.cpp exercises 512 unaligned byte fills, 64 word
fills with truncated tails, translated 32-byte copy and zero-size null pointers;
it passes. DMA runtime's generic DMAStartTransfer still ignores source-fixed
control bits: do NOT add original async fill func_0205a064 without adapting it.

New tools/oam_tables.py imports reviewed HUD descriptor ranges from ov001 and
ov002, including all currently unresolved OAM named tables. It rejects functions
and foreign references. CMake generates local ROM data only. Object verification
compared 10620 bytes to the original ROM after reversing 117 native pointer
relocations, all exact; validation/oam-tables-verification.txt records this.
This is data integrity evidence, not HUD rendering validation.

Deferred platform boundaries include LoadOverlay/UnloadOverlay, OS_SleepThread,
async DMA fill, cartridge save, power-management routines and minigame entry.
Remaining Stage BSS must preserve interior aliases with already-defined Player
and camera globals. Remaining particle callback vtables need native ABI review.
Before Stage runtime, move actor profile/resource initialization out of the
PLAYER_OBJECT_TEST-only branch and audit raw virtual cleanup dispatch.

### Stage shared storage consolidation — 122 unresolved symbols

New tools/stage_state_bss.py generates two contiguous native BSS blocks:
0209f1d8..0209f498 (704 bytes, Stage/player/star/clipper state) and
0209fc48..020a0c5c (4116 bytes, transition/text state). Removed corresponding
independent strong definitions from unmatched/player_globals.cpp. Strong generated
symbols override older weak HAL storage, preserving all original interior offsets
and STAR_MARKERS/data_0209f40c identity. No original src files changed.

The text buffer beginning0209fd5c is cleared as0xf00 bytes by original
func_02031e00; data_020a0244 is an interior alias, not a separate allocation.
This is why allocating only to the next symbol would have been incorrect.
Object-file verification proves all170 strong aliases have original offsets
across4820 zero-initialized bytes; validation/stage-bss-verification.txt.
Compilation succeeds with no duplicate definitions. Fresh link closure is122
unresolved; the current binary still does not link and has not been run.
Full scene construction, native initialization, HUD rendering and gameplay remain
unverified. Latest runnable evidence is still player-native-scheduler-destruct.

### Native particle callback closure — 106 unresolved symbols

Added12 original particle callback implementations and19 audited original
message/particle/engine dependencies (validation/stage-source-audit-4.json).
Eight recovered callback classes declare SpawnParticles overrides in dPa_c.h,
but their ROM vtables actually inherit the base implementation. Native-only
unmatched/particle_inherited_callbacks.cpp forwards each to its ROM-proven base,
allowing GCC to emit correct native vtables without touching the decomp header.
Three classes inherit simpleCallback_c::SpawnParticles at02022640; five inherit
callback_c::SpawnParticles at020226d0. Other callbacks retain original bodies.

Object relocation verification confirms all eight native vtables bind the exact
ROM OnUpdate target at GCC+12 and their GCC+8 spawn wrapper branches to the exact
ROM base target. Evidence: validation/particle-inherited-vtable-verification.txt.
All13 particle callback vtables now have real native definitions recorded in
validation/particle-native-vtables.txt. This proves wiring, not runtime effects.

Build compiles but still fails linkage with106 unresolved symbols. No emulator
run. Remaining code includes matrix assembly primitives, native overlay handling,
cartridge save/sleep/power boundaries, minimap/text helpers and audio routines;
initialized tables and residual BSS also remain. Do not interpret the transient
0-unresolved report from a closure command during compilation as successful:
its linker failed while objects were still being rebuilt. The fresh terminal
closure after completion is106. Run closure only after the build terminates.

### Native particle rotations — differential ROM execution

unmatched/particle_rotation.cpp replaces the two Thumb assembly-only 3x3
rotation-store primitives (Z at02052588, Y at0205256c). Inputs remain integer
sine/cosine; unsigned negation preserves ARM wrap at INT_MIN. The original
MulVec3Mat3x3.c is now included without changes. No floating-point substitutions.

Installed Unicorn2.1.4 in the existing workspace .venv for direct local ROM
instruction checks. build3ds/check_particle_rotation.py compares native functions
with actual Thumb bytes from extracted/arm9_dec.bin, using49 boundary pairs and
1000 seeded random pairs per routine, including buffer guard bytes. All2098
cases match exactly: validation/particle-rotation-verification.txt.
Native dylib can be rebuilt with clang++ -shared -std=c++17
port/unmatched/particle_rotation.cpp
-o build3ds/validation/particle-rotation.dylib (prefix commands with rtk).

Full target still fails linkage, fresh closure103 unresolved. No emulator run.
Next continue initialized-data relocation and platform boundaries. In particular,
func_020316d8 computes glyph destinations from DS VRAM addresses and dereferences
them directly; before adding it, ensure returned/computed destinations are native
translated pointers. Literal-MMIO auditing alone does not cover that path.

### Native glyph blitter — ROM differential check

New tools/glyph_native.py generates func_020316d8 from original src without
editing it. It translates the computed DS character-buffer destination through
ntr::xlate before direct stores and defines ARM LSR-register semantics for the
rsh==32 path (x%16==8); native C/C++ shifts by32 would be undefined. All other
blit logic remains original. CMake includes the generated glyph_native.cpp.

build3ds/check_glyph_native.py uses Unicorn to run the original ARM ROM body,
with the three BG pointer getters and MultiCopy_Int intercepted at their original
addresses. Native test shim supplies translated VRAM and equivalent copies.
1920 cases (3 selectors,5 widths,4 character tiles,32 X offsets) match full64KB
VRAM contents, including nonzero surrounding data. Getter test doubles share
one test VRAM base; this does not validate actual BG bank selection or 2D output.
Evidence: validation/glyph-native-verification.txt. Test shim/generated source/
dylib are in validation/glyph_test_shim.cpp,glyph_native.cpp,glyph-native.dylib.

Full target compiles but still cannot link: fresh closure102 unresolved.
No Azahar run and no dialogue UI validation. Continue initialized table imports,
platform boundaries and scene entry initialization before claiming runtime.

### Initialized Stage/font tables — 55 unresolved symbols

New tools/stage_tables.py imports explicitly selected initialized ARM9 data ranges
as writable native tables, preserving all interior names. Most ranges have no
relocations; eight reviewed pointers are rebound by symbol (one model/material
reference, four local data pointers and three text-control callbacks). Any new
relocation in these ranges fails generation. Raw DS function pointers are never
copied through. Added original text callbacks0203128c/02031154/020313d8,
name-to-font conversion02031028 and GetMinimapID to the player slice.

Object verification compares25420 emitted bytes with ROM after reversing all8
native relocations, exact. Evidence: validation/stage-tables-verification.txt.
The full target still does not link:55 unresolved after the completed build.
No Azahar run, and old3dsx remains stale. Remaining initialized tables include
020756f0 (12 overlay2 resource pointers),02092208 (52 level roots with overlay
context), plus the two character translation tables02075b14/02075d14 newly
referenced by02031028. Remaining BSS/runtime/SDK boundaries are listed in fresh
build3ds/smoke_player-closure.json. Preserve native profile initialization and
full Stage lifecycle requirements before attempting runtime.

### Video fill completion and common resource table — 52 unresolved

hal/half_transfer.cpp now implements func_0205a064: fill destination (r1) with
value(r2), then invoke completion once, including size0. Native completion is
synchronous, consistent with the existing native DMA copy seam; actual hardware
DMA timing is not reproduced. DMASyncFillTransfer now sends DS I/O destination
writes through ntr::io_write so graphics register side effects are not bypassed.
VRAM/OAM still use translated buffers. Existing memory tests plus new I/O-write
ordering/zero-size completion tests pass (native-memory-verification.txt).
Generic DMAStartTransfer still needs source-fixed control support if more
original SDK DMA routines are later imported; this dedicated fill path avoids it.

stage_tables.py additionally imports the two font translation tables and the
12 common resource pointers at020756f0, validated against overlay2 BSS symbols
and explicit original relocation targets. Updated object verification proves
26492 bytes exact after reversing20 native pointers. Original name-slot lookup
func_0203d8a0 is linked; its two22-byte-entry BSS arrays remain unresolved.
Build compiles, link still fails with52 references. No emulator run. Full Stage
initialization, runtime/gameplay, rendering, audio and performance remain open.

### Residual BSS and brightness-fader initialization — 28 unresolved

stage_state_bss.py now includes reviewed small scene/session globals, overlay1
HUD state, both four-entry name tables (88 bytes each,22-byte records), and the
16 audio heap entries (68 bytes each). Two-byte name-field labels remain aliases
inside their owning arrays; original0203db64 clears each entire88-byte table.
Object verification now covers199 strong aliases in6172 bytes, exact ROM offsets:
validation/stage-bss-verification.txt. No duplicate definitions in the full build.

unmatched/fader_globals.cpp now constructs data_0209f5d0 as a real native
FaderBrightness, with0x1000 currInterp and0 speed matching __sinit_02074edc;
compile-time size is12 bytes. data_0209f61c remains intentionally unresolved:
it is a different44-byte dWipe_c with real virtual methods and capture behavior,
not an uninitialized buffer or a FaderWipe substitute.

Fresh completed build compiles but link still fails with28 unresolved symbols.
No emulator run. Main remaining integration fronts: dWipe_c,52 level-root pointer
entries/native overlay registry, card-save and OS/SDK power/sound boundaries,
minigame entry and missing gameplay helper020bd828. A low unresolved count is
not evidence of a full working game; weak HAL traps and runtime initialization
must still be audited after link success.

### dWipe_c native lifetime and original virtual methods

unmatched/screen_wipe_global.cpp supplies the real44-byte dWipe_c global at
symbol data_0209f61c, with GCC construction/vtable, zero interpolation/speed/
color and original func_0202ed14 initialization (matching0202fc40's field state).
Added all original virtual methods except duplicateD0; GCC emits D0/D1/D2 from
D1. Three methods use MMIO hostgen. Original six scanline setup/shape helpers
are included, three via hostgen. Added the original wipe palette words and BSS
flags/double buffer (0209f648..fc48,1536 bytes) to existing generators.

Object verification proves all10 native dWipe_c virtual slots match the exact
ROM method symbols at GCC vtable+8. Evidence: screen-wipe-vtable-verification.txt.
This is ABI wiring evidence only: native runtime currently handles GX FIFO IRQ,
not HBlank/scanline presentation. The original HBlank handler func0202f2c4 still
references data_023c0000+0x3ff8; this is shared DS IRQ memory, so adapt access to
ntr::host_ptr(0x023c3ff8) rather than inventing unrelated storage or a raw DS
pointer in the native process. Native scanline/window rendering remains work.

Completed build compiles but still fails linkage with28 symbols (the new shared
IRQ boundary replaces resolved dWipe global). No Azahar run. All full-game,
scene-initialization, 60fps and complete stereo validation requirements remain.
Earlier stage BSS/table verification byte totals predate the new wipe buffers/
palette words and should not be cited as covering those additions.

### Shared IRQ-memory adaptation and original audio helpers

hostgen now translates the specific hardware symbol address &data_023c0000 to
ntr::host_ptr(0x023c0000u). The HBlank handler thus updates base+0x3ff8 in the same
translated DS RAM used by other hardware users. Its two MMIO reads remain
proxy-dispatched. Generated-source assertion and compiled-object nm confirm no
unresolved data_023c0000 and a native region reference; evidence in
validation/hblank-shared-memory-verification.txt. This does not implement
HBlank delivery or per-scanline rendering.

Added11 original audio transition/interpolation/command helpers, all compile.
They expose further original dependencies; fresh closure count is recorded in
build3ds/smoke_player-closure.json. Native audio is still unfinished. In particular
func0205b608 reads data_020a7fc0[1], but current HAL defines that ARM7 shared-status
pointer without an audio backend: blindly adding its original body would crash
on null at runtime. Establish the real native sound status/command seam before
executing this path; do not use fake success/status values to hide it.
The full target still fails linkage, no emulator run, and full game requirements
remain unverified.

### Audio transition source closure and storage

Added9 original channel stop/dispatch helpers from the current closure; all
compile. Added the80-byte stream state, four0x12c-byte sound players and eight
12-byte channel records to stage_state_bss.py. These are storage only; original
initialization and the real ARM7-equivalent sound backend remain required.
Updated object verification covers209 aliases across9108 bytes, including the
previously added wipe buffers: validation/stage-bss-verification.txt.

Fresh closure remains27 unresolved because imported functions expose further
original audio helpers (0204f0b8/0204f11c/0204f15c/0204fce8/0204ff9c/0205212c,
message queue020587e4, sound commands and volume conversion), plus two BSS
objects020a5578/020a5600. These are genuine dependencies, not runtime-tested yet.
SaveDataToCart original uses a rolling16-bit checksum,8-byte magic and primary/
mirror writes; preserve that contract when implementing native persistence.
func020618b8 is wireless status, distinct from audio and filesystem seams.
No emulator run: build still fails linkage. Full-game objective stays open.

### Audio cleanup and volume data closure

Added8 original sound cleanup, channel bookkeeping and volume conversion sources.
They compile. stage_tables now imports the724-byte volume lookup at02086484;
object bytes match the ROM exactly. stage_state_bss adds252 bytes at020a5538
(scratch pointers/channel descriptors/message queue) and12-byte list head at
020a5bc8. Evidence: audio-volume-storage-verification.txt. These allocations
still require original initialization; zero BSS is not a running sound backend.

Original sound message receive020587e4 can block via OS_SleepThread and wakes
senders through OS_WakeupThread. Native OS scheduling must retain that behavior;
do not fake a message arrival or return success without one. Further pure cleanup
helpers0204fde8/020520dc/020520a4 and command0205aa9c remain to import, alongside
the previously documented platform boundaries. Build is still link-incomplete,
no emulator run; authoritative current references are in smoke_player-closure.json.


### Pause bridge, audio lists and message reception

The address-named pause call func020bd828 now forwards to the original
Player::CanPause method (ov002 symbol metadata confirms the identity). Strong
bridge object and reference verified in player-pause-bridge-verification.txt;
this is linkage evidence, not a pause-menu runtime test.

Added original audio helpers0204fde8,020520dc,020520a4,0205aa9c,0204fda4 and
02050008 plus original message receive020587e4. All compile. The receiver retains
nonblocking-empty failure, ring advancement, sender wakeup and blocking sleep;
OS_SleepThread and OS_WakeupThread now both require native scheduling support.
Neither has been replaced with fake success.

Expanded audio BSS to include active stream list020a552c, free list020a5bbc and
three32-bit allocation masks020a4d48/4c/50. Reproducible object verification:
`../.venv/bin/python ../build3ds/check_stage_bss.py` from repository root.
PASS221 strong aliases,9408 bytes across21 nonoverlapping sections, including
all previous wipe/sound storage. Evidence stage-bss-verification.txt supersedes
older BSS counts. This checks storage layout, not initialization or playback.

Latest completed build still fails linking; fresh closure has16 unresolved:
overlay load/unload and52-level-root table, three absolute overlay identifiers,
SaveDataToCart, thread sleep/wake, ARM7 sound status, four power-management calls,
wireless status and minigame font-buffer accessor. Full Stage has not executed;
no emulator was launched and no stale3dsx was used. Full game/60fps/stereo goal
remains active. Need real native platform services and scene/profile startup.


### Level overlay identity audit

stage_tables.py now emits strong absolute overlay_60/98/102 symbols, rather than
allocating integers (callers pass their addresses as IDs). The generator checks
both original ROM literal values at0202e07c..084 and the three preceding LDR
instructions. Compiled ELF symbols are SHN_ABS with values60/98/102: evidence
validation/overlay-ids-verification.txt. This fixes identifiers only, not the
LoadOverlay/UnloadOverlay lifecycle.

build3ds/audit_level_roots.py resolves all52 Stage roots with the overlay selected
by data020758c8 (IDs8..59), validating root pointer fields against that specific
overlay's relocation metadata. The reproducible catalog is
validation/level-roots.json; it includes root symbol, model/KCL file IDs and area
counts. Address-only lookup is unsafe because many level overlays use the same
DS addresses. No native52-root table is emitted yet: every root and its complete
object/CLPS/texture-animation data must be relocated to native storage, and actor
profiles initialized, before Stage can use it. Do not substitute castle data for
other entries or execute original overlay machine code.

Latest build compiles the generated constants but still fails linkage; fresh
closure13 unresolved. No emulator launch or gameplay/60fps/stereo evidence in
this checkpoint. Full objective remains active.


### Native data for all52 level roots

all_level_data.py now emits data02092208 and private per-overlay numeric data.
It selects contiguous data islands reachable from each level root, preserving
interior offsets; excludes function ranges and islands containing function/
external relocations, and rejects any selected edge into unclassified storage.
Castle entry reuses data_ov009_02112bdc from the existing castle importer, so its
shared identity is preserved. Other levels have distinct native addresses even
when their original DS addresses overlap. CMake tracks all52 overlay metadata/
ROM inputs and builds the generated source. Original src/ is unchanged.

Object verification `../.venv/bin/python ../build3ds/check_all_level_data.py`
passes:136404 bytes across72 islands,1349 internal relocations reconstructed to
ROM addresses, and52 root relocations mapped to their actual overlays. CLPS
headers/counts, area arrays, object category arrays, and directly spawned
standard/entrance/simple/teleport/door/exit payload extents fit the imported
regions. Deferred-consumer category payload extents and BTA runtime mutation
are not yet independently verified. Evidence: all-level-data-verification.txt.

This integrates data, not all level actors or lifecycle. Before claiming working
transitions, LoadOverlay/UnloadOverlay need native profile/init/cleanup dispatch
and reset handling for any mutable imported tables (original DS overlay reloads
restore ROM bytes). Cross-overlay actor CLPS references in level_loader_data.py
also still need proper current-level resolution instead of the castle binding.

Latest full build compiles this import and fails linkage with12 remaining
platform boundaries; smoke_player-closure.json is refreshed. No emulator launch,
no gameplay completion or60fps/stereo claim. Goal remains active.


### Native cart persistence seam

hal/save_cart.cpp supplies SaveData::SaveDataToCart and ReadDataFromCart with
real file I/O. The default is sdmc:/sm64ds/save.sav; SM64DS_SAVE_PATH overrides it
for isolated tests. Uses8192 bytes (matches the local DS reference save), original
128-byte slot addressing, the boot tag at0208ee50, original rolling16-bit checksum
and primary/mirror halves. Supports ordinary68-byte slots and740-byte minigame
block at slot3. Returns original0/1 write and0/1/2 read success/error/blank codes;
reads fall back to the mirror. Wireless mode2 is rejected. Tag is fixed to the
actual game boot's ds mario value; generic SDK cart initialization is not exposed.

Writes preserve other slots, stage the complete image in a temporary file, check
write/flush/fsync/close, then rename; malformed existing images are rejected and
not overwritten. Unlike the original cart's sequential writes this commits both
halves together. Host test checks literal bytes independently against the ROM
tag and checksum, four save blocks, mirror recovery, corruption, blank images,
overflow/range/mode rejection, failure preservation and malformed file rejection.
All tests use temporary files; user's reference save is read only for size.
Evidence: validation/save-cart-verification.txt. Compile host shim with clang++
-shared -std=c++17 -I sm64ds-decomp/include port/hal/save_cart.cpp and the shim
under validation; run build3ds/check_save_cart.py from workspace root.

The3DS object compiles; SD devoptab fsync/rename behavior still needs an actual
Azahar test, including replacement of an existing save. Save menu/bootstrap
loading and error UI are not yet exercised. Calls currently assume serialization
on the game thread; any native worker save requests must synchronize the whole
read-modify-write transaction. Crash durability is unproven. Full target still
fails linking with11 remaining platform references. No emulator launched and
no full-game,60fps or stereo completion claim.


### Azahar verification of SD persistence

Added dedicated smoke_save_cart target using the exact hal/save_cart.cpp from
the player target. Its isolated session-mode provider returns single-player0;
this is explicitly a filesystem probe, not scene/gameplay initialization.
It creates a timestamp-named SD file, refuses an existing path, writes740-byte
minigame-shaped data twice (exercising rename over an existing save), reopens
through SaveData, verifies both750-byte checksum/tag/payload blocks, corrupts
the primary copy, verifies mirror recovery, and deletes only its test file.
No real save is modified. Probe includes native SD fsync and replacement rename.

Build smoke_save_cart_3dsx succeeded. Run:
`python3 build3ds/run.py build3ds/cmake/smoke_save_cart.3dsx --timeout 20`
Actual run evidence:
validation/save-cart-azahar/log.txt and run.json (binary SHA retained).
PASS, both save passes returned0, readback/replacement/mirror recovery verified.
The runner killed Azahar before/after as requested. This supersedes the prior
uncertainty about fsync/rename on Azahar SD, but not physical-hardware durability,
concurrent save calls, menu save/load interaction, or complete game progression.
Full player target remains link-incomplete with11 platform boundaries from its
last build; the standalone probe must not be mistaken for a playable game.


### Stage startup profile availability

smoke_player now shares initialize_player_profile and
initialize_castle_actor_profiles between the full Stage-construction branch and
the object diagnostic. Previously the Stage branch returned before any of those
registrations/resource initializers, so a native Stage could not spawn the
already-ported castle actors. Resource initialization is guarded once per process;
this is startup preparation, not a substitute for overlay reload/cleanup.
Player-only diagnostics retain their narrower startup. Both refactored sources
compile; full Stage runtime still cannot be exercised until platform closure.

unmatched/minigame_font.cpp implements the original ov004020adc4c accessor and
its4-byte BSS pointer020beb60. nm confirms strong text/BSS symbols. It returns the
actual scene-owned pointer, initially null. It does not allocate a fake buffer:
original dScMgBase BeforeInitResources allocates0x4000 and AfterCleanupResources
frees/zeros it. Those minigame lifecycle methods still require integration before
LoadFont(2) can execute. This resolves the accessor only, not minigame support.

Latest completed player build compiles changes but fails linkage. Fresh closure
has10 unresolved: overlay load/unload, thread sleep/wake, sound status, four power
calls and wireless status. No emulator run this checkpoint. Full Stage startup
parameters/main loop, transitions, all actors/minigames and full-game60fps/stereo
remain unverified; goal stays active.


### Native backlight adapter and emulator limitation

platform/3ds/backlight.cpp supplies0205f9ac (set) and0205f8e0 (get tracked state).
Selector0 maps to top,1 bottom,2 both, matching the original PM bit3/bit2 reads
and libnds definitions:
https://github.com/devkitPro/libnds/blob/master/include/nds/system.h
Commands use libctru gsp::Lcd, propagate failed Result codes, and update state
only after service success. Query before initialization returns0xffff without
writing outputs. main3ds invokes an optional init hook after graphics/log setup
to establish known on/on state. Get reads application-owned requested state,
not physical hardware; APT sleep/resume reconciliation remains necessary.

Dedicated smoke_backlight builds and runs in Azahar. It exercises both screens
off, individual on/off, both on, invalid arguments and final restoration.
validation/backlight-azahar/log.txt reports PASS for dispatch/tracked state.
However Azahar's own log reports PowerOnBacklight/PowerOffBacklight unimplemented
while returning0. Captured in service-limitations.txt. This is NOT verification
of actual backlight or pixel changes, on emulator or physical3DS. Do not cite the
PASS as display-power validation. A renderer fallback may be needed for emulator
visual parity, and physical hardware behavior needs its own test.

The full player source compiles these two boundaries; fresh link closure has8
remaining symbols: LoadOverlay, UnloadOverlay, OS_SleepThread, OS_WakeupThread,
func0205b608 sound status, func0205f68c LCD power, func0205f77c sleep and
func020618b8 wireless status. No full Stage run. Full objective remains active.


### Native LCD power transition boundary

platform/3ds/lcd_power.cpp implements0205f68c using GSPGPU_SetLcdForceBlack and
updates bit0 of translated POWCNT1 only after service success, preserving other
bits. Return0 indicates pending settling delay or failed display command;1 means
complete. Original unknown-mode success convention remains. Native monotonic
134ms settling delay replaces the original8-DS-VBlank counter; skip_delay retains
its bypass. This is a conservative platform timing adaptation, not a simulation
clock/60fps change. The3DS OS owns its power LED; optional DS LED blink requests
are intentionally not reproduced (original LCD code ignored LED result anyway).

smoke_lcd_power_3dsx builds and runs in Azahar. PASS: off command, preserved
POWCNT1 bits, immediate-on rejection, successful on after150ms, forced-on bypass.
Evidence validation/lcd-power-azahar/log.txt and run.json. No framebuffer/pixel
capture was compared, so the probe validates control/timing/register agreement,
not visible black frames. Emulator terminated after run as requested.

Player build compiles this boundary and still fails linkage with7 remaining
symbols: native overlay load/unload, thread sleep/wakeup, ARM7 sound status,
console sleep and wireless status. Scene initialization/gameplay/full60fps/stereo
are still unverified; objective remains active.


### Cooperative game-task wait queues

hal/thread_wait.cpp supplies OS_SleepThread/OS_WakeupThread for the existing
single cooperative game task. Sleep registers bit0 in the queue, releases the
emulated IRQ mask while yielding to host frames, and returns only after an
explicit matching wakeup; caller IRQ state is restored. Frames and wrong-queue
notifications do not wake it. Wakeups with no current sleeper are not latched.
Runtime exports rt_running to reject waits outside the game execution context.
Nested/null waits or preexisting unknown task bits fail explicitly. This is not
a general DS multithread scheduler: native workers must marshal notifications
onto the serialized runtime, and interrupt/audio producers still need wiring.

smoke_thread_wait links the original func020587e4 message receiver. Azahar PASS:
9 cooperative frames, two distinct waits, early/wrong wake rejection, preserved
IRQ masks, nonblocking empty receive, blocking receive of an actual supplied
message, ring index/count update. Evidence thread-wait-azahar/log.txt + run.json.
The probe uses host hooks to supply notifications deliberately; it does not prove
that the full game currently receives VBlank/audio notifications. Runtime's
original game/host event handoff remains unchanged. Emulator killed after run.

Player build compiles and closure is now5 symbols: LoadOverlay, UnloadOverlay,
func0205b608 sound status, func0205f77c console sleep and func020618b8 wireless.
Still must run Stage under rt_run, dispatch original VBlank handler and pump
actual audio completion events before its waits can progress. No full Stage
runtime or full-game/60fps/stereo completion claim; goal remains active.


### Pending IRQ dispatch, original VBlank probe and Stage MMIO repair

ntr runtime now stores32 handler slots, delivers pending IF bits under IME/IE/
CPSR gating, masks interrupts during handlers, acknowledges delivered bits, and
rechecks enable/pending state between callbacks. Enable/restore and writes to
IME/IE trigger delivery; IF writes clear specified bits. rt_vblank_wait raises
VBlank on the resumed game context, after the host frame; the host no longer
blindly drops pending VBlank. Existing inline GX FIFO callback path is retained.
MMIO readiness prevents early global initialization from touching unmapped IO.
This is cooperative IRQ delivery, not HBlank scanline scheduling or DS threads.

smoke_vblank_irq compiles original IRQ::VBlankHandler and func0201a4e4. Its two
display callbacks are counters, deliberately isolating wakeup behavior. Azahar
PASS: CPU/IE/IME masking, retained pending interrupts, IF write-one-clear,9 frames,
4 cadence-two game wakes,9 per-VBlank callbacks, actual shared IRQ bit at023c3ff8.
Evidence vblank-irq-azahar. Previous thread/message probe passes again under new
IRQ runtime (thread-wait-vblank-regression). GX pump regression passes with492
triangles/18650 pixels and byte-identical game/harness commands.

IMPORTANT: original VBlank handler is currently linked to the dedicated probe,
not registered in full player startup yet. Its real display callbacks19144/19100
still need integration. Stage assigns data0209d4a8 to data0209f3c4; original
__sinit02074e84 installs a four-entry raw dGraph callback table02092188 there.
That initializer/table and Stage GraphCallback1/2 are not in the player slice.
Import/initialize them before enabling full scene VBlank, or the callback vptr
will be zero. This is a concrete remaining startup dependency, not proven game
rendering. The four callbacks have no destructor prefix in this raw dispatch.

Stage::InitResources previously compiled direct DS register pointers. It now
builds through hostgen. Fixed pointer declaration-initializer recognition and
rejection of pointer mutation; renamed only the generated Stage display p1 local
with strict declaration/use-count guards, preserving the earlier same-name byte
array.19 Stage MMIO rewrites compile. Six mutation/store fixtures and actual
Stage source checks pass via check_hostgen_pointers.py; stage-mmio-verification.
Original src/ remains untouched. Full target still has5 unresolved platform
symbols. Full Stage/game/60fps/stereo requirements remain unverified.

### Scene callbacks, particle rendering and OAM layout checkpoint

The preceding VBlank integration limitation is partially resolved: smoke_player
now includes original IRQ::VBlankHandler, registers it via func0201a4e4 in the
Stage-construction branch, and invokes original __sinit02074e84 for graph callback
and Clipper initialization. Original GraphCallback1/2, Particle::RenderAll and
its six render variants are compiled, with register accesses passed through
hostgen. Original OAM::Flush/Load and their transfer helpers are included.
Stage construction still needs the cooperative runtime/main loop; no full-scene
execution is claimed. The graph matrix is not initialized by __sinit02074edc
(that function initializes faders, despite an inaccurate source comment).

stage_tables imports the four-slot base/Stage dispatch arrays and both particle
function tables with ROM-checked relocations. The native GCC callback vtable is
emitted by the original key function, not duplicated from a DS ABI table.
check_stage_tables.py verifies the compiled ARM object's 27272 bytes and32
relocations against ROM. hostgen now translates used scalar REG_* macros from
nitro/hw/registers.h, which otherwise expand after transformation, and translates
G2x::SetBGyAffine's destination at function entry while retaining its fixed-point
math. check_hostgen_pointers.py covers these paths and the prior19 Stage rewrites.

OAM state now has strong contiguous BSS aliases in the player target, including
its initial zero mode flag, two1024-byte buffers and interior +8/+32 aliases.
The isolated old gates retain their weak single-buffer fixture. BSS verifier:
230 aliases,11476 bytes,22 sections. MultiCopy_Int and MultiCopy32Bytes now match
original forward word and eight-register block copy semantics, including overlap;
plain memcpy was invalid for OAM::Reset's repeated pattern expansion. Differential
Unicorn/native validation passes308 cases with complete buffer comparison
(copy-primitives-verification.txt). smoke_oam_reset checks original dual/single
buffer reset and preserved attribute padding using the strong BSS layout.
Azahar PASS: dual-buffer128/single-buffer128, offsets8/32/1024, failures0;
evidence validation/oam-reset-azahar. Emulator terminated by the run harness.

Current full-player link closure remains5 unresolved platform functions:
LoadOverlay, UnloadOverlay, func0205b608, func0205f77c, func020618b8. No new full
player binary was run after failed links. These symbols do not represent all
remaining work: native audio/overlay lifecycle, Stage startup and full progression
still require implementation and runtime validation.

### Numeric level reload state checkpoint

all_level_data now emits immutable initial snapshots with relocations targeting
live native level arrays, plus a74-range descriptor registry covering all52
levels (including the existing castle import). sm64ds_reset_level_data(overlay)
restores only the requested overlay's numeric ranges and returns the byte count;
unknown IDs return0. It does not reset actor BSS or invoke constructors and is
not yet wired to LoadOverlay. Call it only after old actors release references,
before new overlay initialization. CMake tracks the castle generator dependency.

Azahar smoke_level_reset PASS:140448 bytes across74 ranges/52 levels. The probe
first checks pristine live data equals immutable snapshots (including relocated
pointers), corrupts every byte, restores levels individually twice, and verifies
untouched levels retain their corruption until selected. Unknown IDs7/60 alter
nothing. All-level object-vs-ROM verification still passes136404 bytes/1349
internal relocations and52 root entries; the castle has its separate importer.
Evidence validation/level-reset-azahar and all-level-data-verification.txt.
This proves numeric restoration, not working level transitions or full gameplay.

Read-only ROM overlay-table inventory: validation/overlay-initializers.json
contains103 overlays,282 named initializer targets and103 terminal null entries
(excluded from constructor lists). Every nonzero target resolved in its own
module's symbols. Castle9 needs4 initializers; common actor overlays85/98/100/102
need6/4/7/4 respectively. The manual castle fixture initializes only a subset of
these common overlays; replacing it requires full lifecycle/reset/destructor
integration, not a successful no-op LoadOverlay. Full-player link still has the
same5 unresolved platform functions; game completion remains unverified.

### Native overlay destructor ownership checkpoint

The DS func0205dc7c selects registered destructors by their function/object ROM
address ranges. Native code/data no longer occupy those ranges. The player now
uses unmatched/overlay_destructors.cpp for func020731dc registration, retaining
the three-pointer node ABI and original global list. An explicit initialization
scope records ownership separately; main executable registrations use scope-1.
Eighteen existing original overlay initializer calls and the two partial native
shared-resource initializers in smoke_player now enter their owning scopes.
This does not make those partial overlay initializers complete.

sm64ds_destroy_overlay_resources detaches matching nodes under the emulated IRQ
mask, preserves unrelated nodes, invokes callbacks in original LIFO batch order
with the caller's IRQ state, then repeats for callbacks registered during cleanup.
Caller ownership scope is restored. Null callbacks are detached; destroyed nodes
can be registered on a later load. Duplicate live registrations and untracked
list nodes abort explicitly. The implementation assumes the serialized game
runtime, not concurrent native audio or other worker registration. Main resources
cannot be unloaded through this API. It does not invalidate native resident code.

Azahar smoke_overlay_destructors PASS:6 callbacks in order3,1,4,5,2,6; selective
ownership, null callback, registration during destruction, repeated empty unload,
node reuse, main-list survival and both masked/unmasked caller state. These are
controlled destructor fixtures, not yet original resource cleanup in gameplay.
Evidence validation/overlay-destructors-azahar. Full-player object compilation
passes; link still reports the same5 platform symbols. No stale player binary
was launched. Numeric reset, BSS/profile restoration and constructor dispatch
still need to be combined into the actual LoadOverlay/UnloadOverlay lifecycle.

### Castle overlay lifecycle integration checkpoint

castle_level_data now owns the complete704-byte castle BSS at original
02113c20..02113ee0, including bird, water, metalnet and tree handles/destructor
nodes. Removed seven separate native declarations from player_globals; native
bird PMF constants remain separate initialized data. The ARM object verifies59
BSS symbols at exact ROM offsets. Evidence castle-lifecycle-layout.txt also
checks the four constructor calls against the extracted overlay initializer list.

unmatched/castle_overlay.cpp combines numeric restoration (4044 bytes), full BSS
zeroing and the four original constructors in ROM order under ownership scope9.
It rejects duplicate load/inactive unload. Unload drains registered destructors;
callers must destroy active actors first. Original shared-handle destructor
func02017e34 merely publishes the file ID; it does not force-release live actor
references. Code remains resident and BSS is reset on the next load.

The manual castle startup in smoke_player now calls this lifecycle instead of
four scattered constructor calls. Other common overlays still use the partial
startup and profiles remain separately assigned by the fixture. This specialized
lifecycle is not yet the generic LoadOverlay/UnloadOverlay implementation, nor
has original resource unload/reload been exercised in the full game. Compilation
passes for the changed objects; full link remains blocked by the same5 platform
symbols. No emulator was launched for an outdated full-player build.

### Original castle resource reload runtime checkpoint

build3ds/build_player_probe.py compiles a focused entry point and substitutes it
for smoke_player.o in the existing player link command. It reuses current player
objects and normal section garbage collection, without stubs or relaxed undefined
symbols. Build the player objects first; this utility is not a dependency-aware
replacement for CMake. Output has a distinct ELF/3DSX and leaves player artifacts
unchanged. It provides fast validation of reachable subsystems while the full
Stage link still exposes missing platform implementations.

smoke_castle_reload uses this path with original castle constructors/destructors,
actual native PMF constants, original resource-handle initialization and the real
file-ID catalog. The first test omitted Heap::SetupRootHeap and faulted when the
registration vector allocated through the game's new; it is recorded as a failed
probe, not a game regression. Added the game's normal root-heap initialization.
Also moved ownership metadata to an explicit malloc/free vector allocator, since
this registry must survive destruction of scene heaps and must not use the game's
current-heap C++ allocator.

Azahar castle-reload-native-allocator PASS: three load/unload cycles;29 registered
nodes on each load;704 bytes of initialized BSS restored identically; a modified
spawn vector and numeric root restored on re-entry; destructor list returns to its
preload head; duplicate loads and inactive unloads rejected. The isolated callback
probe also passes registration-during-cleanup and IRQ semantics with the new
allocator (overlay-destructors-native-allocator). No actors/models are spawned by this focused test, so it does not
prove actor resource release, visual transitions, full Stage or playable game.

### Native LoadOverlay/UnloadOverlay dispatch checkpoint

The player now defines real native LoadOverlay/UnloadOverlay entry points via
unmatched/native_overlay.cpp. The original twelve-entry region table is retained,
including original DS start/size overlap checks. Supported loads restore numeric
data and, for castle9, execute the full imported constructor/BSS lifecycle;
unload invokes castle resource teardown and clears the region entry. Duplicate
loads, unknown IDs, unsupported overlays, overlap and inactive unload are explicit
errors. The public game wrappers abort on those errors; diagnostic try-functions
return a specific status without pretending the load succeeded.

native_overlay_info.py reads the user's actual ARM9 overlay table plus extracted
module bytes/symbols. Only zero-BSS, zero-constructor, zero-function level overlays
are accepted as numeric-only. Overlay58 is deliberately excluded: it has closet
actor code despite lacking constructors and BSS. Supported IDs are8,9,11,28,37,38,
40,41,42,46,48,49,50,51,53,54,57,59. Common actor overlays and other levels remain
explicitly unsupported until their native code/data lifecycle is integrated.
Configure SM64DS_SOURCE_ROM to the user's local .nds path; this new CMake cache
entry provides the overlay header, while assets remain local. Generator inputs
include that ROM, module symbol files, all extracted overlay binaries and level
metadata/generators.

Manual actor fixture loading now calls LoadOverlay(9) explicitly. The Stage
construction branch leaves castle loading to its original LoadLevelOverlays path,
avoiding an artificial duplicate load before Stage initializes. Common fixture
profiles/resources remain partial and need replacement by real overlay dispatch.

Azahar native-overlays-azahar PASS:18 supported overlays,36 load/unload cycles,
restored numeric root after mutation, duplicate/overlap rejection, destructor-list
survival and explicit rejection of unsupported overlays10/58/100 and unknown999.
This validates loader dispatch, not eighteen playable levels. Full-player link
closure is now3:func0205b608 (sound status),0205f77c (sleep),020618b8 (wireless).
Remaining explicit runtime failures, audio initialization, full Stage loop and
actor/progression/60fps/stereo completion are not covered by that link count.

### Toad native actor integration checkpoint

The player slice now compiles the original src/game/actors/d_a_kinopio.cpp,
__sinit_ov085_0212f2a8, func02013a44 (save cap flag predicate) and the original
ApproachLinear2Riii helper. Despite that helper's reference signature, its original
implementation updates a byte; it is correct for Toad's opacity field.

port/tools/toad_native.py imports the four star requirement bytes and validates
ROM PMF records, profile fields, state table shape and state names before emitting
native member pointers. The two State records preserve20-byte stride and native
name pointers. Resources/destructor nodes occupy a contiguous60-byte BSS range.
Compile-time checks prove native actor size0x210, State size0x14 and profile size28.
The existing actor fixture publishes g_profile_KINOPIO at actor185 and invokes its
original initializer under overlay85 ownership. This is one more imported actor;
overlay85 remains unsupported by the generic loader until its remaining actors,
mutable state reset and complete initialization/profile lifecycle are integrated.

Focused smoke_toad links the real player objects and uses original factory,
resource loading, state dispatch, behavior and cleanup. Azahar PASS in
validation/toad-behavior-azahar: actor size528, body/idle/talk assets loaded,
state0/message0/opacity255 after initialization,60 Behavior calls with no player
reduce opacity to60 while keeping idle state, then cleanup/deletion and all3
resource destructor registrations drained. This test deliberately uses a normal
Toad with starID255 and no cap companion. It does not validate rendering, player
head tracking, conversations, awarded stars, companion variants or60fps timing.
Full-player link remains at the three audio/sleep/wireless platform dependencies.

### Peach native actor integration checkpoint

The player now compiles original daPeach_c.cpp and __sinit_ov085_0212f3a0.
peach_native.py uses the ROM-validated native PMF generator for all10 callbacks
and the contiguous240-byte resource/state BSS at021304bc..021305ac. It checks
all7 animation-handle pointers and actor-profile fields against the overlay before
emitting native relocations. Compile-time size checks prove daPeach_c=0x36c and
profile=28. The actor fixture registers profile186 and initializes these resources
under overlay85 ownership. That remains a partial overlay85 startup, not permission
to mark the entire common overlay supported.

Focused smoke_peach uses original factory/resource load/state methods/cleanup,
linked against the actual player objects. Azahar peach-native-azahar PASS:
actor size876, all10 state PMFs match native member identities, body and7 animation
resources have live pointers/refcounts,60 Behavior calls retain the default idle
pair/state0, then CleanupResources drops all8 pointers/refcounts to zero. Actor
destruction and8 global destructor registrations complete. No player interaction,
dialogue, ending cinematic, rendering or simulation60fps timing is claimed.
Full-player link still has the three audio/sleep/wireless platform dependencies.

### Wall sign and rabbit-key actors checkpoint

Original daObjKanban_c.cpp and __sinit_ov085_0212fdb0 now compile. The generated
kanban_native data verifies actor183's ROM profile, imports collision offset
(0,-0x28000,0), and preserves the20-byte shared model/destructor storage. Native
class size0x368 and profile size28 are checked. Azahar kanban-native-azahar PASS:
original factory/model initialization,60 no-player Behavior calls keep idle state,
model pointer/refcount cleared by original cleanup, actor deletion and one global
resource callback drained. Reading a sign with a player and visual rendering are
not validated by this probe.

Original daObj_Mip_Key_c.cpp and __sinit_ov085_0212f9bc now compile. The native
PMF generator relocates four state callbacks, preserves the two16-byte state
objects at0213071c/0213072c and checks actor229's profile against ROM. Native
actor size0x1a0 is verified. Original StartMinigameMenu, func02013868 and0201361c
are linked to retain menu-transition/save-unlock calls. dScene_c already provides
the transition methods; no replacement success path was added.

Azahar mip-key-native-azahar PASS: actual Mip resource initializer plus key state
initializer, original factory and key model load, timer10 initially and0 after60
Behavior calls, original0x1000-per-call rotation gives angle49152, loose state
persists without a player, model refcount/pointer released, actor deleted and all
11 Mip resource/vector callbacks drained. Catching a key, cutscene/camera/audio,
minigame unlocking and entering the menu remain untested.

Both native profiles are published by the actor fixture under overlay85 resource
ownership. All six original overlay85 initializers are now compiled, but the
overlay is deliberately still unsupported by generic LoadOverlay: its dispersed
BSS must be reset together, mutable data restored as needed, all six profiles
registered/cleared coherently, and its complete lifecycle verified. Full-player
link remains at the three audio/sleep/wireless platform dependencies.

### Complete overlay85 initialization lifecycle checkpoint

The six actor families now share a single native1024-byte BSS block. Removed the
separate Toad/Peach/Mip/key/Jugem/Kanban resource allocations from their generators;
all79 module BSS symbols retain exact original offsets (overlay85-bss-verification).
Native Mip/Jugem profiles are additionally checked against their ROM descriptors.
Member-pointer constants and initialized state names remain native relocated data.

unmatched/overlay85.cpp clears that BSS and calls all six original initializers
in the ROM order, under scope85. It installs actor profiles185/186/187/229/235/183
and restores the previous profile pointers on unload after draining destructors.
The original initializers reconstruct the runtime PMF tables on each load; callers
must still destroy active actors before unloading. The generic registry now accepts
overlay85 as kind3, so there are19 supported overlay lifecycles. The actor fixture
calls LoadOverlay(85); scattered registrations/initializations for these six actors
were removed. Stage leaves loading to its original object-overlay selection.

Azahar overlay85-reload-azahar PASS: three cycles with castle9 concurrently loaded,
28 destructor registrations on each load, full initialized BSS restored identically,
a mutated Mip vector and cleared Toad callback entries restored, all six profiles
published and restored, duplicate/inactive operations rejected, and castle/main
resource lists preserved. This validates lifecycle state, not all actor interactions
or gameplay. The generic numeric-level probe excludes kind3 because the dedicated
probe covers it. Native overlay metadata now regenerates with CMake VERBATIM,
fixing shell quoting of the supplied ROM filename's spaces and parentheses.

Full-player link remains at the three audio/sleep/wireless platform dependencies.
Common actor overlays98/100/102 and other dynamic levels still need lifecycle work;
full Stage progression, audio,60fps/fullscreen/stereo completion remain unverified.

Peach resource/60-call behavior/cleanup regression also passes against the unified
overlay85 BSS (peach-overlay85-bss-regression). No visual/gameplay claim follows.

### Waterbomb native actor checkpoint

Original daWbm_c.cpp and __sinit_ov098_0213c2b4 now compile in the player slice.
waterbomb_native.py validates the seven-word profile and six fragment-angle
entries against overlay98, relocates three PMFs and supplies the 24-byte state
BSS at 0213c930. The native actor size is asserted as 0x3cc. The actor fixture
publishes profile208 and calls the full initializer, replacing the previous
resource-only helper in cannon_data.cpp. Original game sources are unchanged.

Azahar waterbomb-native-azahar PASS: original factory and real asset loading for
variants0/1/2, correct states/scales/gravity, native state pointer identity,
60 idle Behavior calls for the dropper (timer10 to0 without a player), 60 direct
fall-state calls for each other variant, model reference/pointer release and
actor deletion. All24 shared resource registrations and one overlay98 registration
are drained. The fall-state checks deliberately have no collision scene; they do
not validate motion, landing/bounce, hits, fragment spawning, sound or rendering.

Full-player link still has the same three audio/sleep/wireless dependencies.
Overlay98 lifecycle remains unsupported until arrow signs and the small block,
complete BSS reset and profile/resource ownership are integrated and validated.

### Arrow sign native resources checkpoint

Both YAJIRUSI_L/R profiles now have ROM-checked native factories and the exact
six-pointer model/KCL/CLPS table, including overlapping symbols at offsets0/4/8.
The eight resource/node symbols share80 bytes of BSS with original offsets.
The two missing32-byte shared CLPS blocks are imported from overlay2. Original
arrow source was already inherited through gate9; the full-player target now
replaces that TU with a generated version (the separate gate9 target is retained).

The generated OnAttacked1 adapter avoids an undefined int-function return under
GCC. The generator checks all16 original ARM instructions: no-op returns this,
Bob-omb attack forwards Kill's register via virtual slot31. The compiled native
vtable has Kill at offset0x84 from the table start, or slot31 from its +8 address
point. Original src and headers are unchanged.

Azahar arrows-native-azahar PASS: both original factories, size0x380, appropriate
variant selected by actor299/300, model and KCL loaded with valid pointers and
reference counts, no-op attack return preserved,60 idle Behavior calls each,
collision disabled during cleanup, handles released and four resource callbacks
drained. This does not prove visible rendering, collision response with Player,
Mega destruction, Bob-omb destruction or sound. Full-player link remains at three
platform dependencies; overlay98 still needs small-block integration and complete
lifecycle/reset verification before generic LoadOverlay support.

### Small block native actor checkpoint

Original daObjBlockS_c and __sinit_ov098_0213c058 are now in the player slice.
smallblock_native.py relocates fourteen state PMFs,152 bytes of resource/state
BSS, the three-pointer resource table and80 bytes of carry/launch numeric tables.
The profile is checked against ROM (actor194,priority37,flags0x82); native size
is asserted as0x608. Shared overlay2 CLPS0210d934 is imported. The actor fixture
publishes BLOCK_S and calls its initializer under overlay98 ownership.

Added original RegisterEggCoinCount, GivePlayerCoins and rotation-ZXY helper.
The two coin tables are extracted from ARM9: popup actors288/289/290 and values
1/2/5. GivePlayerCoins deliberately null-checks its reference argument in ROM;
per-source -fno-delete-null-pointer-checks retains that ABI guard, confirmed in
the generated ARM object's cmp r1,#0 / early-return sequence. No src/header edits.

Azahar smallblock-native-azahar PASS: original factory, real model/KCL load,
idle PMF identity, initial/home state and gravity, ten full Behavior calls in
an empty collision scene (y4194304 to3743744; speed0 to-81920), cleanup disables
collider and releases both handles, deletion and two destructors drained.
This proves free-fall integration, not ground response, carrying, Yoshi conversion,
coin payout, destruction, respawn, audio or visual rendering. Full-player link
remains at audio/sleep/wireless dependencies. All four overlay98 initializers are
compiled, but generic loading still requires unified BSS/profile lifecycle work.

### Overlay98 complete initializer lifecycle checkpoint

Unified all19 BSS aliases into one352-byte block at the original relative offsets,
including the final padding. Removed separate resource/state allocations from the
arrow, small-block, waterbomb generators and cannon_data. overlay98_native.py
also verifies the cannon profile against ROM. The four initializers run in ROM
order under ownership98; five profiles299/300/194/207/208 are installed and their
previous values restored on unload. Active actors must be destroyed beforehand.
The original initializers reconstruct all runtime state tables. Generic native
LoadOverlay now supports98 (kind4), bringing supported overlay lifecycles to20.
The actor fixture uses LoadOverlay(98) instead of scattered initializations.

Azahar overlay98-reload-azahar PASS: three cycles with castle9 loaded, eight
resource registrations per cycle,352 initialized BSS bytes identical after reload,
cleared small-block/waterbomb state tables restored, five native profiles installed
and restored, duplicate load/inactive unload rejected, castle/main destructor lists
preserved. overlay98-bss-verification.txt confirms all19 offsets in the linked ELF.
Azahar smallblock-overlay98-lifecycle PASS verifies model/KCL loading, original
free-fall Behavior and cleanup using the actual generic LoadOverlay/UnloadOverlay
path after the BSS consolidation. The numeric-only overlay probe now excludes
all actor kinds, which have dedicated lifecycle probes.

This is lifecycle support, not full game validation: actor interactions, scene
transitions with live actors, sound and gameplay remain to be exercised. Overlays
100/102 and many other actor groups still need native lifecycle support; the full
player link retains its three audio/sleep/wireless platform dependencies.

### Native console sleep and mirrored main RAM checkpoint

platform/3ds/console_sleep.cpp implements the game's sole PM sleep call0xc/0/0
using PTMU shell state and APT sleep handling. It requests sleep while closed,
waits for asynchronous APT notification/reopening, preserves prior sleep policy
and emulated IRQ masks, and reconciles the shared DS lid flag on return. Unsupported
wake modes and service failures fail explicitly. Cartridge-removal wake has no
native SD-asset equivalent. APT owns graphics/power resume. Source reference:
https://github.com/devkitPro/libctru/blob/master/libctru/source/services/apt.c

The first Azahar check failed: shared address027fffa8 was outside the allocated
4MiB main-RAM mapping. host_ptr now folds main-RAM mirrors at4MiB boundaries,
for translated3DS and desktop pointers (other regions unchanged). This matches
retail DS addressing in melonDS NDS.cpp (MainRAMMask0x3fffff):
https://github.com/melonDS-emu/melonDS/blob/master/src/NDS.cpp
Only translated accesses gain this behavior; arbitrary raw mirror dereferences
still require hostgen. func02019ac4 is now generated to translate its shared lid
halfword, leaving the original source intact.

Azahar console-sleep-mirrored-ram PASS: PTMU reports open shell; original game
wrapper returns and clears its in-sleep flag, stale lid bit cleared while lower
bits preserved, IME0/1 and IE0x1234 preserved, prior APT allowance unchanged,
allfour RAM aliases map to the same pointer. This does NOT exercise physical sleep.
Host check_console_sleep.py uses the actual adapter with mocked APT/PTMU to test
notification delay, three wait polls, reopening, prior allowed/disallowed policy,
open-shell path and explicit unsupported-mode failure. Host mapping checks also
cover allfour banks at257-byte intervals for desktop/3DS translation branches.
Full-player closure is now TWO symbols:0205b608 audio status and020618b8 wireless.
Native audio/ARM7 IPC, full Stage boot/gameplay, true60fps and stereo completion
remain outstanding; sleep on physical hardware is unverified.

### Original wireless status query checkpoint

Imported original func020618b8, func0206152c and WM_GetSystemWork. The initialized
halfword and work pointer now occupy the original8-byte BSS span020a89a8..89b0.
This is the SDK's real query logic: uninitialized returns0 without dereferencing
the work pointer; initialized states10/11 return the status halfword at0xb8,
and state9 does so only if the halfword at0x17e is nonzero. Other states return0.
The game's existing0203d9f4 gate still suppresses the query unless bit0x8000 is set.
No always-zero/native-network substitute was introduced; no source files changed.

Azahar wireless-status-azahar PASS: uninitialized/null work guard,128 controlled
state/flag/value combinations, game gate both enabled/disabled and restoration
to uninitialized state. This exercises queries with a controlled work buffer,
NOT radio initialization, DS wireless transport, Download Play or multiplayer.
Full-player link now has ONE unresolved function:0205b608, ARM7 audio status.
The sound pointer020a7fc0 must be backed by an initialized audio system: original
0205b358 points it at020a64e0, clears it, then queues command0x19 with that pointer.
Blindly importing the getter or supplying a zero mask would not implement audio.

### Native audio backend candidate investigation

Cloned kode54/SSEQPlayer into build3ds/vendor/SSEQPlayer at commit
77222d3657adff358fb4e610d3e56bb7ada8ec24. Upstream LICENSE.TXT states WTFPLv2;
source attribution also names CyberBotX, FeOS/fincs and DeSmuME concepts/code.
The candidate is not yet linked into the game. Source:
https://github.com/kode54/SSEQPlayer

The supplied ROM's sound_data.sdat has83 SSEQ entries, five SSAR archives
(274/276/514/399/1 entries),134 banks,89 wave archives and zero streams.
build3ds/probe_sseq.cpp compiles the candidate on macOS and synthesizes5 seconds
at32768Hz/stereo16-bit. All83 SSEQ entries generated nonzero PCM with no process
failure. validation/sseq-native-host/report.json records source/SDAT hashes,
per-sequence PCM digests, amplitude, RMS and timing; title.wav is a local example.
The title's5 seconds render in roughly0.011 seconds on this Mac; this says nothing
about3DS performance. check_sseq_native.py rebuilds and reproduces the sweep.

This establishes a candidate sequencer/sample decoder only, NOT audio fidelity,
full-track coverage, SSAR support, sound effects/voice behavior, hardware output,
game command integration or audio completion. Upstream uses a single sequence
Player with16 channels and lacks an SSAR container reader. Its global Player name
also conflicts with the game, so native integration needs symbol isolation.
A signed negative shift warning in consts.h should be corrected in any vendored
production copy. Next steps: portable3DS compilation/performance, sequence-archive
support, command/voice allocation semantics, NDSP output and original shared-status
updates driven by actual active voices. Do not substitute zero audio-status masks.

### ARM11 SSEQ synthesis checkpoint

Vendored the pinned SSEQPlayer sources and license under port/third_party/sseqplayer.
ORIGIN.txt records source URL, commit and local changes: negative constant shift
replaced by multiplication, standard codecvt replaces legacy redefinitions and
legacy facet-ID definitions removed for GCC16. CMake builds sm64ds_sseq separately,
renaming Player/Track/Channel to avoid collisions with game symbols. The C boundary
in audio/sseq_decoder.{h,cpp} opens a local SDAT sequence, renders stereo16-bit PCM
in bounded chunks and destroys the decoder. It is not called by game audio yet.

The first ARM11 probe exhausted the8MiB game root heap during iterator-based
archive vector growth. Read exact file size before allocating, then release the
source archive after SSEQ/bank/waves take ownership of their parsed data. This
fixes that tested allocation peak; persistent synth allocation must still be
isolated from transient scene heaps before gameplay integration.

Azahar sseq-arm11-exact-allocation PASS: native ARM11 decoding of title sequence55,
163840 stereo frames at32768Hz (5seconds),peak23569,325954 nonzero samples,
0.787 emulated seconds including SD writes. PCM is655360 bytes and bit-identical
to the macOS candidate output: SHA256
b7d79f562296d98e3fee600901ab0fc9066110b7069d578d3eb2b7579e561283.
The fresh PCM and comparison JSON are saved with binary/run provenance.
This is not a physical3DS performance measurement or a comparison against DS audio.
NDSP output, SSAR effects/voices, multiple game sequence players, command semantics,
shared ARM7 status and game timing integration are still outstanding. Full-player
link still has0205b608 unresolved intentionally until real audio state exists.

### NDSP output probe checkpoint

`tests/smoke_sseq_ndsp.cpp` now feeds the ARM11 synthesizer into libctru NDSP:
three linear-memory buffers, 1024 stereo frames each, cache flush before queueing,
reuse only after DONE, and explicit queue clear before decoder destruction.
Each pass drains64 buffers (65536 input frames at32768Hz), clears another pending
buffer, and reopens the title sequence. Two passes run per emulator launch.
The probe also captures the DSP's mixed output using ndspSetCapture; capture
storage is read only after ndspExit joins its worker. This is a focused output
probe, not yet a game-connected sound service.

Initial ndspInit failed with d880a7fa because no DSP component was present.
Created an empty file ONLY in Azahar's virtual SD at3ds/dspfirm.cdc, following
the official emulator HLE procedure:
https://github.com/devkitPro/3ds-examples/blob/master/audio/README.md
This marker is not firmware and must not be shipped as physical-console firmware.
The native application still uses the real libctru NDSP API without a success stub.

Evidence under build3ds/validation:
- sseq-ndsp-azahar: missing-component failure, retained.
- sseq-ndsp-hle-azahar: unexplained early failure after3 submissions at0.007s;
  no error detail in that initial probe, retained rather than called a pass.
- sseq-ndsp-clock-diagnostic: both64-buffer passes succeed in2.022/2.019s.
- sseq-ndsp-capture-azahar: both passes succeed in2.018/2.022s;
  capture79329 nonzero samples, peak23385. Fresh163840-byte mixed PCM copied
  after run-start timestamp check; SHA256
  d0ecb4eff1351e5c761d651ac70ad7a285051497a1258bd689513af8bae25686.
  WAV uses32728Hz, rounded from NDSP_SAMPLE_RATE=16756991/512.
- sseq-ndsp-capture-repeat: same binary passes again in2.022/2.020s;
  capture79649 nonzero samples, peak23385. Ring-capture phase is asynchronous,
  so its hash is not a deterministic synthesis oracle.

Azahar was terminated between every launch and confirmed absent afterwards.
These results prove emulator DSP buffer consumption and nonzero mixed output,
including drain/clear/restart; they do not establish DS audio fidelity, physical
hardware operation, concurrent game sound effects or the original ARM7 command
protocol. Persistent decoder memory and SSAR/voice handling remain next tasks.

### SSAR archive reader and synthesis checkpoint

Added bounded SSAR table/header/entry parsing in third_party/sseqplayer/SSAR.cpp.
SDAT now accepts an archive index and optional bank override; audio/sseq_decoder
exposes sm64ds_ssar_open. The entire shared event region stays intact and the
first track starts at the selected entry's offset, so jump/call/open-track offsets
continue to address the original region. Empty entries and invalid indices fail.
Layout comparison uses the installed independent ndspy.soundSequenceArchive reader.

The ROM contains1464 SSAR entries:1448 present and16 absent. All395 present SCENE
entries name bank4, which is deliberately absent from SDAT. The original game
passes a bank pointer independently (see func_0204f600); callers must supply the
loaded level bank. The focused test supplies bank32 (T_KINOKO) explicitly as one
fixture, not as a claimed universal mapping for every scene sound.

build3ds/probe_ssar.cpp and check_ssar.py test every entry. The current report in
validation/ssar-native-host/report.json has1439 successful one-second renders,
16 correctly rejected absent entries, and9 outstanding render errors. Successful
cases compare entry offset, bank, volume and full event-region FNV against ndspy.
237 successful renders are silent; a successful render is not a DS fidelity test.
Seven errors require dynamic wave bindings (VOICE34,92,98,162,226; SYSTEM57,102).
Two SYSTEM entries424/440 loop without advancing time. These remain unsupported,
and the comprehensive checker intentionally exits1 until they are implemented.

The sweep exposed an upstream channels[-1] reference in ChannelAlloc, now fixed
by constructing the reference only after a candidate exists. Wave lookup checks
missing bindings/sample IDs instead of dereferencing null/end. A65536-command
limit throws an explicit diagnostic on non-yielding tracks; it does NOT claim to
implement Nitro's zero-length-note scheduling. Selected AddressSanitizer/UBSan
checks now report no violation and preserve explicit failures for those cases.

validation/ssar-arm11-scene-bank PASS: five representative archive entries render
on ARM11, then invalid archive/entry indices are rejected. Their one-second PCM
FNV/nonzero/peak values match the host results (host-comparison.json):
PLAYER3 ed449809; VOICE0 67bc9bea; SYSTEM0 f0d9d361;
SCENE0 with bank32 f1e5f9c6; NINTENDO0 09db4397.
validation/sseq-after-ssar PASS: the five-second title remains b1442dcc with the
same23569 peak and325954 nonzero samples as before. Azahar killed between runs.

Still required: proper zero-time sequence semantics, game-supplied wave/bank
bindings, persistent decoder allocations, shared16-channel allocation across
players, command processing/status synchronization, and game-loop integration.
Full-player unresolved0205b608 has not been replaced with a dummy status getter.

### Zero-duration note semantics verified against ARM7

The ROM's ARM7 track runner at02384c98 explicitly sets track flag0x10 after a
zero-duration note with note-wait enabled (02384e3c..02384e4c). Its event loop
exits when that flag is set (0238554c..02385568). On the next invocation it
returns while track+0x3c, the active-channel linked list, is nonempty, clearing
the flag only when that list becomes empty (02384ca8..02384ccc). Note allocation
links channels at023856c4..023856cc and its callback removes them at023858e8.

Track::waitForNotes now models that condition using the native player's channel
ownership/state. It resets with track state and does not impose an arbitrary
delay or mark the sequence finished. build3ds/check_ssar_note_wait.py executes
the original ARM7 routine in Unicorn, replacing only cached byte reads and note
allocation with controlled inputs. Durations0,1,7 are checked through initial,
held-channel and emptied-channel states. The separate native probe_note_wait.cpp
matches these state transitions. Evidence and ROM hash:
validation/ssar-zero-note/{rom-reference.json,native-track-check.txt}.

SYSTEM424 and440 now render instead of hitting the command-limit diagnostic.
validation/ssar-zero-note-arm11 PASS: original five representative cases unchanged,
plus424 FNV36385152/nonzero64114/peak5361 and440 FNVbad03233/nonzero64114/peak10112.
All seven outputs match host PCM hashes, counts and peaks. Full SSAR sweep now
has1441 successful renders,16 absent entries and7 explicit missing-wave-binding
errors; its exit1 remains intentional. The command limit remains a diagnostic
for other non-yielding data, not a scheduling policy. This does not yet validate
all audio fidelity or integrate the original game's command queue.

### Cached instrument-bank wave bindings

Recovered func_02051a98 looks up an SBNK by its file ID, then calls0205b78c only
for wave slots whose metadata is notFFFF. Therefore aliases of an already loaded
instrument file inherit its other bindings. The ROM metadata confirms file sharing:
Mario banks6/11/12 use file88, Luigi7/13/14 file89, Wario8/15/16 file90,
Yoshi9/17/18 file91, and system1/2/10/21/27 file85. Creating a fresh bank from each
alias loses these bindings and explained the seven remaining probe failures.

SDATBankState and public Sm64dsBankState now carry the instrument file ID and four
wave archive IDs. sm64ds_ssar_open accepts optional caller-owned state: matching
file IDs preserve omitted slots; explicit metadata replaces them; different files
start with their own bindings. C API state commits only after successful setup.
Callers must retain one state per cached file and discard it when that file unloads.
This is an input for future game cache/command integration, not automatic guessing
of bindings or loading every available wave. Parsed bank/wave ownership also now
starts before Read, so exceptions release partially constructed resources.

Controlled prerequisites for the seven cases: load full voice banks6/7/8/9 before
their aliases, or system bank21 before system bank1. The full SSAR sweep now passes
1448 present entries and correctly rejects16 absent entries, with237 silent first
seconds. SCENE still uses explicit fixture bank32; the report records prior banks.
This does not prove every sequence is correct in every real scene/bank combination.

validation/ssar-bindings-arm11 PASS: all seven cases generate nonzero ARM11 PCM
matching the host hashes/counts/peaks, and each case verifies that switching to
Nintendo file93 drops inherited slots. FNV values:
VOICE34=7159dec3,92=09eb332e,98=d6af1702,162=28d5d34f,226=66a7e373;
SYSTEM57=d8668397,102=4827c263. The owned-resource rerun uses the final allocation
ownership ordering. Both runs terminate Azahar before/after execution.

The sequencer can now accept these cached bindings, but original ARM7 command
processing, persistent audio allocation outside scene heaps, shared channel
allocation and gameplay-triggered sounds remain outstanding. Full-game status
0205b608 is still unresolved rather than returning fabricated completion masks.

### Audio allocation survives scene-heap destruction

The game's global C++ new/delete use Memory::defaultHeapPtr. Decoder open,
render (including lazy PCM allocation), and close now select the persistent root
heap with a scoped guard, restoring the caller's context afterwards. The guard
encloses catch blocks as well: exception-owned strings must be destroyed before
restoring a transient scene heap. These APIs are synchronous game-thread calls;
they must not be invoked from the NDSP worker, and the root outlives all decoders.

The first lifetime probe exposed fragmentation: while a title decoder and a
256KiB scene heap remained live, another4.46MB full-SDAT allocation failed despite
7.6MB aggregate free space. PseudoFile now also accepts an external non-owning
byte view. The C boundary reads the temporary full SDAT into a malloc-owned buffer,
then frees it after parsing; persistent parsed data/PCM stay on the game root.
This fixes that concrete peak/fragmentation pattern without enlarging the game
arena. It is not yet a shared bank/wave cache across simultaneous players.

Linked original Heap::Destroy in the player target for genuine scene teardown.
validation/sseq-persistent-heap PASS: two independent title decoders each render
5seconds while the default heap is a256KiB child. After the first chunk, destroy
that child, recreate it and overwrite200KiB withA5. Both PCM hashes remain the
known b1442dcc. Open/render/close and rejected sequence999 leave child free space
and default-heap selection unchanged. After decoder/child destruction, root free
space returns exactly to8388512 bytes on both passes. Earlier failed runs remain
under sseq-root-heap and sseq-root-heap-diagnostic for provenance.

The seven inherited-wave SSAR cases are also rerun through the external byte-view
path in validation/ssar-persistent-heap. Original game audio IPC/status integration
and shared player/channel/resource allocation are still outstanding; successful
focused probes do not imply that smoke_player can yet link or run the full game.

### Sequence activity and note release

The C decoder boundary now exposes sm64ds_sseq_playing (any unfinished sequence
track), sm64ds_sseq_channels (active synthesis-channel mask), and stop with either
release or immediate kill. These are different lifetimes: a stopped/ended sequence
may still have release tails. A kill after a previous release stop also kills those
tails even though the track list has already been freed. These queries describe
the synthesizer, not PCM still queued in NDSP.

Corrected END to release its track's active notes. The ARM7 aggregator02384c1c
calls02385968 for each ended track; that path invokes the release routine at
02385ab0. check_sequence_completion.py executes the original aggregator with eight
controlled combinations of running/ended/absent tracks, verifying which tracks
are released and when the whole sequence is reported ended. Evidence with ROM
hash is in validation/sequence-completion-rom/report.json.

validation/sseq-state-arm11 PASS: Nintendo SSAR finishes naturally within40
1024-frame chunks, active channels have been observed, and both sequencing and
channel states become zero. Title release-stop reports sequencing stopped while
channels remain, drains in40 chunks and then renders exact silence. Release-stop
followed by immediate kill drains in zero chunks and also renders silence.
validation/sseq-state-heap-regression PASS retains the five-second title hash and
complete root-memory recovery across both scene destruction/reuse cycles.

The full controlled SSAR sweep still passes1448 present/16 absent entries. END
release changes43 first-second PCM hashes (track-end-release-diff.json); the prior
report is retained as before-track-end-release.json. This reflects corrected
note release, not a newly established bit-exact DS audio comparison. Multi-player
shared channel allocation, full track-slot reuse semantics and the original game
command queue/status publication remain to be integrated. No dummy0205b608 getter
has been installed.

### Logical audio track allocation and reuse

ARM7 setup023862c0 reads an initialFE mask at the selected sequence entry, always
allocates logical track0 and reserves the requested slots1..15. OPEN_TRACK at
02384f1c looks up an existing logical slot, ignores missing/self targets, releases
its current notes and changes only its sequence position (02385bc8). It does not
append a new track or reset that track's volume/wait parameters. The previous
candidate appended every open, allowing trackIds[16] to overflow on repetitions.

Player::Setup now takes the actual entry offset and consumes the initial mask.
trackIds is a sixteen-slot logical map withFF for missing slots. Execution visits
logical slots in order, skips reserved-but-unstarted tracks and frees ended slots.
Open restarts only existing non-self slots; released channels detach from track
ownership before storage can be reused. Hard stop also kills detached tails.
The wrapper and host SSAR probe pass entry offsets into Setup rather than changing
the first track pointer after setup. sm64ds_sseq_playing ignores unstarted reserves.

validation/sseq-track-slots PASS in Azahar:1024 repeated opens keep exactly two
slots, ended slots cannot be implicitly reallocated, unreserved opens are ignored,
zero/sparse/high/all-bit allocation masks stay within16 logical slots, and release
tails detach before hard stop. check_open_track.py executes five controlled cases
through the ROM runner and confirms existing/self/missing behavior plus preserved
wait/volume; ROM hash/results are stored alongside the native run.

validation/sseq-track-state-regression PASS retains natural completion, release
drain and immediate silence. The controlled SSAR sweep still passes1448 present
and16 absent entries with237 silent first seconds. The title/scene-memory test is
rerun under sseq-track-heap-regression. This implements each player's logical
track behavior, but the candidate still owns a separate pool per player; the DS
global pool/shared channel allocation and game command dispatch remain outstanding.

### Decoder entry point for game-loaded sound resources

The original start command0 (func_0205adc4) carries player index, event-data base,
entry offset and loaded SBNK pointer. The SBNK's four binding slots refer to loaded
SWAR buffers. Added sm64ds_sseq_open_data with explicit bounded event/bank/wave
spans, offset, rate and volume, so future command dispatch need not translate a
live game sound back to an archive ID/path. It snapshots the event region and
parses owned bank/wave data under the persistent root-heap scope. The caller must
provide the actual current four bindings and valid resource extents; command
dispatch and loaded-resource extent resolution are not yet connected.

PseudoFile scalar/byte reads now reject spans beyond the supplied input size.
Loaded SBNK/SWAR inputs also check their declared file size; allocation-mask-only
event data without a command is rejected at setup. These checks are for bounded
game-resource inputs, not a claim of a fully hardened arbitrary-file decoder.

validation/sseq-loaded-buffers PASS: title SSEQ55 and Nintendo SSAR4:0 each produce
5seconds of identical PCM through path-based and loaded-buffer entry points
(FNVb1442dcc and809e38ff). The test overwrites the SBNK's reserved binding-header
region to model the game's cached pointers, opens the decoder, then overwrites
and frees the entire original SDAT copy before rendering. Both cases recover
exactly8388512 root-heap bytes on close. Invalid entry offsets and short banks
are rejected without allocation loss. The additional sseq-loaded-bounds run also
tests a valid16-byte header whose DATA payload is missing, exercising bounded
reads rather than only the outer size check. Azahar is killed between launches.

This provides the decoding seam for command0; no command is acknowledged as
success without execution, and the full game's audio-status getter remains
unresolved pending the actual sound service and shared resource/channel handling.

### First native sound-command executor

audio/sound_commands.{h,cpp} now accepts command0 start and command1 stop using the
original argument order. ARM7 dispatch02386dd8 forwards all four start arguments
to023862c0; dispatch02386df0 forwards the player index to02386260 for release-stop.
The executor resolves event/bank addresses only inside explicitly registered
resource extents and resolves the four actual32-bit SBNK binding pointers at
0x18+8*i. Missing, overlapping or overflowing registrations are rejected. It does
not reopen SDAT or choose wave banks by a guessed sequence ID.

The executor maintains16 player slots and computes an active-player mask from
actual unfinished sequencing. Stop initiates release tails, per-player rendering
drains them, and quiet players are reclaimed. Unsupported commands and replacement
of a still-occupied player return explicit errors without modifying its playback.
That replacement restriction is temporary: preserving its old tails requires the
shared-channel mixer, which is not implemented yet. The per-player render method
is a test/integration primitive, not a substitute for that mixer or NDSP scheduling.

validation/sound-start-stop-commands PASS in Azahar: register title resources,
bind native SWAR pointers into the SBNK, start player3 (mask8), render the known
five-second title FNVb1442dcc, stop (mask0), drain to silence, then reuse the slot.
Unsupported command/replacement, unregistered sequence and player16 are rejected.
This exercises execution, masks and resource lookup; it does not yet exercise the
original linked command queue, IPC acknowledgements, resource-cache registration
hooks, all sound opcodes, simultaneous shared16-channel mixing, or game playback.
The ntr IPC fail-fast path and unresolved0205b608 remain until those are connected.

### Shared sequencer channel/track pool

The decoder core now supports a SoundPool shared by multiple Players: one array
of32 tracks, one array of16 hardware channels, and one sample/clock scheduler.
Standalone Players own a private pool and retain their public rendering API.
Shared Players must render through the pool; per-player rendering and duplicate
entries in the clock list are rejected. Each channel retains its owning Player,
which must outlive its release tail. Stop/hard-stop/destruction affect only that
owner; allocation transfers ownership when a higher-priority note steals a channel.
Release-stop detaches track IDs before those global slots can be reused. The pool
must outlive its Players and sequence/wave data must outlive their channels.

validation/sseq-shared-pool PASS in Azahar: two panned PSG sequences rendered in
317-frame partitions exactly equal the independent PCM sum (64143 nonzero
samples). Stopping one retains the other sample-for-sample at the original clock
position. Two16-track sequences exhaust the global32-track pool, release permits
reuse, channel priority enforces the global16-channel limit, stolen channels are
not killed by their previous owner, and replacement setup preserves another
owner's detached release tails. This is a core test, not game IPC integration.

The complete SSAR host sweep retains every prior result (1448 present,16 absent,
237 silent first seconds; shared-pool-diff.json lists zero changes). The Azahar
sseq-shared-heap-regression retains title hash b1442dcc and complete8388512-byte
root recovery across scene destruction/reuse. Command service ownership/lifetime,
replacement integration, resource hooks and the NDSP pump remain outstanding;
its temporary occupied-player rejection is still present. The full game is not
claimed to boot by these focused tests.

### Native DS slope-contact comparison

The DS reference task added three reproducible Mario resting states on Castle
grounds slopes, in build3ds/reference-ds/slopes. Diagnostic character/position
writes are explicit; the retail engine simulates180 frames after placement.
Independent runs produce identical RAM/framebuffers. Player Y equals floor Y,
with zero sinkDepth/bodyYoffset. Resting contact uses dBgCh_SphCrr, not the
continuous movement ray (the captured ray has no collision).

smoke_floor_reference replays the measured sphere centers, radius204800,
wall-slab extent163840 and scale4096 through the port's original KCL sphere
solver, then func_02037a38. validation/ds-slope-sphere-reference PASS:

| Point | DS/3DS final Y (Fix12) | DS/3DS pushback Y | Normal (Fix12) |
|---|---:|---:|---|
| west_bank |1382513|49164|(1148,3860,-748)|
| castle_lawn |2785137|49208|(-88,4060,528)|
| opposing_bank |1596188|49154|(-1188,3892,476)|

All corrected heights and displacement components match exactly. The independent
vertical-ray diagnostic finds the same normals but lower Y by6257,881 and5468
Fix12 units. The initial test wrongly assumed ray height equals spherical player
support and is retained as validation/ds-slope-floor-reference (FAIL). The final
test compares the correct primitive; it adds no arbitrary player-height offset.

The probe uses ordinary surface filtering and a zero/default CLPS block, not a
full actor/scene replay or exact material-behavior test. It validates static slope
contact geometry at three selected points; animation phase, all surfaces and
full-game movement remain outside this evidence. Native DS screenshots, object
blobs and camera/projection measurements are available for later render alignment.

### Shared mixer connected to commands and NDSP output

Sm64dsMixer now owns the one SoundPool,16 logical players and up to16 retired
owners. Start parses/copies resources before releasing an old player; retired
owners retain their bank/waves until their last channel dies or is stolen.
Each retired owner has at least one distinct live hardware channel, bounding
retention by16. Replacement frees its tracks immediately while keeping its
channel tails. Collection and all allocations/destruction use the persistent
root heap. Public channel masks now filter by owner; hard-stop cannot kill
another player's channels. The per-player command render primitive is replaced
by a whole-mixer render, so multiple players never advance the hardware twice.

validation/sound-shared-commands PASS in Azahar: original title hash b1442dcc,
32 successive replacements while sounding, simultaneous players3/5, stopping3
preserves5, unsupported commands rejected, invalid resource parsing preserves
an existing player's channel mask, replacement retains one old owner, final
stop/drain yields zero channels/retired owners and exact silence. Destruction
recovers the complete root free space8388512 before/after. This supersedes the
earlier temporary occupied-slot rejection in the command executor.

Sm64dsSoundOutput is a reusable game-thread NDSP pump with three512-frame stereo
buffers at32768Hz. Only completed/unowned descriptors are refilled, PCM is flushed
before submission, and close clears the queue and exits NDSP before freeing the
linear buffers/descriptors. No decoder runs on the DSP worker. Its header hides
libctru types to avoid conflicts with the original game's u32/s32 ABI typedefs.
The output owns channel0 and expects exclusive NDSP lifecycle ownership.

validation/sound-shared-ndsp PASS:120 pump iterations per pass with nominal16.67ms
sleeps, starting a second player, stopping the first, replacing the second, then
closing with queued audio and reopening. Passes took2.510/2.574seconds including
work and sleeps;160/162 buffers submitted and157/159 completed. DSP captures
contain79933/79983 nonzero samples with peaks22240/22582. Fresh PCM/WAV and source
mtime/hash records are saved beside run.json (WAV rate32728 for the DSP capture).
This is audio pipeline evidence, not a60fps game-loop or physical-hardware result.

The output pump and command service are still not hooked to the original game's
IPC queue/resource cache/lifecycle. Remaining command opcodes, shared status and
the real0205b608 getter remain outstanding; the existing IPC fail-fast path has
not been bypassed. Full game boot/progression/rendering validation remains open.

### Command2 pause/resume follows the original ARM7

ARM7 command2 dispatch02386dfc calls02386204. Pause stores the player's pause
flag and releases each reserved track's channel list with release override127.
The scheduler023863f0 skips023859f8 while paused but still updates channel
parameters and includes the active player in the published mask. Resume clears
the flag; it does not reinitialize sequence position, wait or tempo accumulator.
check_sequence_pause.py executes both command and scheduler from the supplied
ARM7 in Unicorn with instrumented track lookup/release/sequencer helpers. Its
ROM-hashed evidence is validation/sseq-pause/rom-pause.json.

Player::SetPaused now implements that behavior; Track::ReleaseAllNotes accepts
an optional release override and detaches the released channels. The shared
clock continues updating hardware and other players while Player::Run is paused.
Sm64dsSoundCommands executes opcode2 through sm64ds_mixer_pause. An empty slot
has no sequence to pause, and a later start resets its state as in the original.

validation/sseq-pause-resume-arm11 PASS: five seconds of pause leave track slot,
event pointer, wait and tempo accumulator unchanged, while another sequence ends
normally. Existing notes use release65535 (converted from127), drain, and the
paused sequence resumes its existing wait. The earlier sseq-pause-arm11 failure
is retained: its test waited a full second after resume, beyond this short
fixture's remaining lifetime, and wrongly expected its track to still exist.
The corrected assertion samples31.25ms after resumption.

validation/sound-pause-commands PASS: title FNVb1442dcc, mask8 retained through
five seconds of silent pause,162801 nonzero samples after resume; simultaneous
players/replacements/drain and complete8388512 root-memory recovery still pass.
This is opcode2 support, not completion of the original IPC queue integration.

Next command mapping evidence: original func0205a924 emits raw player-field
command3; callers write offset6/width2 (external signed attenuation), offset4/
width1 (player priority), and offset1a/width2 (tempo ratio). Command4 writes track
fields through func0205a8f0. A newly observed decoder issue is that metadata
info.vol and the open_data volume argument are stored but sseqVol is never
assigned: standalone title metadata volume110 is currently synthesized at127.
The existing hashes establish reproducibility, not correct absolute DS loudness.
This must be corrected alongside the external-volume command before claiming
sound fidelity; raw game command0 starts at127 and receives attenuation later.

### Sequence volume and player attenuation/tempo commands

Player::Setup now initializes sseqVol from the sequence metadata's volume via
Cnv_Sust. Explicit INFO volume0 remains0; the reader no longer replaces it with
127. A synthetic entry defaults to127 until real metadata is read. Loaded-buffer
volume arguments and SSEQ/SSAR metadata now affect synthesis, independently of
in-sequence master-volume events. Earlier full-volume hashes are historical
reproducibility checks, not evidence of the intended metadata loudness.

The five-second title metadata volume110 now produces FNV380174bd, peak17735,
325853 nonzero samples, SHA256 c0173a2ee48e960b5c2f525e3ccb629823ed203a3c978d49cf0e7039ce16e74c
on the host. validation/sseq-loaded-volume matches this title FNV and Nintendo
SSAR FNVaaabf091 through both file-based and loaded-buffer paths on ARM11, with
complete8388512 root-memory recovery. The heap regression's expected title hash
has been updated from the independently generated host PCM, not merely from a
passing native run. Raw command0 still deliberately starts at127, as ARM7 prepare
does; the game supplies its attenuation separately through command3.

validation/sseq-volume-arm11 PASS with synthetic PSG metadata levels0/32/64/110/127:
peaks0/1296/5120/15104/20224. Volume0 renders exact silence; live attenuation to
-32768 silences a still-allocated note, and restoring0 makes it audible again.
The complete SSAR sweep passes1448 present/16 absent entries with237 silent
first seconds.57 PCM hashes change with metadata attenuation; preserved before/
after data are in ssar-native-host/metadata-volume-diff.json.

Command3 now accepts the original two-byte player fields offset6 (signed external
attenuation) and offset1a (unsigned8.8 tempo ratio). It truncates to16 bits and
marks attached tracks for volume refresh so sounding notes change too. Other
fields/widths still return an explicit unsupported error; player priority at
field4 is not implemented by this partial field mapping. Inactive-slot writes
have no observable effect because the next start initializes these fields.
check_player_parameters.py executes ROM02385fc0 for five writes, confirming the
offsets,16-bit truncation and untouched surrounding bytes.

validation/sound-volume-tempo-commands PASS: mute/restore (16286 nonzero restored
samples), width rejection and accepted tempo changes, followed by pause/resume,
simultaneous players,32 replacements and exact root recovery. The raw-command
127-volume title retains b1442dcc. These commands still await the original game
IPC/resource/lifecycle connection. This is not full-game or DS bit-exact audio
verification, and physical3DS/stereo/60fps validation remains outstanding.

### Native player/track priority separation

ARM7 initialization02385b24 sets player priority64; track initialization02385bdc
sets track priority64 independently. Note allocation0238564c adds the two bytes
as an integer before calling023843d4. Channel initialization02383fcc stores the
low byte of that sum. Existing tied notes reuse their channel without rewriting
its priority. The previous candidate baked player priority into the track, with
player default0, so later command changes did not affect future notes correctly.

Player and Track now retain separate priorities, each default64. New notes compare
the full sum for allocation and store its low byte in the selected channel. Tied
notes retain their allocation priority. Command3 field4/width1 now writes the
player byte; together with fields6 and1a this covers all three player-field
writes emitted by the original func0205a924 wrappers. Other offsets/widths remain
explicitly unsupported rather than arbitrary writes into a host structure.

check_sound_priority.py executes the supplied ROM's note-allocation prefix and
channel initializer for five combinations, including255+255, and then executes
the tied-note path to confirm that priority84 survives later player/track changes.
check_player_parameters.py now covers byte priority writes and truncation as well
as the two halfword controls. ROM hashes/results are recorded with those probes.

validation/sseq-priority-arm11 PASS: exhaust the six PSG-capable channels, reject
a lower-priority player, allow a higher-priority replacement, preserve tied-note
priority and verify full-sum comparison/byte storage. The shared pool/pause/clock
checks also pass. validation/sound-priority-commands accepts the one-byte priority
command, rejects a two-byte write there, and retains command playback/memory checks
with8388512 root bytes recovered. The1448-present/16-absent SSAR sweep retains all
previous PCM results (zero changed entries in player-priority-diff.json).

These tests cover command execution and channel allocation, not the original
IPC transport. Status publication/resource hooks and the remaining track/hardware
commands still precede a verified full-game boot.

### Command4 track controls

The original func0205a8f0 packs field width in argument0's top byte and the player
index in its low24 bits. Its five wrappers target track fields9 (signed external
pan,byte),a (signed external attenuation,halfword),c (signed external pitch in
1/64-semitone units,halfword),19 (modulation speed,byte) and1a (depth,byte).
ARM7 handler02385f2c applies the write to existing logical tracks selected by a
16-bit mask, ignores missing slots and discards excess high mask bits.

Sm64dsSoundCommands now decodes that packed player/width before validating the
player, and routes opcode4 to those five bounded field mappings. Track retains
separate external attenuation/pan/tune fields, initialized to0. Channel updates
combine them with sequence-driven values: attenuation adds in the existing log
volume domain, pan adds/clamps to[-128,127] before instrument/hardware panning,
and external pitch adds to the existing pitch-bend/key calculation. Updates flag
sounding channels for refresh. Modulation speed/depth also propagate immediately.
Unknown offsets/widths are rejected; no arbitrary native-layout writes occur.

check_track_parameters.py executes ROM02385f2c for seven controlled cases,
covering all five fields, byte/halfword truncation, sparse/missing slots and empty/
high-only masks. validation/sseq-track-controls-arm11 PASS tests a live PSG note:
external pan moves it from the left extreme to the right, pitch raises its sample
increment, modulation reaches the channel, and track attenuation silences/restores
it without creating a new note. Existing shared-clock/pause/priority checks pass.

validation/sound-track-commands exercises original packed command arguments,
empty/high-only masks that preserve the exact title hash b1442dcc, all-track mute/
restore, unsupported width and out-of-range player rejection. It also retains
player control/pause/replacement/drain and root-memory recovery checks. This
extends the synchronous executor only: original IPC dispatch and resource/cache
hooks are still outstanding, along with the remaining sound command opcodes.

### Command5 track mute and release-only mode

ARM7 handler02386170 releases selected track notes with override127 whenever
mode is nonzero, then stores mode&1 as the mute bit. Thus mode1 mutes future
notes, while mode2 releases existing notes but permits later ones. Track events
continue to advance in either mode; this is different from command2 player pause.
The note runner02384e00 skips allocation for muted tracks but still updates the
portamento key and note wait, including zero-duration wait-for-notes behavior.

Track::SetMute and sm64ds_mixer_track_mute now implement that distinction for
opcode5 and selected logical tracks. The event runner suppresses note allocation
while muted, retains the timing, clamps transposed keys to the native0..127 range,
and updates portamento key even when a note is muted or cannot allocate a channel.
The sequence-file eventD7 remains separately unsupported; opcode5 is the ARM9
sound command, not that event byte.

check_track_mute.py executes ARM7 command modes0/1/2/3 with selected/unselected
tracks and confirms release127/low-bit latching. It also executes six note-runner
cases for muted/unmuted normal and out-of-range transposed keys, confirming note
allocation suppression, clamped portamento keys and preserved waits. Its read
cache and note allocator are instrumented helpers, not a whole ARM7 playback run.

validation/sseq-track-mute-arm11 PASS: a looping track increments a sequence
variable while muted, produces silence, then allocates notes after unmute; mode2
releases/detaches current notes while allowing new ones. Shared clock, priorities,
external track controls and pause tests remain passing. validation/sound-track-
mute-commands PASS: mute keeps the title active/silent, unmute renders160555
nonzero samples, release-only is accepted, and later controls/replacements/drain
recover all8388512 root bytes. The complete1448-present/16-absent SSAR sweep passes
with237 silent first seconds; before/after rows are retained in track-mute-diff.json.

Remaining work still includes channel-mask command6, sequence variables/status,
remaining hardware/resource commands and actual original IPC/lifecycle hookup.
The full game has not been declared bootable or complete by these focused runs.

### Command6 track channel allocation masks

Track channel masks now constrain future note allocations in the shared hardware
pool. Opcode6 selects logical tracks and truncates the supplied channel mask to
16 bits; tracks default toffff and retain the explicit-mask flag. Instrument
constraints still apply (PCM, PSG and noise channel sets). Existing notes and
ties keep their allocated channel. Global hard/weak channel locks are not yet
implemented; the explicit-mask flag is retained for that later integration.

check_track_channel_mask.py executes ROM02386100 for four controlled mask/flag
cases (validation/sound-channel-mask-rom/report.json).
validation/sseq-channel-mask-arm11 passes empty masks, truncation, instrument
compatibility and tied-note retention. validation/sound-channel-mask-commands
passes: empty masks prohibit new notes, restoration produces159778 nonzero
samples, existing channels survive a mask write, and all8388512 root bytes are
recovered. The raw-command title hash remainsb1442dcc.

Remaining work includes sequence variables/shared status, global channel locks,
other hardware/resource commands and original IPC/lifecycle integration. These
focused tests do not establish full-game boot,60fps or complete gameplay.

### Sequence-variable scopes and stop lifetime

ROM resolver02384bd8 maps indices0..15 to the selected player's sixteen local
shorts and16..31 to sixteen global shorts shared by the sound service. The
previous imported player incorrectly held all32 independently. SoundPool now
owns the globals; Player::Variable routes FROMVAR, arithmetic and comparisons
to the correct scope. Pools initialize globals to-1; Setup resets locals only.
Stop no longer calls player ClearState: the original stop frees tracks and
clears active state without resetting variables or player parameters. Explicit
native index bounds reject malformed inputs instead of accessing out of bounds.

check_sequence_variables.py executes the ROM resolver, command7/8 halfword
writes, player setup02385b24 and stop0238592c. The stop track-release helper is
instrumented; actual parameter/status memory writes are checked. ROM setup
resets only the selected sixteen local variables and its tick counter, leaving
other players/globals intact. Results: validation/sseq-variable-rom/report.json.

validation/sseq-variables-arm11 PASS uses actual SETVAR/ADDVAR/FROMVAR events
across two players, local isolation, global boundary31, signed values, retained
state on stop, local-only reset on Setup, player destruction and independent
pools. Existing shared-clock/channel/control tests pass.
validation/sound-shared-variables-commands PASS preserves title hashb1442dcc
and recovers8388512 root bytes after replacements and release tails. Full SSAR
sweep:1448 present,16 absent,237 silent first seconds,zero errors andzero changed
PCM hashes (shared-variables-diff.json).

Command7/8 dispatch and shared-status publication are not yet connected. The
mixer currently creates/deletes Player objects per sequence, so persistent
per-slot local status after owner retirement still needs service-level storage
before implementing those commands. Full original IPC/game boot remains pending.

### Persistent mixer locals and command7/8 variable writes

Sm64dsMixer now owns sixteen arrays of sixteen local shorts, initialized to-1
and destroyed after its players/retired owners. Each sequence binds to its slot
before Setup; resource collection and release-tail owner destruction no longer
remove the slot's observable local values. Setup still resets that slot's locals.
Standalone players retain their internal storage. Globals remain in SoundPool.

Sm64dsSoundCommands implements opcode7(player,variable,value) and opcode8(global
variable,value), narrowing values to signed16 as the ARM7 dispatcher02386e74/
02386e90 does. Writes work for inactive slots too, lazily creating the mixer.
A bounded variable reader exposes the same persistent storage for later shared
status publication. Invalid indices/players reject without modifying state.

validation/sound-variable-commands PASS in Azahar exercises writes before start,
local-only reset, actual FROMVAR/ADDVAR/SETVAR event visibility, retained values
after natural END and owner collection, signed truncation, stop/collection,
restart, rejected start preserving variables, and invalid indices. Existing
audio controls/replacements still pass with title hashb1442dcc and root memory
fully restored to8388512 bytes. check_sequence_variables.py remains passing
against the ROM handlers and resolver.

Shared-status layout/tick publication and original IPC/lifecycle hookup are still
outstanding, as are remaining hardware/resource commands. This checkpoint does
not establish full-game boot or gameplay completion.

### Persistent sequencer tick counters

Player::Run now counts due sequencer ticks before executing events, matching
ARM7023859f8: drain tempoCount in units240, publish all due ticks, then execute
until the sequence ends, and finally accumulate tempo*ratio/256 for next time.
The published count includes remaining scheduled ticks when END stops execution
early. Counters wrap as32-bit values. Paused and inactive players do not advance.
Completion frees unused reserved tracks as well as the finished event tracks.
Unsigned multiplication avoids signed C++ overflow at extreme tempo/ratio values.

Mixers own persistent per-slot counters alongside local variables. Sequence
resource collection leaves the count readable; Setup resets only its own count.
The service tick reader will support the original shared-status publication.

check_sequence_ticks.py executes ROM023859f8 for six cases, including multiple
ticks, END after the first due tick, counter overflow and maximum16-bit tempo/
ratio. Track execution and stop are instrumented, while original counter and
accumulator writes run unchanged (validation/sseq-tick-rom/report.json).
validation/sseq-ticks-arm11 PASS checks cadence, ratio, pause/stop, restart, END
batching, wrap and reserved-track cleanup. validation/sound-tick-commands PASS
checks persistence after natural END/collection and reset on restart alongside
existing command/audio/lifetime checks; root free bytes recover to8388512.
Full SSAR sweep remains1448 present/16 absent/237 silent,zero errors andzero PCM
hash changes (tick-accounting-diff.json).

Original shared-status publication and IPC lifecycle integration remain pending;
full game boot, progression,60fps and stereo completion remain unproven.

### Original shared-status layout and real audio publication

Sm64dsSoundStatus represents the640-byte original ABI: completed batches at0,
active players at4, enabled channel bits at8, capture bits ata, reserved bytes
through1f, sixteen36-byte local-variable/tick records at20, global variables at
260. Compile-time size/offset assertions cover this layout. publishStatus writes
actual service state while preserving completedBatches and reserved bytes.
Publication alone must never acknowledge a command batch.

Channel enable status reads the emulated hardware enable bit, matching ARM7
02383520, rather than inferring it from allocated/releasing channel ownership.
Capture status is0 because no capture can be started: those commands still
reject as unsupported. No unsupported capture command is acknowledged.

validation/sound-shared-status PASS builds the unmodified original
src/func_0205b608.c into the focused test and temporarily binds data_020a7fc0
to the published block. It verifies the original getter against real playback,
paused and stopped/drained states; enabled channel bits; all variable/tick
fields; and preservation of a5-filled queue counter/padding. Separate event
fixtures check raw ABI offsets against known42/7/43 variables and tick1 after
natural END/resource collection. Existing controls/replacements retain title
hashb1442dcc and all8388512 root bytes are recovered.

The test restores the previous shared pointer. The full game's getter remains
outside the production slice until IPC initialization/submission/publication is
connected. publishStatus is currently an explicit synchronous operation, not
yet an automatic original-game lifecycle hook. Next integration also requires
coherent storage aliases for original0205b358's command-pool/status initialization
(the initializer references6760,7760,7f48 and64e0 as contiguous regions).
Full gameplay,60fps and stereoscopic completion remain unverified.

### Coherent original sound queue/status BSS

stage_state_bss.py now emits the original contiguous6484..7f60 audio region,
including queue counters/pointers,640-byte shared status at64e0 and256x24-byte
command pool at6760. Interior symbols7760 and7f48 now alias pool+1000 and+17e8
(the final command). The following7f60 region extends through7fc4 to include the
shared-status pointer. Scattered HAL allocations for those fields were removed.

validation/sound-original-storage PASS runs the original0205b358/0205b554
initializers twice in Azahar against poisoned native storage. It checks every
free-list link, the final null, exact final-node/status aliases,255 nodes left
after the first allocation, all16x16 locals/global variables initialized to-1,
zero ticks/masks/counters, untouched reserved bytes, and command19 carrying the
shared status address. Ready/allocation/append/submission are instrumented in
this storage fixture; it does not claim actual IPC execution or recycling.

Full smoke_player build refreshed these BSS/HAL objects and still stops at the
known unresolved0205b608 getter; no extra unresolved symbols were introduced.
The full original-game IPC integration remains pending.

### Linked sound command execution and original recycling

Sm64dsSoundQueue executes the original24-byte32-bit node ABI synchronously on
the game thread. It bounds/alignment-checks every next pointer against the caller's
node pool, rejects cycles and unsupported opcodes before processing any node,
and preserves links for the original recycler. Command19 binds the supplied
initialized status block (unexpected addresses reject); commands0..8 dispatch
to the real shared mixer. Only a fully successful list publishes state and
increments completedBatches once, with32-bit wrap. refresh publishes later audio
state without acknowledging another batch. Runtime execution failure latches
the queue so a partially executed list cannot be replayed. It is not rollback;
IPC integration must report failures rather than retry a partially applied list.

validation/sound-linked-queue PASS in Azahar calls the production original
0205b274 recycler and0205b5d4 completion getter after queue execution. It verifies
free-list append/tail, no duplicate completion, ring8->0 wrap, ordered local/global
writes,256-node batches, counter wrap, malformed pointers/cycles/unsupported
opcodes without acknowledgement, runtime failure latching and root recovery.
The initial probe link duplicated the already-imported recycler/getter; the test
was corrected to link those production objects rather than include extra copies.

validation/sound-queued-playback PASS launches/replaces the original title
sequence through lists containing command19 and command0. Playback retains the
5-second hashb1442dcc; refresh exposes active/enabled channels without incrementing
the two acknowledged batches. Existing audio/variable/status/lifetime checks
pass and recover all8388512 root bytes.

This establishes the queue executor/recycler protocol, not the original IPC
transport: initialization readiness, channel7 dispatch, automatic status refresh
and resource registration/lifecycle hooks remain to connect. Other hardware
commands still reject. Full game completion remains unverified.

### Native channel7 boundary and full player link

The3DS smoke_player target replaces generated IPCSend at the function boundary
with audio/sound_native.cpp. Channel7 now submits the full native pointer directly
to Sm64dsSoundQueue, avoiding the DS26-bit payload packing. The current linked
BSS command pool happens to lie below the26-bit limit; the adapter deliberately
does not depend on that placement. Unsupported channels/flags and command errors
fail explicitly, rather than acknowledge unimplemented work or retry partial
batches. Payload0 is a synchronous wakeup/status refresh without acknowledgement.

Native0205ae64 makes the synchronous backend available; original0205b358 and
0205b554 initialize the coherent pool/status, while original allocation, append,
0205b070 submission and0205b274 recycling remain unchanged. Original0205b608 is
now included in the production target. Full smoke_player links successfully,
with no remaining unresolved symbols. That is link closure, not full-game boot.

The backend uses static placement storage and explicit shutdown while the root
heap is alive. Registration/unregistration and manual rendering APIs are exposed
for resource/lifecycle integration. Rendering refreshes shared state; shutdown
destroys the mixer and clears the shared pointer. Original audio event callbacks
are not synthesized: their hardware commands remain unsupported.

validation/sound-native-ipc PASS runs original initialization/allocation/append/
submission/recycling through actual native IPCSend, twice with explicit shutdown.
Forty16-command variable batches cross the ring repeatedly; all256 nodes return
to the free list, wakeup preserves the completion counter, idle rendering is
silent and root memory recovers. validation/sound-production-getter uses the
production original getter in the queued title/audio regression.

Automatic resource registration, NDSP frame pumping, complete original sound
initialization and remaining hardware commands still need integration. The main
smoke_player diagnostic currently does not invoke the complete sound boot chain.
The full game,60fps target and stereoscopic completion remain unverified.

### Main3DSX packaging and resumed Stage construction

Full ELF link closure exposed a3dsxtool packaging failure: absolute overlay_60,
overlay_98 and overlay_102 symbols were referenced as addresses, yielding invalid
relocations to3c/62/66. These DS linker symbols are integer overlay IDs. hostgen
now lowers `(int)&overlay_N` to the numeric ID, and the original object-overlay
loader is compiled through that generator (the raw source remains unchanged).
The full smoke_player_3dsx target now builds successfully.

An optional NATIVE_OVERLAY_TRACE prints load/unload IDs and native return
addresses for symbolication. PLAYER_STAGE_CONSTRUCT previously left destination
data_02092110 at its initial-1 sentinel, indexing before the level overlay table
and requesting overlay0. The direct castle diagnostic now explicitly requests
sublevel1, as its intended destination; normal menu/boot destination selection
still remains to integrate.

validation/stage-native-audio-link and stage-overlay-origin retain the initial
overlay0 failures. validation/stage-castle-destination now enters original Stage
construction, successfully loads the castle overlay9, then fails explicitly on
unsupported object overlay62. The call originates in generated
LoadOrUnloadObjectOverlays, whose ROM castle row is[1,1,0,2,1,1,1]. That row requires
object overlays62,69,85,89,94,100 plus the fixed98/102 loaders. Overlay62 includes
daHolhei_c. This is an actual remaining implementation requirement, not a loader
error to suppress. The diagnostic also preloads98 through its actor-profile
helper; proper Stage-owned overlay lifecycle will need to replace that bootstrap
before completing the original load chain. No full Stage success is claimed.

### Overlay62 native compilation preparation

The explicit EXCLUDE_FROM_ALL target sm64ds_overlay62 now compiles five actor
translation units (Holhei, normal/small Koopa, Koopa the Quick, race flag and
Jango), their five original static initializers, and generated native data.
It does not yet link into smoke_player or advertise loader support.

GCC-specific generator fixes preserve the source's intent without modifying src:
Koopa's anonymous Vec3 typedef now has the tag used later by that TU; Jango's
`#pragma cplusplus off/on` region becomes an extern-C block so its state function
and two C ABI calls retain their actual linker names instead of being mangled
again. Remaining legacy void*/typed-pointer C linkage warnings still require
ABI review during runtime integration.

Decoder-checked overlay62_native.py emits960 bytes of contiguous BSS with original
interior labels, six28-byte profiles checked word-for-word against the ROM, and
36 nonvirtual native member-function pointers checked for valid overlay targets
and zero adjustment. The five initializers correspond to the original ROM
constructor table at211d8cc..211d8e4. Build sm64ds_overlay62 succeeds (11 objects).

check_overlay62_dependencies.py records the current object-level inventory in
validation/overlay62-compile/dependencies.json. Outstanding game dependencies
include BlendModelAnim, blue-coin resources, model material visibility, second
music-layer controls, actor/player/cap/particle helpers and four initialized
overlay data islands ced8..cf30. SDK symbols and data020a0eac in that inventory
are resolved separately by existing libraries/linker aliases. This is not link
closure or runtime validation. The loader correctly continues to reject62 until
its data/dependencies/lifecycle and actor behavior are actually integrated.

### Overlay62 dependency closure and first runtime initialization

The overlay62 object target now includes the twenty missing game helpers, all
five BlendModelAnim virtual methods and their five buffer/blending dependencies
from the original sources. Four Koopa data islands at211ced8..211cf30 are emitted
with thirteen native pointers checked against both ROM words and relocation
metadata; numeric animation flags retain their original bytes.

The focused probe linker accepts --object-target sm64ds_overlay62 to include
these compiled objects without advertising incomplete loader support. The
resulting3DSX links normally with all six profiles retained. Current dependency
inventory:41 objects,320 global definitions; remaining inventory entries resolve
through SDK libraries or the existing data020a0eac linker alias. No unresolved
function stubs were introduced.

validation/overlay62-initializers PASS executes all five original initializers
twice in Azahar. validation/overlay62-initializer-recovery repeats with assertions
for exactly33 resource destructors per cycle, no destructors on a second drain,
and full root-memory recovery. It also checks960-byte BSS extent, nonnull factory
pointers and the original blend-buffer sizing helper (7 bones ->336 bytes).

This validates initialization/destruction and link closure only. Actor factories,
resource loads, animation blending and per-frame behavior have not yet been
exercised by this probe. Lifecycle/profile registration into the native loader
and actual actor execution remain before enabling overlay62 for Stage.


### Overlay62 actor resource lifecycles on ARM11

`port/tests/smoke_overlay62_actors.cpp` links through the focused probe helper
with `--object-target sm64ds_overlay62`. Evidence:
`build3ds/validation/overlay62-all-actor-lifecycle` PASS in Azahar. The test runs
all six native factories twice: race flag, Chuckya, normal and small Koopa
(each with both model variants), Koopa the Quick and Klepto. It loads their
original models/animations and releases them through original cleanup methods
and destructors. Each overlay cycle drains exactly 33 static resource callbacks.

The flag executes 120 actual Behavior calls per cycle, checks disarmed/timer
state and exact model translation including the original +60-world-unit offset.
Chuckya, the racer and Klepto each advance animation 120 times. Klepto exercises
BlendModelAnim allocation and advancement. A documented synthetic four-node
path feeds the original PathPtr functions; exact first-node coordinates are
checked for the racer and Klepto. The racer registers and explicitly untracks
its star marker (castle grounds is outside the main-course filtering rule).

Root free space is 8388512 before actor tests, 8388296 after either cycle,
and 8388512 after original ShadowModel::CleanAll/CleanCommonModelDataArr. The
216 bytes are the shared cylinder-shadow model cache, reused across cycles,
not an accumulating actor leak. Earlier strict per-overlay-memory and race
marker assumptions failed; their diagnostic logs remain in validation directories.

Limits: this is not a gameplay or visual rendering test. Enemy/race/bird full
Behavior loops, star/cap interactions, native overlay62 loader ownership and
profile registration remain pending. The loader still rejects overlay62;
production Stage has not advanced past that point. Full-game 60fps/stereo and
completion remain unproven. No original src/ or include/ files changed here.


### Native overlay62 loader integration and empty retail placeholders

Overlay62 is now linked into smoke_player and exposed as lifecycle kind5.
`unmatched/overlay62.cpp` zeros its 960-byte BSS, runs the five original sinits
under resource scope62, installs six profiles (190,203,204,188,205,239), and
restores previous profiles while draining overlay-owned destructors on unload.
The profile generator verifies these indices against ACTOR_SPAWN_TABLE in the
local decompressed ARM9 (load base02004000), in addition to profile/relocation
checks. The focused probe helper deduplicates explicitly requested object-target
objects already present in the main link command.

`validation/overlay62-native-reload` PASS: three load/unload cycles, 33 callback
registrations per cycle, duplicate loads rejected, profiles restored, destructor
head restored, BSS poisoned between cycles then restored byte-for-byte.
`validation/overlay62-actors-native-loader` PASS: previous actor-resource probe
now uses actual LoadOverlay/UnloadOverlay; no callbacks remain after unload and
scene cleanup fully recovers the root heap.

The ROM's overlay69 is a placeholder consisting solely of 32 zero bytes, with
no BSS, constructors, symbols or relocations. The registry now recognizes only
this exact checked shape as kind6, preserving normal residency/overlap/unload
checks. Thirteen such placeholders are present:
61,67,68,69,76,82,83,86,87,88,93,97,101.
`validation/native-empty-overlays` PASS covers each twice plus the existing
18 data/castle overlay cycles; unsupported nonempty overlays still reject.
Main smoke_player_3dsx builds successfully; diff --check passes.

`validation/stage-overlay69-integrated` is an expected FAIL documenting current
progress: original Stage factory loads9,62,69,85 then rejects89. Next: integrate
overlay89 (daObjKey_c OBJ_KEY/LAST_STAR, initializer021328d4), resolving overlap
with existing tools/key_resources.py bootstrap storage. Overlays94,100,102 also
remain. Stage diagnostic still preloads98 through initialize_castle_actor_profiles;
that must yield ownership to Stage before its later fixed98 load. Actor full
behavior/rendering and complete gameplay/60fps/stereo remain unverified.


### Overlay89 and common star initialization integrated

Native kind7 now loads overlay89's original initializer021328d4, unified256-byte
BSS, eight resource callbacks, eight native state PMFs and profiles282/283
(OBJ_KEY/LAST_STAR). tools/overlay89_native.py validates ROM profile words and
spawn-table indices, PMF targets/adjustments and the final-star offset vector.
Existing key_resources.py shares the unified handles instead of defining duplicate
storage. Its partial fixture initializer is skipped in PLAYER_STAGE_CONSTRUCT.
Stage no longer calls initialize_castle_actor_profiles, which preloaded98 and
partially initialized100/102 before Stage could own those overlays.

Linking the complete key actor retained two missing original helpers:
Event::ClearBit and Sound::LoadAndSetMusic_Layer3. The original key actor's landing
flag data_ov055_02111b68 is defined beside existing055 flags in player_globals;
these still need relocation into overlay55's lifecycle when that unsupported
level is integrated. This storage is not a validation of final-star progression.

Evidence: validation/overlay89-initializers PASS twice with eight destructors
and full heap recovery. validation/overlay89-native-reload PASS three cycles,
profile restoration, poisoned-BSS recovery and no pending callbacks.
validation/overlay89-actor-common-star-resources PASS: actual native loader,
both factories/all eight kinds, 120 animation advances each, two cycles,
8388512 root bytes restored after scene cleanup. Full key/star Behavior, collision,
collection, sound playback, rendering and progression are not covered here.

The actor probe initially failed because common star animation801b was not
initialized. Original __sinit_ov002_02106e40 is now included and called by main;
its14 state PMFs are ROM-relocated by tools/star_states.py, which also supplies
numeric data0210aa0c and02082714 (rejecting relocations in the latter range).
Original missing DeathTable_ClearBit, ClearJustHitGroundFlag, func0203d024,
FarthestPlayer, NumCoins and NumVsStarsObtained are linked. No stubs added.

hostgen's scoped daStar_c rewrite converts two fictitious embedded SubV5 virtual
calls into dispatch through the actual model addresses (+30c/+370). GCC otherwise
devirtualized them to nonexistent SubV5::m5. ARM11 disassembly confirms vtable
slot+14 and blx r3 for both paths. Original src/include remain untouched; full
star rendering still needs runtime visual verification. build_player_probe.py
now removes previous probe artifacts before rebuilding to prevent stale execution
following compile/link failure (and forbids using it for production smoke_player).

Current main smoke_player_3dsx builds; diff --check passes.
validation/stage-overlay89-common-star is expected FAIL: original Stage loads
9,62,69,85,89 successfully, then rejects94. Next overlay94 is daOwl_c (OWL),
initializer021367e8;100 and102 still remain before full castle boot. The global
overlay2 startup is still partial, and replacing the fixture bootstrap means its
remaining common profiles/initializers need boot-owned integration. Full-game
60fps, stereo and completion remain unverified.


### Overlay94 owl lifecycle integrated

`tools/overlay94_native.py` supplies the contiguous160-byte BSS, ten original
native state PMFs, ROM-checked offset vector (0,-131072,0), and28-byte OWL
profile234 checked against both the overlay bytes and ARM9 spawn table. The
original daOwl_c TU is host-generated without source edits. Four missing original
helpers are linked: func_ov002_020df840,020df7ac,020df7f4 and
 dBgCh_Actr::Unk_0203589c. These preserve the player/owl interaction paths; no
replacement behavior or unresolved-symbol stubs were introduced.

`unmatched/overlay94.cpp` and native kind8 reset BSS, execute original initializer
021367e8 with resource ownership94, register/restore the actor profile and drain
its four callbacks on unload. Registry generation verifies the ROM's base,
BSS size and exact constructor list before exposing this support.

Evidence in build3ds/validation:
- overlay94-initializers PASS: two constructor/destructor cycles, four callbacks,
  zero on second drain and full heap recovery.
- overlay94-actor-resources PASS: two original factory/resource/cleanup cycles,
  120 dormant Behavior calls plus120 direct animation advances each, stable
  position/state without a player;8388512 root bytes recovered after scene cache
  cleanup. Original actor size1016 bytes.
- overlay94-native-reload PASS: three cycles, profile restoration, all160 BSS bytes
  restored after poisoning between loads, duplicate-load/unload checks.
- overlay94-actor-native-loader PASS: same actor test through actual loader.
- stage-overlay94-integrated expected FAIL: Stage now loads9,62,69,85,89,94 and
  stops at unsupported100. Main smoke_player_3dsx and diff --check pass.

Limitations: no visual render, approach/talk, carrying Mario, flight controls,
release, or special mission suppression has been verified for the owl yet.
Next main boot dependency is overlay100: seven profiles BUTTERFLY,IRONBALL,
WANWAN2,DOOR,STAR_GATE,FISH,PATH_LIFT and seven initializers (including the unusual
__sinit_d_a_star_gate.cpp symbol). Some actor/fixture resources already exist;
merge their storage into a coherent lifecycle instead of duplicating handles.
Overlay102 and remaining common bootstrap still follow. Full-game60fps/stereo
and completion remain unproven. All emulators were stopped after each test.


### Overlay100 compilation and star-gate lifecycle preparation

Explicit object target sm64ds_overlay100 now builds15 objects/129 global
symbols: missing IronBall, unchained Chomp (factory src/d_a_wanwan2.cpp AND
implementation src/actors/daWanwan2_c.cpp), star gate, path lift plus the path-lift
base, three original initializers and six CommonModel methods. Actor source
resolution must retain both Chomp TUs; the factory alone has no vtable definition.
The target remains excluded from production and native loader still rejects100.

The original star-gate TU uses a dynamic global model-handle constructor with
undefined wrapper constructor/destructor symbols under GCC. hostgen now performs
a guarded rewrite for this TU: POD handle storage, zero-initialized states and
explicit sm64ds_init_star_gate(). That function initializes handle0x4a1 and registers
its real destructor through func020731dc under the caller's overlay scope, then
restores all five state records. No implicit CRT resource initializer remains.
The original source/header files are unchanged. These separate storage definitions
must join the unified overlay100 BSS before enabling its lifecycle.

validation/star-gate-explicit-lifecycle PASS in Azahar: no model initialization
before the explicit call; three cycles, exactly one callback and no second-drain
callbacks; all five state pairs restored after poisoning. Each cycle constructs
all four door variants through original factory/InitResources, verifies parameter
normalization/state/model translation, releases resources and deletes the actor.
Common-model scene cache is cleaned and root heap returns to baseline each cycle.
The model handle1185 maps to catalog file755 (an initial probe incorrectly compared
the mapped handle with1185; corrected using handles.tsv). ROM inspection confirms
STAR_GATE profile354 and thresholds12/30/50/80 and associated save/message fields
match the source. Opening/unlocking/dialogue/rendering have not been verified.

build3ds/check_overlay100_dependencies.py records the current object-level
inventory in validation/overlay100-compile/dependencies.json. This is not a full
link/runtime proof: remaining items include rotation/moving-background/dust
helpers, cxa_vec_ctor ABI, path-lift shared tables/init and overlay100's missing
BSS/state/vector islands. Existing butterfly_data.cpp, door_data.py and fish_pmfs
own fragmented portions; consolidate them without duplicate resource ownership.
Star-gate POD globals/state definitions currently remain in its generated TU;
its explicit initializer must be preserved when moving those into unified BSS.
Main Stage still stops at100;102 and common boot follow. Goal remains incomplete.


### Overlay100 unified storage and collision dependency work

`tools/overlay100_storage.py` now owns the entire1152-byte BSS span
02148600..02148a80, with original interior resource/state labels and explicit
star-gate destructor record at0214893c. Butterfly, door and fish generators no
longer allocate overlapping separate storage. The guarded star-gate hostgen
rewrite now uses external model/vector/state storage and the unified callback
record while retaining explicit sm64ds_init_star_gate(). Original sources remain
untouched. Production smoke_player_3dsx builds with this storage consolidation.

Added original overlay100 dependencies: Matrix4x3_FromRotationXYZExt,
dBgW::UpdatePosAndAngs, dActor_c::HugeLandingDust, func020393c4/393d4/3d73c and
func_ov002_020ad660. func0203d73c is an original empty destructor, not a port stub.
HugeLandingDustAt was already linked in the main and is not duplicated.
The focused probe helper now reads the actual CMake build.make object list;
stale .o files left by removed sources are no longer included by recursive scan.
The overlay100 dependency inventory uses the same manifest-based enumeration.

validation/overlay100-unified-storage PASS: three cycles of the original
butterfly,door,fish initializers plus explicit star-gate initializer, exactly21
resource destructors each. Every BSS byte matches the first initialized snapshot
after poison/reset/reload. All four star-gate resource variants still construct,
load and clean up each cycle, restoring root heap exactly after cache cleanup.
No stale callbacks remain. diff --check passes, emulators stopped between runs.

The explicit overlay100 object target currently has22 objects/129 definitions.
Remaining game dependencies in validation/overlay100-compile/dependencies.json:
dBgW::UpdatePosWithTransform/UpdateAngsWithAngularVelY, LandingDustAt,
path-lift tables0210af00/af2c/af70 and CLPS0210d7d4, IronBall five PMFs,
Chomp two PMFs and vector02148008. SDK aliases/runtime symbols are separate.
Path-lift's original initializer is src/__sinit_ov002_021071f4.c; af70 aliases its
native base vtable (factory immediately installs derived vtable afterward).
Still need the remaining initializers/profiles, full actor execution and native
loader registration100. Loader intentionally continues rejecting100; Stage/full
completion,60fps and stereo are not yet established.


### Overlay100 native loader integration

Added original moving-background UpdatePosWithTransform and
UpdateAngsWithAngularVelY plus LandingDustAt. tools/overlay100_dependencies.py
relocates seven IronBall/Chomp PMFs, six path-lift PMFs, the three path state
records including their original names, two checked vectors and the path-lift
CLPS block. Original __sinit_ov002_021071f4 is linked and invoked in common boot.
The path-lift factory's temporary base vptr now uses the GCC native vtable
address point (+8), replacing the raw ROM-data symbol in hostgen.

A full-factory probe found Chomp deletion leaked1972 bytes: its legacy factory
also installed the table start instead of the GCC address point. The guarded
hostgen correction for src/d_a_wanwan2.cpp adds8 to its vptr. This is verified
by exact root-memory recovery after all three factories (IronBall,Chomp,path
lift). Both their construction and destruction execute real native code,
including __cxa_vec_ctor for embedded model/shadow arrays. Earlier failing logs
remain under overlay100-all-initializers / overlay100-factory-memory-diagnostic.

validation/overlay100-native-factories PASS: three cycles of all seven overlay
initializers,28 resource destructors, all1152 bytes restored after poisoning,
all five star-gate states checked, all four star-gate resource variants tested,
and the other three previously missing factories created/deleted with no heap
loss. Current object inventory27 objects/153 definitions; only SDK/C++runtime
and the existing data020a0eac alias remain outside player objects. Full ELF and
3DSX link successfully; no stub functions added.

Native lifecycle kind9 now owns overlay100, with exact ROM constructor-list
verification. tools/overlay100_profiles.py checks all seven ROM profiles and
registry indices, emitting six (STAR_GATE is already supplied by its class TU).
Profile IDs336,220,337,353,354,344,31 install on load and restore on unload.
Original initializer order is retained, using explicit sm64ds_init_star_gate
for the former compiler-generated constructor02147a70. Native storage zeroes
before initialization. validation/overlay100-native-reload PASS: three cycles,
28 callback registrations each, profile restoration and complete BSS recovery.

validation/stage-overlay100-integrated is expected FAIL documenting next step:
Stage loads9,62,69,85,89,94,100,98 then rejects102. The earlier98 preload conflict
is gone. Next overlay102 has14 profiles across simple backgrounds, six hatena/
cap/item block variants, Bob-omb and shell; four sinits0214d714/d908/de70/dfac,
BSS960 bytes. Existing partial fixture resources must be consolidated.

Limitations: Chomp/path-lift/IronBall full resource and Behavior loops, doors
opening, actor interactions and full castle progression are still unverified.
Link/lifecycle tests are not full game validation. Main complete boot,60fps,
fullscreen stereo and completion remain outstanding. Diff --check passed;
emulators terminated between all tests.


### Overlay102 initializers and shell resources

Added explicit sm64ds_overlay102 OBJECT target for original SimpleBg and Shell
actors via hostgen, and original sinits0214d714/dfac. Hatena and Bob-omb TUs and
their sinits already belong to the player target. New tools/overlay102_storage.py
unifies all960 BSS bytes0214e6e0..0214eaa0; removed the partial Hatena PMF BSS
allocation. The eight shell PMFs are relocated by actor_pmfs.py. Shell model
handle pair0214d70c is checked against ROM and points to common native handles
0210da18/da20. Production smoke_player_3dsx builds successfully.

validation/overlay102-initializers PASS: all four original sinits in original
order, three cycles, all960 bytes identical after poison/reset, four shell state
records checked,26 destructor callbacks per cycle, no stale callbacks or heap
loss. Extended probe validation/overlay102-shell-resources PASS adds all four
shell param combinations (two models, bit4 holdable variant) on every cycle.
Actual factory/InitResources/CleanupResources/delete execute; initial idle state,
spawn position, model/variant selection and Yoshi-eat result checked. Original
scene cache cleanup returns root free memory exactly8388512 each cycle.
No shell Behavior/riding/sliding/render or player interaction claim follows.

Overlay102 is NOT registered in native loader yet. Next: SimpleBg's interleaved
six model/collision/CLPS table0214e188..0214e1d0,14 profiles and loader lifecycle.
Its CLPS pointers02113ccc/3cec/36f4/2c18/2c58 are ambiguous across level overlays,
per ov102/relocs.txt; do not hardwire them all to castle9. Common CLPS0210d814
is in overlay2 (header CLPS,8,1); model/collision handles are own BSS. Use the
active level and a checked native numeric-data address resolver (all_level_data
already emits per-overlay numeric spans/reset ranges) at actual variant access.
Current check_overlay102_dependencies.py inventories the remaining table symbols
against production objects. All emulators stopped between tests.


### Overlay102 loader integration and level-aware SimpleBg collision

Overlay102 is now native lifecycle kind10, with ROM-checked constructors/base/
BSS, all14 profiles installed/restored and complete960-byte reset. Generated
profiles are checked against ROM and ARM9 registry; BOMBHEI remains defined by
its original class TU. Added the three original dependencies FindEgg,
func02010304 and Player::IsInsideOfCannon exposed by retaining Bob-omb's profile.
Full production ELF/3DSX builds and links. Native-overlay unsupported test now
uses55 (103 does not exist;102 is supported).

SimpleBg resource table's two handle columns now point to native BSS. Its CLPS
column is resolved at actual InitResources use via guarded hostgen substitution
(two call sites); no fixed assumption of castle9 for all six variants. New
all_level_data address range table preserves overlay identity and bounded size
lookup. Current loaded level comes from native overlay residency. Runtime CLPS
checks magic,stride8,count and full range. Common variant3 uses checked overlay2
CLPS0210d814. ROM inspection identifies actual CLPS for variants0/1 in overlay16,
variant2 in15, variants4/5 in9. Levels15/16 themselves remain unsupported actor
lifecycles; their numeric data is already imported. New resolver does not imply
these levels run. Castle BSS has an explicit address range alongside its data.

validation/overlay102-native-reload-checked-ranges PASS: three real loader cycles,
26 registrations,14 profiles restored,960 bytes poison/reset; duplicate load/
unload handling. Checks independent identity at common original address02113550,
bounds rejection, five complete CLPS imports from levels9/15/16, and active
castle CLPS pointer identity. Earlier overlay102-native-reload FAIL code5 was a
probe error: test address02112c18 is not imported in16; fixed to an actual shared
numeric range without widening imported data.

validation/overlay102-castle-scenery-resources PASS: three cycles of all four
initializers, four shell variants plus actual DOKAN/MC_DODAI/MC_HAZAD factories,
InitResources/CleanupResources/delete. Exact heap8388512 recovery after scene
cache cleanup each cycle. No Behavior/render/interaction claim.

validation/stage-overlay102-integrated TIMEOUT35s: stage passes prior missing
102 load and continues, but cannot finish construction. Saved first30 Azahar
memory errors in first-memory-errors.txt. Current ELF PC12dc2c is func0201b6f8
line29 scanning message data via data0209d6f4, evidently invalid/null (unmapped
Read8 walking low addresses). Earlier errors: PC12ac84 func0203d974 reads DS
system field027ffc40; PC107298 NestedHeapIterator::Previous reads8; PC12e914
func02051244 reads0; PC10db6c SolidHeapAllocator::ResetStart reads18. Next work
must restore correct common boot/heap/text initialization, not suppress those
errors. Last stage caller17549c maps InitResources after LoadLevelOverlays.
No complete Stage/gameplay/60fps/stereo claim. Emulators terminated after runs.


### Boot message bank restored; sound boot dependency audit

The preceding Stage timeout had two independent missing boot steps. First,
func0203d974 read027ffc40 directly; guarded hostgen now translates this DS RAM
mirror through ntr::xlate and production removes the raw TU. Second, the direct
Stage probe omitted LoadMessageBankForLanguage, normally called by original
BootScene::Behavior after its readiness gate. Linked original engine/message/
func0201fe08 and func0201cb2c, called the former before direct Stage entry.
No source/header changes and no synthetic message strings.

validation/message-boot-languages PASS in Azahar: boot-mode mirror cases0/1/2
and data020a0f10 fallback, all five original language banks, actual parser pointer
layout/header checks, bounds/terminators and original width scanner for control
labels640..642 (15 total), exact heap recovery after each bank is freed.
Production ELF/3DSX builds successfully.

Sound's group loader receives null data0209b498 because original boot func020133bc
has not run. Attempting to link/call full original boot exposed12 dependencies,
including SDAT path0208e498, archive0209b4b4 and stream buffer0209baa0. The original
function is now retained as EXCLUDE_FROM_ALL object target sm64ds_sound_boot
(compiles), not called yet. Inventory: validation/sound_boot-compile/dependencies.json
via check_sound_boot_dependencies.py; failed exploratory link in
stage-sound-boot-build.log. Original func020506fc starts an infinite SDK worker
func02050038 on queue020a5600; func02049cd8 configures its two-channel stream
through507e0/503a4. Needs native worker/stream and SDAT resource-registration hooks,
not a dummy sound-disable gate. Current Stage still exposes these null-heap errors.

validation/stage-message-bank-initialized TIMEOUT35s, but no longer loops scanning
null text. Full Azahar log saved alongside result. It now reaches
Stage::LoadClsnAndObjects -> LoadEntranceObjects -> player Spawn/initialization.
Terminal crash is Undefined Instruction PC0, LR110984 (fBase_c::Process after
hook), R0=095a36b8, R3=00205d00 (native Player table). Caller stack154754 entrance,
154b00 LoadObjects,154b5c LoadClsnAndObjects,17562c Stage InitResources. Current ELF
native Player table at205d00 has valid Init/Before/After words; init-dispatch PMFs
20112c/34/3c are {8,1},{4,1},{0,1}. Need investigate callback/stack corruption or
ABI while spawning player; do not treat timeout as successful Stage creation.
Earlier heap errors persist (1072a0 NestedHeapIterator,12e8e8 sound heap reset).
All emulators stopped after runs. Full game, sound integration, performance and
stereo requirements remain unverified.


### Actor registry pointer/array corruption fixed

Process trace established a precise failure: Stage entered with after-PMF{8,1},
but Player entered with {0x207568,1}. The latter is g_profile_BK_TOWER. Symbol
addresses prove data020a4bb8 at201068 +49*4 equals data02099e94 at20112c.
Seven native loaders wrongly declared the pointer global as void* array[]; their
profile stores overwrote adjacent native data instead of the 512-entry registry.
The reload probes repeated the same wrong declaration, so their earlier profile
installation claims were invalid. Earlier resource/destructor results do not
by themselves establish correct registry installation.

New unmatched/actor_registry.h declares the actual void** plus backing storage.
All seven loaders62/85/89/94/98/100/102 and their reload tests use it; main/actor
probes and registry definition also share it. PLAYER_PROCESS_TRACE guarded
hostgen diagnostic reports before/after member pointers and initial vtable slots.
No src/include changes. Production build and diff --check pass.

validation/actor-registry-pointer-correction PASS: three cycles of all seven
actor overlays simultaneously with castle9, independent reads of actual512-slot
backing storage, checks every installed profile ID/factory, all unmodified slots,
pointer identity, three init-dispatch PMFs unchanged, full restoration on unload.
This supersedes previous reload-profile evidence affected by the bad declaration.
Updated102 reload probe also uses backing storage and preserves after-PMF bytes.

validation/stage-registry-corrected now completes Player Process correctly and
fails at missing CAMERA332 in the actual registry (PC0 LR10dee8 R5=14c). Added
shared initialize_camera_profile to direct Stage boot, reusing fixture setup;
verified ARM9 slot332 points02086d78, bytes44e400024c010000 (factory0200e444,
behavior332/render0). validation/stage-camera-profile proceeds through Player,
Camera and three further actors with intact PMFs, then fails spawning ID342
(MC_FLAG, R5=156, PC0 LR10def0). Thus next work is castle9 profile installation
and remaining common boot profiles. Castle9 currently owns resources/BSS only;
legacy initialize_castle_actor_profiles registered its profiles only for fixture.
Azahar full logs saved for both runs. Sound heap boot errors still remain; none
of these Stage timeout runs establish complete scene/gameplay. Emulators stopped.


### Castle9 profiles and common scenery startup

Added tools/castle_profiles.py: all four28-byte profiles SBIRD343,MC_WATER338,
MC_METALNET339,MC_FLAG342 checked against local ov9 ROM tuples and ARM9 spawn
registry. Existing original factories are used. Castle native lifecycle installs
after initializers and restores previous profiles on unload via typed registry.
validation/castle-actor-registry PASS three cycles of castle plus seven actor
overlays with independent backing-array checks and PMF corruption guards.

validation/stage-castle-profiles progressed through four flag actors, then failed
at null common TREE286 profile (PC0, LR10def0, R5=11e). Added ROM-checked common
profiles TREE286,CH_SCENE349,SET_SE350 in tools/common_scenery_profiles.py; explicit
sm64ds_init_common_scenery_profiles called at direct Stage boot alongside Player
and Camera. These persist in common overlay scope, not castle unload scope.
No duplicate resource initialization added. Existing actor implementations used.

validation/common-and-castle-registry PASS three full cycles; verifies the three
common profile addresses in actual backing registry before snapshot and ensures
they survive castle/actor overlay reloads unchanged. Production build succeeds.
validation/stage-common-scenery-profiles TIMEOUT20s now gets through numerous
trees/transitions/emitter actors and reaches missing MAP335 (g_profile_MAP at
ov2:0210c160; PC0 LR10def8 R5=14f). Full emulator log saved. Next: native map actor
profile/factory/resources, then remaining Stage boot/sound integration. Prior
sound heap errors remain; resource/Process traces do not prove functioning sound,
rendering, gameplay loop or full-game completion. Emulators terminated after runs.
