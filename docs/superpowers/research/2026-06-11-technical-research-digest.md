<!-- Provenance: produced by the Hermes research session 20260611_015644_47f677
     (delegated web-research task), recovered from the session store 2026-06-11 and
     committed so the plan's "research digest" reference resolves in-tree.
     Cited by: docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md §5.
     Caveat from the producing agent: page-extract backend was unavailable (search-only);
     findings combine search-result content with model domain knowledge — verify any
     load-bearing claim against the primary source before relying on it in a spec. -->

# Technical Reference Digest: Agentic-AI Work System with Persistent Memory for a C++23/Swift CAD Platform (InGENeer)

---

## A. AGENT MEMORY ARCHITECTURES

**Key sources**
- MemGPT paper: https://arxiv.org/abs/2310.08560
- Letta (MemGPT successor) memory system internals: https://deepwiki.com/letta-ai/letta/2.3-agent-memory-system
- A-MEM: Agentic Memory for LLM Agents: https://arxiv.org/abs/2502.12110 (code: https://github.com/agiresearch/A-mem)
- Zep/Graphiti temporal knowledge graph memory: https://arxiv.org/abs/2501.13956 (code: https://github.com/getzep/graphiti)
- "Episodic Memory is the Missing Piece for Long-Term LLM Agents": https://arxiv.org/pdf/2502.06975
- Mem^p (procedural memory for agents): https://arxiv.org/html/2508.06433v2
- Anthropic, "Effective harnesses for long-running agents": https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents

