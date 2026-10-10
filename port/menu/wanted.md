# Native mini-game closure

The target imports the complete overlay 4 base and the complete overlay 6
source/data/constructor graph. Runtime admission remains explicit: Wanted (366),
Loves Me (390), Memory Match (362), Mario's Slides (371), Bounce and Pounce (372),
Pair-a-Gone (381), Snowball Slalom (377), Bob-omb Squad (368), and Picture Poker
(379). The other 21 retail profiles remain rejected; compiling their source does
not establish gameplay or save support.

Original scene destruction precedes unload. The wrapper loads 4 before 6 and
unloads 6 before 4, retaining the original overlap rules. All 31 overlay 6
constructors execute in ROM order. Storage keeps every original overlay 6 data
and BSS interval contiguous, restores initial data, and zeros BSS on each load.
Lazy destructor ownership follows these retained spans.

`prepare_overlay6_native.py` audits original roots, relocations, BSS initializer
provenance, OAM consumer extents, and constructor/admission identity. It compiles
the two source owners with duplicate initialized data and proves all 21 native
initializers against retail before replacing those definitions with typed extern
declarations. Four reviewed RTTI records retain their complete original intervals,
including a name interval's trailing vtable prefix, with explicit native ABI vptr
translation. `verify_overlay6_native.py` is a mandatory post-link gate for native
vtable prefixes/RTTI, address-point aliases, retained metadata, canonical heap
ownership, and absence of strong undefined symbols. Dynamic RTTI behavior for
unadmitted classes remains unvalidated. No original DS source is changed by this
import pipeline.

## Global overlay 6 and Picture Poker integration

`validation/owned-native-presentation/global-ov6-common-integrated-v50` links the
common product graph with diagnostic main/entry only. Six isolated runs cover
Poker win/Quit/fresh-process reload, Bob-omb score2200/game over/Quit/reload, and
Flower entry/Quit/reload. All pass with zero unmapped accesses; all 72 raw LCD
captures and the three complete 8192-byte saves match the respective validated
private V46/V42 references. The final build's 3523 inputs, ELF and 3DSX remain
byte-identical after adding the automatic post-link gate. These are regression
comparisons against prior native runs, not complete DS pixel parity.

Picture Poker uses original actor379 and card/state/RNG code. The controller
only touches visible cards and HOLD, then uses A/Quit; it never injects a board,
actor, score or RNG state. Both original DS and native runs earn score1 and retain
it after a fresh process. Their deals differ, so the resulting winnings and cart
bytes differ between DS and native. Save fields/checksums/mirrors are validated
independently. Access in these tests uses a dedicated synthetic fixture that
changes only the Poker unlock bit and corresponding checksums; it is not earned
rabbit progression and is not a distributed save. Natural rabbit unlock and
exact DS/native RNG/deal parity remain open.

The sections below retain earlier, bounded per-game evidence and its limits.

## Wanted evidence

Paths below are relative to build3ds/validation. wanted-native-lifecycle covers
menu → Rec Room → Wanted, an earned point through touch, pause → Quit → Rec Room,
with production profiles/lifecycle and zero unmapped accesses. wanted-native-reload
adds two complete visits from the one-point save. Current score resets each time;
three tied one-point records occupy three ranking slots, matching all 8192 bytes
of the actual DS cart. wanted-windows validates the rectangular/OBJ window fix.
The iris HBlank animation still needs per-scanline register timing; a matching
static frame does not prove that timing.

## Loves Me evidence

flower-first validates the initial played flag and checksums against all 8192 DS
save bytes. No ranking record is added. Its early captures were taken before
Render, so do not use them as visual evidence. flower-reload-owner performs two
complete visits, results and Quit transitions with zero unmapped accesses and a
DS-identical cart, after correcting the lazy ov6 destructor lifetime.

flower-rules-rounds covers A → instructions/pause → B → play, five complete rounds
through Play Again, and Quit. Both rules screens match DS exactly: 98304 pixels.
The rounds include LOVES ME, LOVES ME NOT and REALLY LOVES ME after three wins;
cumulative native scores are1,1,2,3,6. Play Again retains the session score. The
final 8192-byte cart equals the DS after multiple rounds and Quit. These are normal
input-driven journeys, with no RNG or game-state injection. RNG differs from DS,
so their animated gameplay captures are not frame-aligned pixel comparisons.

## Petal geometry corrections and aligned oracle

