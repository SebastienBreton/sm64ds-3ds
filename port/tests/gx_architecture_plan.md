# GX batch execution and direct geometry preparation

Status: plan and isolated diagnostics; no new performance result. Baseline is
the integrated binary `17e469ed11302cb3c6b579e0d3e120c692b46f63a2558e9b1da463f12ad66359`.
The prior 72.197 ms update implies a +50% throughput gate of 48.131 ms. Parser
work alone is not the architectural milestone. Render includes original game
code that command caching cannot remove; its remaining cost is not measured.

## Current path and boundaries

* `runtime.cpp::DMAStartTransfer -> copy_words` is the real list ingress.
  For ordinary RAM and FIFO destination it calls `gx_write_fifo` for each word.
  This branch currently does not update the IO mirror. Preserve that behavior.
  VRAM sources use `MemoryTransferBus` reads/writes, including IO mirror writes.
  Preserve physical-bank read order, aliasing, widths and last FIFO mirror.
* `Copy36Bytes`, `Copy48BytesFixed` and direct IO writes can continue a partially
  decoded packet. DMA chunks also need not end at command boundaries. FIFO
  decoder state and direct-port parameter state are independent and persistent.
  IRQ/callback delivery follows each synchronous DMA block and must stay there.
* `gx.cpp::feed` extracts four command bytes, repeatedly determines parameter
  count, copies parameters to `g_params`, then calls `exec`. `exec` mutates all
  live matrices/stacks, lighting, material, vertex latches and texture state.
* `project` preserves DS combined fixed12 transforms before perspective divide.
  `push_vertex` stores transformed vertices in a growing strip; `emit_tri`
  expands primitive topology into `GxTriangle` storage. Presentation traverses
  those triangles, converts colors/UV, clips and quantizes the DS viewport into
  another vector. Viewer copies that vector to its linear VBO and adjusts the
  DS/PICA pixel centers. The same VBO supports center capture and both eyes.

The old diagnostic counts are useful: 3,648.8 vertices, 2,657 NORMAL commands,
2,661 direction conversions, 1,335 matrix commands, 150.4 texture binds,
zero texture decodes and 2,310.4 triangles/update. Its phase times are not net
budgets: fine timers changed Render from approximately 31 to 55 ms.

## Measurement before choosing optimization scope

Prepare an isolated replacement `gx.o` and `runtime.o` archive; no shared graph
or production edits. A startup-only selector chooses counters only, sampled
GX ingress, sampled command groups, sampled primitives or sampled bulk ingress.
Sample every 64th invocation independently per category, with configurable
offset and rate; keep counts across the exact original 120-update window.
Read ticks directly, report samples and count-weighted estimates separately.
Only one hierarchy level is timed per run. No logging, allocation or environment
lookups occur inside the measured window. Report clock-pair calibration and
full Render perturbation versus counters-only and the uninstrumented baseline.

Mutually disjoint command categories are matrices, NORMAL, vertices, texture
commands and other commands. Their sampled sum estimates `exec`, not total
GX ingress. Ingress estimates include `exec`; their difference is a noisy
estimate of parser/dispatch overhead, not an additive phase. Bulk ingress also
includes nested GX execution; subtracting it from original Render only estimates
non-bulk work and misses direct-port emission. Primitive projection, matrix
multiply, direction, bind and assembly timings remain inclusive diagnostics;
never add them to command or ingress totals. Use offsets/rates and repeated
runs to expose sampling aliasing, distributions and instrumentation cost.

The first decision gate is the actual removable parser budget. If only 1–3 ms,
the parser becomes an execution tool for the larger architecture, not a claim
that the GX region can be halved. Native timings remain NOT_EXECUTED until a
coordinated run. Whole-update A/B uses neither counters nor sample timers.

## API and staged implementation