**Findings**
1. **MemGPT/Letta model = virtual context management.** Treat the context window like RAM and external stores like disk; the agent itself pages data via tools. Letta implements this as labeled "memory blocks" (persona, project state, constraints) that are recompiled into the system prompt on every step, plus recall (conversation history search) and archival (vector DB) tiers. The agent edits its own core memory with `core_memory_replace`/`archival_memory_insert` tool calls. This is directly reusable for InGENeer's implementation agents: a `project_state` block, a `current_spec` block, and a `decisions` block, all persisted in SQLite.
2. **A-MEM (Zettelkasten-style)** generates a structured note per memory (content, keywords, tags, embedding), then runs *link generation* (find nearest memories, ask LLM whether to link) and *memory evolution* (updating old notes' tags/context when new related notes arrive). Useful for the product flywheel: each surveyor decision (e.g., "breakline accepted along curb face") becomes a note that links to and refines prior decisions.
3. **Separate the three memory types explicitly** — surveys consistently find software-engineering agents lean hardest on *procedural* memory (verified code patterns, build commands, architecture decisions), while *episodic* (what happened in session N) and *semantic* (facts about the codebase/domain) serve retrieval. Store them in different tables with different retrieval policies; don't blend into one vector soup. (https://arxiv.org/html/2603.07670v1)
4. **Event-sourced decision log.** Make the append-only SHA-256 audit chain the *source of truth* and derive memory views from it (classic event sourcing/CQRS): every agent action emits an immutable event `{seq, ts, actor, intent, inputs_hash, outputs_hash, prev_hash, sha256}`; semantic/episodic stores are rebuildable projections. This unifies (a) audit/certification requirements and (b) the learning flywheel — the same log feeds both.
5. **Hybrid vector + graph retrieval (Zep/Graphiti pattern).** Zep builds a bi-temporal knowledge graph: episodic nodes (raw events, non-lossy, traceable for citation) link to extracted entity nodes and semantic edges with validity intervals (`valid_at`, `invalid_at`); retrieval combines cosine similarity, BM25, and graph traversal, then reranks. For InGENeer: entities = surfaces, alignments, parcels, control points; edges = "derived-from", "superseded-by", "certified-by". Bi-temporal edges natively model "this surface was valid until re-survey on date X".
6. **Drift prevention across sessions (Anthropic harness pattern):** long-running agents must work in discrete sessions; each session starts amnesiac. The cure is an *initializer agent* that sets up environment + writes structured artifacts, then *coding agents* that each: read a progress/handoff file, do one increment, run tests, update the handoff file, commit. Two distinct techniques: **compaction** (summarize old context in-window) vs **context reset + structured handoff** (fresh agent, state carried in files). Use handoff files (`PROGRESS.md`, `DECISIONS.md`, feature-list JSON with pass/fail status) as the canonical inter-session contract.
7. **Anchored system prompt:** keep invariants (architecture rules, "AI proposes, never certifies", coding standards) in a system prompt regenerated from versioned files each session — never carried as mutable conversation state, so they can't be eroded by compaction.

**Example — memory tier table for InGENeer agents**

| Tier | Contents | Store | Mutation | Budget |
|---|---|---|---|---|
| Anchored system prompt | Invariants, authority rules, coding standards | versioned files → prompt | git PR only | ~2K tok |
| Core memory blocks | project_state, current_task, open_questions | SQLite rows, in-context | agent tool calls | ~4K tok |
| Working context | current diff, test output | ephemeral | per-step | rest of window |
| Episodic | session transcripts, decision log events | append-only event log (hash-chained) | append-only | retrieved on demand |
| Semantic | codebase facts, domain entities | vector DB + Graphiti-style graph | consolidation job | top-k retrieval |
| Procedural | "how to run sanitizer CI", verified recipes | markdown skills/playbooks | promoted from episodic after N successes | loaded by name |

**Consolidation pseudocode**
```
on session_end(transcript):
    events = extract_decisions(transcript)        # LLM pass
    for e in events: audit_chain.append(e)        # hash-chained
    notes = a_mem_noteify(events)                 # keywords/tags/embedding
    for n in notes:
        neighbors = vector_search(n.emb, k=10)
        link_or_evolve(n, neighbors)              # A-MEM evolution
    if recipe_succeeded >= 3 times: promote_to_procedural(recipe)
    write_handoff(PROGRESS.md, next_steps, failing_tests)
```

---

## B. AGENT ORCHESTRATION RELIABILITY

**Key sources**
- Anthropic long-running agent harnesses: https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents
- LangChain agent evaluation readiness checklist: https://www.langchain.com/blog/agent-evaluation-readiness-checklist
- LangGraph stateful workflow / supervisor patterns: https://medium.com/@krishnan.srm/langgraph-mcp-patterns-c24d2f29754f
- Framework comparison incl. durable execution (Temporal vs LangGraph checkpointing): https://www.speakeasy.com/blog/ai-agent-framework-comparison

**Findings**
1. **Generator/evaluator separation.** Anthropic's harness splits planning, generation, and evaluation into separate agents to defeat self-grading bias — the agent that wrote code never decides it's done; an evaluator agent with fresh context judges against the spec. For InGENeer: a *spec agent* freezes acceptance criteria as machine-checkable artifacts before any code agent runs.
2. **Spec-anchored validation = contract tests as ground truth.** Convert the spec into an executable feature list (JSON of features, each with test command + pass/fail state). The orchestrator, not the LLM, runs the tests; the LLM only sees results. Drift is then detectable mechanically: any session that flips a previously-passing contract test to failing is rejected/rolled back. This is the single highest-leverage anti-drift mechanism for multi-session C++ work.
3. **Plan/act separation.** Require a written plan artifact (files to touch, tests to add, expected diffs) approved/validated *before* edit tools unlock. LangGraph implements this naturally as graph nodes with conditional edges (plan → human-or-rule gate → act → verify); checkpointing persists graph state so interrupted runs resume deterministically.
4. **Deterministic tool sandboxes.** Tools should be pure-ish: pinned toolchain (exact clang version, locked deps), hermetic build dir, fixed seeds, no network during build/test. Same inputs → same outputs makes agent behavior auditable and makes the audit chain meaningful. Run agent shells in a jail restricted to the repo workdir.
5. **Layered guardrails:** (i) prompt-level invariants (anchored), (ii) tool allow-lists per agent role (evaluator gets read-only FS), (iii) post-hoc validators (clang-tidy gate, sanitizer suite, contract tests), (iv) hard authority boundary in the *product*: the entity-authority system rejects any AI-originated certification at the API level, not the prompt level. Never rely on (i) alone.
6. **Eval before scale.** LangChain's checklist: manually review 20–50 real traces before building eval infra; define unambiguous success criteria; build small offline eval sets from real failures. Apply this to InGENeer's geometry agents (e.g., golden TIN fixtures: "given these points + breaklines, contours must match reference within tolerance").
7. **Durable execution for long jobs.** LangGraph checkpointing covers in-process resumption; for crash-safe multi-hour pipelines (point-cloud processing supervised by agents) the documented pattern is an external durable engine (Temporal-style) where each agent step is an idempotent activity.

**Example — session contract (orchestrator-enforced, not prompt-enforced)**
```yaml
# session_contract.yaml — checked by harness before merge
preconditions:
  - git_clean: true
  - read_files: [PROGRESS.md, DECISIONS.md, specs/feature_list.json]
invariants:                       # mechanically verified post-session
  - cmd: "ctest --preset asan"     ; must: pass
  - cmd: "clang-tidy-diff"         ; must: zero_new_warnings
  - cmd: "python check_api_abi.py" ; must: no_breaking_changes
  - contract_tests_previously_passing: must_still_pass
postconditions:
  - file_updated: PROGRESS.md
  - audit_event_appended: true     # hash-chained
on_violation: revert_branch_and_log
```

---

## C. C++23 HARDENING

**Key sources**
- OpenSSF Compiler Options Hardening Guide: https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++.html
- Quarkslab Clang hardening cheat sheet (2026 refresh): https://blog.quarkslab.com/clang-hardening-cheat-sheet-ten-years-later.html
- C++ Core Guidelines: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines (GSL: https://github.com/microsoft/GSL)
- Sanitizers overview/CI guidance: https://github.com/behnamasadi/cpp_tutorials/blob/master/docs/sanitizers.md , https://iree.dev/developers/debugging/sanitizers/
- Conan sanitizer workflow (ABI modeling): https://blog.conan.io/sanitizers/toolchain/tools/conan/2025/11/25/How-to-use-sanitizers-in-your-conan-workflow.html
- libFuzzer: https://llvm.org/docs/LibFuzzer.html ; FuzzedDataProvider: https://github.com/google/fuzzing/blob/master/docs/split-inputs.md
- isocpp exceptions FAQ: https://isocpp.org/wiki/faq/exceptions

**Findings**
1. **Ownership/lifetime (Core Guidelines I.11, R.1–R.5, F.7):** raw pointers never own; `unique_ptr` for ownership transfer, `gsl::not_null<T*>` for non-owning never-null params, `std::span<T>` (C++20+) for all buffer-length pairs — eliminates the classic CAD-kernel bug class of pointer+count desync. For the geometry kernel: TIN/half-edge structures should use index-based handles (`VertexId{uint32_t}`) instead of pointers — safer, smaller, serializable, and relocation-friendly.
2. **Sanitizer matrix in CI.** ASan+UBSan combined in one job (low conflict, ~2x slowdown); TSan in its own job (incompatible with ASan) for the threaded point-cloud pipelines; MSan only if you can build *all* deps (incl. GEOS/GDAL) instrumented — otherwise false positives make it useless; many teams substitute Valgrind or skip MSan. Use `-fno-sanitize-recover=undefined` so UBSan aborts in CI, and `ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1`. Model sanitizer as an ABI-affecting build axis (Conan setting / CMake preset) so instrumented deps stay consistent.
3. **Hardening flags (OpenSSF consensus set):** `-O2 -Wall -Wformat=2 -Wconversion -Wimplicit-fallthrough -Werror=format-security -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstrict-flex-arrays=3 -fstack-clash-protection -fstack-protector-strong` plus `-Wl,-z,relro -Wl,-z,now` on ELF. GCC 14+ offers `-fhardened` as a one-flag umbrella for most of these. On Apple/clang, also `-ftrivial-auto-var-init=zero` and (arm64e) pointer authentication; libc++ hardening via `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST` in release, `_EXTENSIVE`/debug in dev — gives bounds-checked `operator[]` on vector/span nearly free.
4. **clang-tidy + clang-format as gates, not suggestions.** Pin versions in CI; run clang-tidy on the diff (`clang-tidy-diff.py`) with `cppcoreguidelines-*, bugprone-*, performance-*, modernize-*, concurrency-*` checks and `WarningsAsErrors`; `.clang-format` enforced by a format-check job. For agent-written code this is critical: it's a deterministic validator in the guardrail stack of section B.
5. **Structured fuzzing for every parser.** InGENeer parses LandXML, DXF, LAS/LAZ, control files — each gets a libFuzzer target built with `-fsanitize=fuzzer,address,undefined`, using `FuzzedDataProvider` to slice the input into structured fields; seed corpora from real survey files; run continuously (OSS-Fuzz style nightly job) with corpus minimization (`-merge=1`).
6. **Contracts/assertions:** C++26 contracts (`pre`/`post`) aren't usable yet in C++23; the practical pattern is a tiered assert macro: `KERNEL_ASSERT` (always on, even in release, for invariants whose violation corrupts survey data — cheap checks like "triangle indices in range") vs `KERNEL_DEBUG_ASSERT` (debug/sanitizer builds only, e.g., full Delaunay-property audits). `-D_GLIBCXX_ASSERTIONS`/libc++ hardening covers the stdlib side.
7. **Exceptions vs `std::expected<T,E>` (C++23):** use `std::expected` for *expected, recoverable* domain failures — degenerate geometry, constraint conflicts, file-format errors — because failures are part of the API contract and must be visible to the agent layer and audit chain; reserve exceptions for truly exceptional states (allocation failure, programming errors) or ban them at the plugin ABI. Monadic ops (`and_then`, `transform`, `or_else`) keep pipelines clean. **Never let exceptions cross the C ABI plugin boundary.**
8. **Plugin SDK ABI stability:** C++ has no stable ABI across compilers/stdlib versions — expose plugins through an `extern "C"` facade with versioned structs (size-prefixed for forward compat), opaque handles, no STL types in signatures; or adopt hourglass pattern (C ABI core, header-only C++ sugar on both sides). C++20 modules don't solve ABI; keep the SDK header-distributed and semver the C layer.

**Example — error type + boundary pattern**
```cpp
// kernel API: expected for domain failures
enum class GeomErr { DegenerateTriangle, ConstraintIntersection, OutOfDomain };

auto insert_breakline(Tin& tin, std::span<const Point3d> polyline)
    -> std::expected<BreaklineId, GeomErr>;

// usage in the agent-facing service layer:
auto r = insert_breakline(tin, pts)
           .and_then([&](BreaklineId id){ return retriangulate(tin, id); })
           .transform_error([&](GeomErr e){ audit.log_failure(e); return e; });

// plugin ABI: C only, no exceptions, versioned
extern "C" {
  typedef struct { uint32_t struct_size; uint32_t abi_version; /*...*/ } ing_plugin_info_v1;
  int32_t ing_plugin_get_info(ing_plugin_info_v1* out);  // returns ING_OK / error code
}
```

---

## D. GEOMETRY / KERNEL ALGORITHMS

**Key sources**
- Shewchuk, Adaptive Precision FP Arithmetic & Fast Robust Geometric Predicates: https://people.eecs.berkeley.edu/~jrs/papers/robustr.pdf (public-domain `predicates.c` via https://www.cs.cmu.edu/~quake/robust.html)
- Triangle (Ruppert refinement, CDT): https://www.cs.cmu.edu/~quake/triangle.html ; paper https://people.eecs.berkeley.edu/~jrs/papers/triangle.pdf
- artem-ogre/CDT (C++, MPL-2.0): https://github.com/artem-ogre/CDT (docs: https://artem-ogre.github.io/CDT/)
- CDT library benchmark thread (CDT vs poly2tri vs CGAL vs Triangle): https://github.com/artem-ogre/CDT/issues/40
- Constrained DT background: https://en.wikipedia.org/wiki/Constrained_Delaunay_triangulation
- nanoflann (BSD, header-only KD-tree): https://github.com/jlblancoc/nanoflann
- Prismoidal/end-area volume reference (WYDOT survey manual): https://www.dot.state.wy.us/files/live/sites/wydot/files/shared/Highway_Development/Surveys/Survey+Manual/Appendix+F+-+Volume.pdf
- TIN/contours background: https://en.wikipedia.org/wiki/Triangulated_irregular_network

**Findings**
1. **Library/licensing decision:** Shewchuk's **Triangle** is the algorithmic gold standard (CDT + Ruppert quality refinement + exact arithmetic) but is **non-commercial without a license** — a problem for InGENeer's open core. **artem-ogre/CDT is MPL-2.0** (commercial-safe), numerically robust (uses adaptive predicates), supports constraint edges with intersection resolution (`IntersectingConstraintEdges::TryResolve`) and conforming triangulation. **poly2tri** (BSD) is fast but notoriously fragile on degenerate/repeated points. **CGAL** is GPL/commercial dual — avoid in the open core. Recommendation: CDT (MPL-2.0) + Shewchuk's public-domain `predicates.c`.
2. **Bowyer–Watson incremental insertion** is the right base algorithm for a dynamic TIN (surveyors add/remove points constantly): locate containing triangle (walk + spatial index), find the "cavity" of triangles whose circumcircle contains the new point, delete, re-fan to the new point. O(n log n) expected with a good point-location structure; randomized insertion order avoids pathological cases.
3. **Robustness is non-negotiable:** all in-circle/orientation decisions must use **Shewchuk adaptive predicates** — floating-point filter first, escalate to exact expansion arithmetic only near degeneracy (typically <1% of calls), so you get exact correctness at ~float speed. Naive double-precision predicates *will* produce inverted triangles and crashed cavity walks on real survey data (collinear points along roadway alignments are the common killer).
4. **Breakline insertion (constrained edges):** for each breakline segment, (a) insert endpoints as TIN vertices, (b) find triangles crossed by the segment, (c) remove crossing edges and retriangulate the two pseudo-polygons on either side so the segment becomes an edge (Anglada-style pseudo-polygon retriangulation). Survey-grade detail: breaklines carry Z; crossing breaklines must either be rejected (`GeomErr::ConstraintIntersection`) or split at the computed intersection with interpolated Z, per project policy — make this an explicit, audited decision (flywheel input!).
5. **Contour extraction from TIN:** for contour level z, each triangle whose vertex z-range straddles z contributes one segment computed by linear interpolation along the two crossed edges; chain segments into polylines via shared-edge hashing. Handle the vertex-exactly-on-level degeneracy by symbolic perturbation (treat z_vertex == z as z + ε). Smooth afterwards (e.g., Chaikin) but always tag smoothed contours as derived, non-authoritative geometry.
6. **Volumes:** surface-to-surface volume over a shared (merged) triangulation is exact for linear TINs: for each prism between design and existing surface, V = A_plan × (Δz1+Δz2+Δz3)/3, signed for cut/fill. The classical **prismoidal formula** V = L/6 (A1 + 4Am + A2) applies to corridor cross-section workflows (more accurate than average-end-area V = L/2 (A1+A2), which overestimates for pyramidal solids). Implement both: TIN-prism for site volumes, prismoidal for alignment-based reports (matches DOT manuals — see WYDOT ref).
7. **Spatial indexing:** **nanoflann** (BSD, header-only, no deps) for static/bulk KD-tree queries — kNN and radius search over point clouds; build is O(n log n), queries microseconds at 10⁷ points. For out-of-core/LOD rendering of massive clouds, an **octree with Potree-style nested LOD sampling** is the standard (feeds section E). Use nanoflann's `L2_Simple_Adaptor` with a flat `float[3]` SoA dataset adaptor; rebuild-on-bulk-load, not incremental.

**Example — Shewchuk adaptive orient2d sketch**
```c
// Stage A: fast float path with error filter (covers ~99% of calls)
double orient2d(const double* pa, const double* pb, const double* pc) {
    double detleft  = (pa[0]-pc[0]) * (pb[1]-pc[1]);
    double detright = (pa[1]-pc[1]) * (pb[0]-pc[0]);
    double det = detleft - detright;
    double detsum = fabs(detleft) + fabs(detright);
    if (fabs(det) >= ccwerrboundA * detsum)   // statically derived bound
        return det;                            // sign is provably correct
    return orient2dadapt(pa, pb, pc, detsum);  // Stage B–D: escalate using
}                                              // exact expansion arithmetic
// orient2dadapt computes the determinant as a sum of nonoverlapping doubles
// (two_product / two_sum exact transforms), re-checking error bounds B, C, D,
// only going fully exact when the sign is still ambiguous.
```

**Example — Bowyer–Watson insertion pseudocode**
```
insert(p):
  t0 = locate(p)                       # walk from last-inserted triangle
  cavity = BFS from t0 over neighbors where incircle(tri, p) > 0   # Shewchuk
  boundary = edges of cavity hull
  delete cavity triangles
  for edge (a,b) in boundary: create triangle (a, b, p)
  legalize nothing further needed (cavity method is already Delaunay)
```

---

## E. METAL COMPUTE / RENDER KERNELS

**Key sources**
- Vertex amplification: https://developer.apple.com/documentation/metal/improving-rendering-performance-with-vertex-amplification
- WWDC22 "Go bindless with Metal 3" (argument buffers tier 2, heaps): https://developer.apple.com/videos/play/wwdc2022/10101/
- WWDC23 "Render with Metal" (residency, synchronization, command submission): https://developer.apple.com/videos/play/wwdc2023/10125/
- GPU-driven rendering w/ indirect command buffers (worked example): https://www.kodeco.com/books/metal-by-tutorials/v2.0/chapters/15-gpu-driven-rendering
- Metal modern-era features survey (ICB/argument-buffer evolution): https://metalbyexample.com/a-decade-of-metal-the-modern-era/
- MTKView + SwiftUI representable pattern: https://medium.com/@giikwebdeveloper/metal-view-for-swiftui-93f5f78ec36a

**Findings**
1. **Unified memory zero-copy:** on Apple Silicon use `MTLStorageMode.shared` buffers — CPU (your C++ kernel) and GPU see the same physical pages; no blit, no `managed` synchronization (that's Intel-Mac legacy). Allocate point-cloud pages with `device.makeBuffer(bytesNoCopy:length:options:.storageModeShared)` over page-aligned (`mmap`/`posix_memalign`, 16KB-aligned) memory owned by the C++ engine → the TIN/point-cloud engine writes, Metal renders, zero copies. Mind CPU/GPU hazards: fence with `MTLSharedEvent` or triple-buffer per-frame uniform data.
2. **GPU-driven culling + indirect command buffers (ICB):** for 100M+ point clouds, store the octree's node metadata in an argument buffer; a compute kernel performs frustum + screen-space-error LOD culling per node and encodes draw commands into an ICB (`MTLIndirectCommandBuffer`); render pass executes with `executeCommandsInBuffer` — CPU cost per frame becomes O(1) regardless of node count. This is the Potree-on-Metal architecture.
3. **Argument buffers (tier 2 / Metal 3):** bindless residency — write GPU addresses of all octree-node vertex buffers into one argument buffer, call `useHeap`/`useResource(.read)` once, and let kernels index freely. Drastically cuts encoder overhead for thousands of LOD nodes.
4. **Vertex amplification** renders the same geometry to multiple outputs (stereo, cascaded shadow maps, multi-viewport) in one pass — relevant for InGENeer's future AR/stereo review modes, not core 2D/3D viewport; don't reach for it for plain point splatting.
5. **Point rendering technique:** prefer compute-shader software rasterization of points into a 64-bit atomic depth+color buffer (`atomic_min` on packed depth|color) over native `point_size` sprites once counts exceed ~10M — this is the established high-perf approach (Schütz et al.-style) and maps cleanly to Metal compute with `device atomic_ulong*`.
6. **SwiftUI integration:** wrap `MTKView` in `NSViewRepresentable` (macOS) with a `Coordinator: MTKViewDelegate` holding the renderer; SwiftUI state flows in via `updateNSView`; never recreate the device/queue on update. Use `view.enableSetNeedsDisplay = true` + explicit invalidation for a CAD viewport (redraw-on-change, not 120Hz game loop) to keep energy use down.
7. **CoreML/ANE coexistence:** keep inference (CoreML) and rendering on separate queues; both share unified memory, so feature tensors derived from the point cloud need no copies either — but schedule heavy compute kernels with `MTLCommandQueue` priorities so agent-triggered batch jobs don't starve the viewport.

**Example — minimal MSL compute kernel (point cloud transform + frustum classify)**
```metal
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 mvp;
    float4   frustumPlanes[6];
    uint     pointCount;
};

kernel void classify_points(
    device const packed_float3* positions  [[buffer(0)]],
    device       uint*          visibility [[buffer(1)]],
    constant     Uniforms&      u          [[buffer(2)]],
    uint gid [[thread_position_in_grid]])
{
    if (gid >= u.pointCount) return;
    float4 p = float4(positions[gid], 1.0);
    uint vis = 1u;
    for (uint i = 0; i < 6; ++i)
        vis &= (dot(u.frustumPlanes[i], p) >= 0.0) ? 1u : 0u;
    visibility[gid] = vis;
}
```
```swift
// Swift dispatch (zero-copy buffer from the C++ engine)
let buf = device.makeBuffer(bytesNoCopy: cppEnginePointsPtr,
                            length: byteLen, options: .storageModeShared,
                            deallocator: nil)!
let tpg = MTLSize(width: (n + 255) / 256, height: 1, depth: 1)
encoder.setComputePipelineState(pso)
encoder.setBuffer(buf, offset: 0, index: 0)
encoder.dispatchThreadgroups(tpg,
    threadsPerThreadgroup: MTLSize(width: 256, height: 1, depth: 1))
```
```swift
// SwiftUI wrapper skeleton
struct MetalViewport: NSViewRepresentable {
    @ObservedObject var scene: ViewportScene
    func makeCoordinator() -> Renderer { Renderer(scene: scene) }
    func makeNSView(context: Context) -> MTKView {
        let v = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        v.delegate = context.coordinator
        v.enableSetNeedsDisplay = true     // CAD: draw on demand
        v.isPaused = true
        return v
    }
    func updateNSView(_ v: MTKView, context: Context) {
        context.coordinator.apply(scene); v.needsDisplay = true
    }
}
```

---

## Cross-cutting recommendations for InGENeer
1. **One log, many views:** the SHA-256 audit chain doubles as the event source for agent episodic memory and the decision-learning flywheel — design the event schema once (actor, intent, geometry refs, authority state) and derive vector/graph memory as projections.
2. **Validators are the memory of intent:** contract tests + clang-tidy + sanitizers + golden geometry fixtures are what actually prevent multi-session drift; handoff files just make them legible to the next session.
3. **Licensing guardrail:** CDT (MPL-2.0) + Shewchuk predicates (public domain) + nanoflann (BSD) + Eigen (MPL-2.0) are commercial-open-core-safe; Triangle and CGAL are not.

---

**Summary of work:** Ran 12 targeted web searches covering all five areas (agent memory papers MemGPT/A-MEM/Zep/episodic-semantic-procedural surveys; Anthropic long-running-agent harness; LangGraph/eval patterns; OpenSSF/Quarkslab hardening flags; sanitizer CI guidance; Shewchuk predicates and Triangle/CDT licensing; nanoflann; TIN contour/volume references; Metal vertex amplification/ICB/argument-buffer docs and MTKView-SwiftUI patterns). The page-extract backend was unavailable (search-only), so findings combine search-result content with domain knowledge; all cited URLs were verified to exist via search results. No files were created — the digest above is the deliverable, ~3,400 words, structured A–E with sources, findings, and code examples (memory tier table + consolidation pseudocode, session contract YAML, expected<T>/C-ABI pattern, orient2d adaptive sketch, Bowyer–Watson pseudocode, MSL kernel + Swift dispatch + SwiftUI wrapper).