The large transient gray triangle was a real math defect. Two SQRT writes in
func020531a4 escaped hostgen because they indexed a pointer. The HAL also omitted
recalculation on the high 32-bit SQRT parameter write. Retail writes low=0 then
high=n; both writes restart SQRT, as corroborated by melonDS. The menu adapter and
HAL now preserve those effects. flower-math-after tests split-word writes,32-bit
mode, four fixed-point roots, a 90-degree quaternion and quaternion normalization.
Before correction quaternion components overflowed into billions.

Retail020c35e8 also sign-extends a trigonometric index and reads up to 8192 bytes
before data02082214. The isolated native table lacked those neighboring bytes.
romdata.py now keeps the original prefix and exports the table symbol at +8192.
The complete 24576-byte window is byte-identical to ROM in the ELF; the prefix
also matches DS RAM in the captured scene. This preserves the retail behavior
instead of substituting wrapped angles.

flower-petal-ds-replay-seven replays seven physical states captured from the DS,
including the last live update and expiry. Original update/matrix functions
produce zero differing physics, quaternion, matrix or lifetime bytes. This is
an explicitly controlled component oracle, not a normal gameplay journey.
The separate flower-petal-render probe retains native model pointers/assets and
injects only the captured physical state to compare an isolated rendered petal;
its method.json documents the distinction.

## Remaining bounded checks

flower-loss-streak completes 12 native rounds and reaches REALLY LOVES ME... NOT!
after three losses, then Quits with zero unmapped accesses and all 8192 cart bytes
identical to DS. All four ordinary/streak result labels are now observed natively.
The controlled petal image comparison is reported below. DS-only reference rounds 21/22
show the special one/two-petal round followed by automatic stripping, then the
counter resets; that later-cycle behavior is not yet natively validated. Other
mini-games remain outside the currently validated slice.

### Flower petal directional lighting

The controlled petal replay now passes the original physics check and renders
without invalid memory accesses after correcting LIGHT_VECTOR/NORMAL fixed-point
lighting. Independent host coverage:135 cases, also ASan/UBSan, with12 shadow and
2444 texture-cache regression cases passing. The DS screenshot has a two-image
capture delay; at aligned state the bounds agree. Native gray lighting reduces
the crop RGB error5448→1653, but32 pixels still differ, including five coverage
pixels. See `build3ds/validation/flower-petal-lighting` and
`build3ds/reference-ds/flower-lighting`. This is an injected physical-state oracle,
separate from the normal touch journeys above. Specular/shininess and exact
raster/alpha quantization remain outside this correction.

## Memory Match bounded gate

`validation/memory-constructor` imports actor362, its original state-table
initializer021311c8, nine initial data roots plus the initializer's dependency
data, and the original12718/126e8/127ec/12860 sound wrappers. The ROM actor table
and factory are checked before generation. Scene destruction still precedes
overlay unload, and the new profile is restored on unload.

The real main-menu→Luigi-tab→Memory Match→A/rules→Quit→Rec Room path passes
with1783 triangles in the active game and zero unmapped accesses. Both static
rules screens match the actual DS exactly (98304 pixels); all8192 cart bytes
after Quit also match. Active gameplay images show the original Luigi scene,
but animation phase is not aligned. The first round has8 cards and9 coins
after entry (12 in the salon), as on DS.

The extended gameplay and reload gates are reported below. The earlier `memory-first` run failed on a null state
callback because the BSS tables had not been initialized; it is retained as
failure evidence, not success. The source originals and personal cart are unchanged.

### Memory pairs, outcomes and saved gains

`validation/memory-play-ready` passes one wrong pair followed by four correct
pairs, a won round, then three wrong pairs and a lost round. Every selection is
a real touch. The controller reads current card values to choose pairs, but
never writes game state or RNG; native and DS random deals differ. It waits for
all active cards to return to state2 and selection count0 between attempts.
The earlier fixed-delay probe in `memory-play` clicked during an animation and
failed its second-pair assertion; this was a test-input scheduling defect.

DS coin flow is12 in the salon,9 after the first stake,21 after the12-coin reward,
18 after the next stake,15 after losing and starting again. Quit refunds the
untouched third stake, leaving18. The native state checks agree, and all8192
final cart bytes exactly match actual DS `memory/played-return.sav`. The native
run has zero unmapped accesses. DS reward-timeline.json independently observes
the counter9→11→14→16→19→21 during the reward animation.