1. `gx_write_fifo_words(const uint32_t* host_words, size_t count)` executes
   a stable contiguous RAM span. Initially it caches only blocks with empty
   packed decoder at both boundaries. Cache entries own the words plus decoded
   `{command, parameter_count, parameter_offset, consumed_word_end}` records.
   A pointer is a lookup hint only: a hit requires equal count and full `memcmp`
   of bytes. Mutations/reused addresses rebuild. Resource/matrix commands replay
   their original `exec` bodies against current state, so no rendered results
   or external resources are silently retained. Record consumed-word positions
   to preserve stream hash and any future mirror before each command executes.
   Partial packets, IO/VRAM sources, unusual source ranges and oversize blocks
   fall back unchanged. Bound memory and retain DMA interrupt boundaries.
   This stage preserves all original per-vertex exact math and triangle output.

2. Give compiled lists a general dependency/read-write summary: position,
   projection, direction, texture matrices and relevant stack slots; inherited
   position/color/UV/material/light/shininess/viewport/BEGIN state; texture and
   palette bytes/mapping/version. Treat unknown commands as conservative
   dependencies. This is required for reuse of geometry, not raw parser hits.
   Decoded immutable topology/local coordinates can be reused unconditionally
   after byte validation. Transformed/lit results require exact values of all
   dependencies and preserved state deltas, not pointer identity or a hash-only
   key. Animated palette slots invalidate only their dependent spans. Cache
   latches/stores/push/pop cannot be skipped without replaying their side effects.

3. Coordinate with FPS on a frame sink and linear arena lease:
   `acquire_geometry_arena(capacity) -> {vertices, capacity, frame_token}`;
   `gx_set_triangle_sink(context, emit(vertices[3], material), retain_cpu)`;
   `commit_geometry_arena(token, vertex_count, ordered_batches, convergence)`.
   GX emits completed primitives directly to the sink, avoiding the permanent
   expanded triangle list when admitted. The sink uses the *existing* DS clipping,
   quantization, UV/color conversion and pixel-center math in the same order,
   appending directly to linear memory and grouping only consecutive compatible
   materials. No global sorting of alpha/shadow polygon IDs. Visible-depth median
   uses separate small scratch storage; source triangles are unnecessary for it.

   Acquire before Render; never select a still-in-flight arena. A lease owns
   geometry through DS-center capture and both eyes, flushing once after commit.
   Capacity/unsupported-path rejection must occur before state is lost, using
   a prevalidated bound or a replayable frame journal, not silent truncation.
   Explicit CPU raster/oracle/capture diagnostics can retain legacy triangles.
   Do not patch viewer/player_present until FPS agrees on ownership and API.

ARM11 payload sizes are 44 bytes/GxVertex, 160/GxTriangle and 40/V3dsVert.
At 2,310 triangles the retained triangles plus converted vector plus VBO are
approximately 903 KiB of payload per update before clipping growth. That is
a representation/storage envelope, not measured DRAM traffic or a timing gain.
Direct emission removes representations/copies but leaves exact transforms,
lighting and clipping when their dependencies change. Reuse must target those
remaining calculations to approach the 18–20 ms architectural saving.

## Proof and fallback contract

Compare ordered command execution and relevant decoder state, every matrix
including stack slots, vertex/color/UV latches, materials/lights/shininess,
texture/palette parameters and immutable texture identity, polygon attributes,
ordered geometry and DS framebuffer. Include every possible split boundary,
zero-param packets, all legal parameter counts, port/FIFO interleaving, reset
and frame persistence, mutated/reused source pointers, identical lists under
different incoming state, animation matrices and physical VRAM remapping.
Existing diffuse/specular/texgen/shadow/viewport oracles remain independent.
Use real BMD lists and captured natural streams; synthetic parser tests alone
cannot establish game parity. Direct VBO must also prove byte-identical prepared
vertices/batches and center/depth/alpha coverage before actual stereo/gameplay.

CPU GX test/result commands still execute on the exact CPU state. A specialized
render sink is a backend choice, not permission to substitute float PICA math
for fixed12. No transformation, clipping, rounding or alpha approximation is
admitted without a separate exact domain proof. Texture generations cannot
replace byte snapshots until all relevant writes/remapping are covered.

No budget is promised. The conditional target is roughly 18–20 ms from GX and
conversion combined, plus at least 6 ms net PPU improvement without overlap.
Root owns the frozen global A/B and genuine stereo gameplay gate; FPS owns
viewer/affine GPU, menu owns PPU separation, and runtime edits are coordinated.