This first gameplay gate covers the initial eight-card difficulty. The later
difficulty and perfect-bonus evidence is separated below. Animated game screens are inspected but
not synchronized for pixelwise parity. `validation/memory-reload` launches a fresh native process using only the saved
cart, re-enters Memory and verifies15 coins/round0, then Quits. It passes with
zero unmapped accesses; all8192 cart bytes remain unchanged and exactly match
the fresh-boot DS reload. No emulator savestate is used for either reload.

### Memory difficulty progression and perfect bonus

The actual DS controller completes eleven consecutive perfect rounds using real
touches, without modifying RNG, cards or progression. The first five rounds use
eight cards, scores5–9 use ten, and scores10+ use twelve. A perfect round grants
18 coins, then the next stake subtracts3. Its saved cart after Quit contains
177 coins and score11; a fresh DS boot loads174 coins after the entry stake.

`validation/memory-perfect` verified ten consecutive native perfect wins and
reached score10, twelve cards and159 coins, with ten calls to the original PERFECT
callback. It then hit its genuine450-second timeout before the eleventh win and
Quit. It has zero unmapped accesses, but remains a TIMEOUT, not a completed gate.
Its on-disk cart was not flushed and still holds the initial12 coins/score0.
The ten-card board crop matches DS in all36864 pixels; the twelve-card crop
matches all40960 pixels. The visible PERFECT banner confirms the original bonus
presentation. Random animated upper-screen phases are not pixel-aligned.

The separate `memory-twelve` probe starts from a legitimately earned DS cart at
score10/162 coins, enters through the normal native menu and uses ordinary touches.
This bounded continuation must not be conflated with eleven uninterrupted native
wins. It passes six correct pairs, one original PERFECT callback, progression to
score11/174 coins after the next stake, Quit and return to Rec Room, with zero
unmapped accesses. The saved177 coins/score11 cart matches all8192 DS bytes.

`validation/memory-advanced-reload` then starts a fresh native process using only
that saved cart. Normal menu entry loads174 coins, score11, difficulty2 and six
pairs; the in-session maximum starts at0, as on DS. Rules/Quit/return passes with
zero unmapped accesses. The complete8192-byte cart stays unchanged and matches
the independent fresh-boot DS cart. This finishes the bounded twelve-card win,
perfect bonus, saved gains and reload checks. Ten/twelve-card loss-specific
animations and exact upper-screen randomized animation parity remain unverified.

## Mario’s Slides first integration gate

Actor371/MG_AMIDA is the first unlocked Mario-tab game, confirmed by the DS
vtable0213b918 and original ROM profile/factory. The earlier guess MG_JUMP was
rejected before integration after reading the actual rules screen. The native
slice adds the complete Amida TU, six original helpers, data relocation closure,
profile registration/restoration, and two narrow generator adaptations: preserving
the original Virtual7C loader return and the six-argument sound wrapper prototype.
Both adaptations have retained ROM disassembly in `build3ds/amida-closure`.

`validation/amida-first` passes normal main menu → Mario tab → Mario’s Slides →
A/rules → Quit → Rec Room with zero unmapped accesses. Both rules screens match
all98304 DS pixels and all8192 cart bytes after Quit agree. The initial active
lower screen contained unwanted sprites along the left edge. Rendering the actual
DS VRAM/OAM through the native PPU did not reproduce them, isolating the native
resource state. The defect is now resolved by importing the complete32-byte arrow OAM
descriptor, split atbyte12 by an ambiguous ROM label. `amida-arrow-fixed` passes
with the unwanted sprites removed, zero invalid accesses, and the same exact
rules/cart comparisons. `amida-closure/check_arrow.py` verifies the32 original
bytes in the ELF. The earned-score and reload gates are reported below.

`validation/amida-two-lines` uses two ordinary pen strokes, commits two new
lines (counter4→6), reaches the star and earns score1/round1, then opens rules
and Quits. It passes with zero invalid accesses, and its complete8192-byte cart
is identical to the actual DS one-point cart. Random deals differ, so the native
and DS strokes follow their respective observed routes without modifying RNG.
The earlier `amida-earned` controller drew a valid line but chose a losing route;
its Game Over assertion remains a retained FAIL, not a port defect or success.
This covers the first earned point, not five-star stage progression or every
pattern variant.

`validation/amida-reload` starts a fresh process using only the earned cart,
re-enters through the original menu, opens rules and Quits again. It passes with
zero invalid accesses. Score1 persists; all8192 bytes remain unchanged and match
the independent fresh-boot DS reload. No native emulator savestate is used.
