# Plugin SDK ABI — Hourglass C ABI Spec

**Status:** Draft
**Date:** 2026-06-11
**Phase:** 10 (Plugin SDK ABI; handoff Pending Task 7)
**Pending Task:** 7
**Author:** Claude Code session (feat/llm-intent-generator)
**Scope class:** Spec-first only. Implementation remains ON HOLD (C-5.1). This document grants no implementation authorization; it is the §10 "Hourglass C ABI spec (§3.3) → ADR" deliverable and is intended to feed a future ADR.

## Goal

Define the stable binary interface (ABI) for the **open Core plugin SDK** — one of the four
surfaces ADR-0006 fixes as open (geometry/survey engines, file formats, **plugin SDK**, automation
API). The SDK lets third parties and the closed Aura Intelligence / closed ODA bridge extend the
deterministic Core *without* linking Core authority internals and *without* compromising the
authority doctrine (AI/plugins never certify geometry — C-1.1, R-2.3). The interface is an
"hourglass": rich C++23 on both sides, a narrow versioned `extern "C"` waist at the seam.

This spec covers the ABI contract only: shape, versioning, error model, memory ownership, authority
safety, capability model, and stability policy. It does **not** specify concrete engine entry points
(those belong to per-engine specs once `libs/` exists) and contains **no implementation** — only
illustrative signatures (C-5.4: no placeholder implementations land from this doc).

## 1. Purpose & Scope — what a "plugin" is here

A **plugin** is an independently-built, separately-distributed binary that the open Core loads at
runtime (or hosts out-of-process) to extend it, communicating only across the stable C ABI defined
here. It is the open-side mechanism for two distinct classes of extender:

1. **Open third-party plugins** — importers/exporters, COGO/report add-ons, survey workflow tools,
   built by anyone against the Apache-2.0 SDK (ADR-0021). These extend production/advisory surfaces.
2. **Closed components bridged across the same waist** — the closed **Aura Intelligence** layer and
   the closed **ODA DWG bridge** (`interop_odaw`, ADR-0020) interact with the Core only across a
   process/API boundary (C-2.4, ADR-0006), never by linking Core authority internals (C-2.2). The
   ODA bridge in particular is an **out-of-process subprocess** per ADR-0020 §3 — the same hourglass
   contract serializes across IPC rather than a `dlopen` boundary.

The seam (D7, ADR-0006) is fixed and not re-litigated here: the plugin SDK and automation API are
open; JEPA / foundation models / agents / copilot / cloud / enterprise and the ODA codec are closed.
This ABI is the load-bearing interface ADR-0006 calls "the platform's most important interface."

Two transports, one contract:

| Transport | Used by | Boundary |
|---|---|---|
| In-process `dlopen`/`extern "C"` | open third-party plugins | shared-library symbol table |
| Out-of-process IPC (length-prefixed C structs over a pipe/socket) | closed ODA bridge (ADR-0020), optionally sandboxed third parties | subprocess |

The ABI struct/versioning/error rules below are transport-agnostic; §7's compatibility policy holds
identically for both.

## 2. The Hourglass Pattern

Per plan §3.3: the SDK is an **hourglass** — rich C++23 internally on both host and plugin, a thin
stable `extern "C"` facade at the waist. The waist is the *only* thing that is ABI-stable; everything
above and below it may use the full C++23 vocabulary (`std::expected`, RAII, index handles, modules).

Rules at the waist (all binding):

- **`extern "C"` linkage only.** No C++ name mangling, no overloading, no default arguments across
  the seam.
- **No C++ types cross the boundary.** No `std::string`, `std::vector`, `std::span`, `std::expected`,
  smart pointers, iterators, exceptions, RTTI, or any STL/template type in a waist signature
  (plan §3.3: "no STL types in signatures"). Strings cross as `(const char* utf8, size_t len)` pairs;
  buffers cross as `(const T* ptr, size_t count)` pairs with **C-layout POD `T` only**.
- **No OCCT types in the public ABI.** C-4.3 forbids OCCT types in public APIs outside `interop_core`;
  the plugin SDK is public open Core, so the waist exposes **zero** OCCT (and zero Boost.Geometry /
  GEOS / Eigen) types. Geometry crosses as plain coordinate POD (see §6).
- **No exceptions across the waist.** C-4.5: no FFI exceptions across `extern "C"`. Every waist
  function is `noexcept` in effect; a C++ exception reaching the waist is a contract violation. Each
  exported function wraps its body in the catch-all boundary guard the plan names `ING_FFI_BOUNDARY`
  (plan §8 H-20): any escaping exception is converted to a result code (§3), never propagated.
- **Versioned, size-prefixed structs.** Every struct crossing the waist begins with a
  `uint32_t struct_size` (and where useful a `uint32_t struct_version`) so the host can detect and
  tolerate older/newer plugin builds (plan §3.3: "versioned size-prefixed structs"; see §7).
- **C++ modules do NOT solve ABI** (plan §3.3, verbatim intent). Module boundaries are a *build/
  encapsulation* tool, not a binary-stability tool; the waist remains hand-authored `extern "C"`.

### 2.1 ABI version negotiation at load

Two version axes:

- **Semantic version** of the SDK (human-facing `MAJOR.MINOR.PATCH`), reported for diagnostics.
- **Numeric ABI version** (`uint32_t INGENEER_PLUGIN_ABI_VERSION`) — the machine-checked
  compatibility gate, bumped on the additive/breaking rules in §7.

Negotiation happens once at load, before any other call. The host reads the plugin's advertised ABI
version and refuses to proceed on a major mismatch (illustrative — signatures only, no bodies):

```c
/* Stable C waist — illustrative signatures only (NO implementation, C-5.4) */
typedef uint32_t ing_abi_version;       /* packed: (major<<16)|minor */
typedef int32_t  ing_status;            /* result code, see §3 */
typedef struct ing_host          ing_host;          /* opaque host handle */
typedef struct ing_plugin        ing_plugin;        /* opaque plugin handle */

#define INGENEER_PLUGIN_ABI_VERSION  ((ing_abi_version)0x00010000u)  /* 1.0 */

/* The single well-known entry symbol every plugin must export. */
typedef struct ing_plugin_desc {
    uint32_t        struct_size;        /* = sizeof(ing_plugin_desc) at build time */
    ing_abi_version abi_version;        /* plugin's compiled-against ABI */
    const char*     sdk_semver_utf8;    /* "x.y.z", diagnostics only */
    const char*     plugin_id_utf8;     /* stable reverse-DNS id */
    uint64_t        capabilities;       /* requested capability bitset, §6 */
    /* additive fields appended below this line only (§7) */
} ing_plugin_desc;

ing_status ingeneer_plugin_query(ing_plugin_desc* out_desc);          /* fill desc */
ing_status ingeneer_plugin_init (ing_host* host, ing_plugin** out_p); /* after gate */
void       ingeneer_plugin_shutdown(ing_plugin* p);                   /* never throws */
```

Host policy: same `major` ⇒ load; plugin `minor` ≤ host `minor` ⇒ load (plugin built against an
older host); plugin `minor` > host `minor` ⇒ load but host treats unknown trailing struct fields as
absent (additive-only guarantee, §7); `major` mismatch ⇒ refuse with `ING_ERR_ABI_INCOMPAT`.

## 3. Error Handling Across the Boundary

Maps plan §3.3's `std::expected` discipline onto the C waist. Internally both sides use
`std::expected<T, GeomErr>` (and friends) with monadic chaining; **at the waist** that becomes a
**result-code + out-param** pattern, because exceptions and `std::expected` cannot cross `extern "C"`
(C-4.5).

- Every fallible waist function returns `ing_status` (an `int32_t`); the produced value is written
  through a trailing `out_*` pointer and is only valid when the status is `ING_OK`.
- Status codes are a stable, additive-only enum. `0 == ING_OK`; negative = error; the space is
  partitioned (ABI/protocol, argument, capability/authority, domain/geometry, host/resource).
- Domain/geometry codes mirror the internal `GeomErr` enum (plan §3.3:
  `DegenerateTriangle, ConstraintIntersection, OutOfDomain, OracleMismatch`) one-to-one, so the
  agent-visible / audit-visible failure taxonomy survives the FFI round-trip.
- Rich error context (message, offending entity) is retrieved via a separate thread-local
  last-error getter, never by throwing or by returning an STL string.

```c
enum {
    ING_OK                      =  0,
    ING_ERR_ABI_INCOMPAT        = -1,   /* §2.1 negotiation failed            */
    ING_ERR_NULL_ARG            = -2,
    ING_ERR_BUFFER_TOO_SMALL    = -3,   /* see §4 two-call sizing             */
    ING_ERR_CAP_DENIED          = -10,  /* §6 capability not granted          */
    ING_ERR_AUTHORITY_DENIED    = -11,  /* §5 attempted certify/forge — hard  */
    ING_ERR_DEGENERATE_TRIANGLE = -20,  /* ← GeomErr::DegenerateTriangle      */
    ING_ERR_CONSTRAINT_CROSSING = -21,  /* ← GeomErr::ConstraintIntersection  */
    ING_ERR_OUT_OF_DOMAIN       = -22,  /* ← GeomErr::OutOfDomain             */
    ING_ERR_ORACLE_MISMATCH     = -23,  /* ← GeomErr::OracleMismatch          */
    ING_ERR_HOST_INTERNAL       = -100  /* caught exception at waist (H-20)   */
};

/* internal (host side, above the waist) — NOT crossing the boundary: */
/*   auto r = engine.insert_breakline(tin, pts);                          */
/*   if (!r) return ing_status_from(r.error());  // GeomErr → ing_status  */
```

The translation is mechanical and total in both directions: `ing_status_from(GeomErr)` on the way
out, and on a plugin's own internal side it may re-lift an `ing_status` back into its own
`std::expected`. A caught C++ exception at the waist always collapses to `ING_ERR_HOST_INTERNAL`
(never `ING_OK`), satisfying C-4.5 and plan H-20.

## 4. Memory Ownership Across the Boundary

One rule: **whoever allocates, frees, through the matching ABI call** — memory is never freed across
an allocator/heap boundary, and STL containers never cross (so their allocators never need to match).

- **Opaque handles.** All host- and plugin-owned objects cross as opaque pointers
  (`ing_host*`, `ing_plugin*`, `ing_tin*`, …) created and destroyed by paired `*_create` / `*_destroy`
  (or `init`/`shutdown`) ABI calls. The other side never dereferences them. Internally these map to
  the Core's **index-based handles** (plan §3.2: `struct VertexId { uint32_t v; }`), which are
  serializable and relocation-friendly — a good fit for both `dlopen` and IPC transports.
- **Caller-owned buffers (two-call sizing).** For variable-length output, the caller passes a buffer
  + capacity; the callee writes up to capacity and reports the required size. `ING_ERR_BUFFER_TOO_SMALL`
  means "call again with a bigger buffer." No callee-allocated returns that the caller must free.
- **Borrowed POD spans.** Inputs cross as `(const T* ptr, size_t count)` borrowed for the duration of
  the call only; the callee must not retain the pointer past return. `T` is C-layout POD (§6).
- **No passing STL containers** (plan §3.3), no transferring ownership of a `new`'d C++ object as a
  raw pointer the other side must `delete`, no `std::function` callbacks. Callbacks, where needed, are
  C function pointers plus a `void* user_data`.
- **Lifetime contract for shared buffers** (where zero-copy is later layered in): the producing side's
  arena must outlive every handle referencing it; this mirrors plan §8 H-27's buffer-lifetime contract
  and is stated here so the ABI never implies cross-heap free.

```c
/* two-call sizing — illustrative only */
ing_status ing_tin_export_triangles(const ing_tin* tin,
                                     ing_triangle* out_buf, size_t cap,
                                     size_t* out_required);
```

## 5. Authority Safety (the hard boundary)

This is the constraint that makes the SDK safe to open. The governing doctrine (ARCHITECTURE §2,
ADR-0003) is verbatim: *AI may create, modify, recommend geometry; AI may NEVER certify geometry.*
Plugins are, for authority purposes, treated as non-human agents.

**Binding rules:**

- **The ABI exposes no certify primitive.** There is no waist function that promotes an entity to
  `CERTIFIED`, and none that mutates `AuthorityClass`, `ApprovedBy`, `ApprovedAt`, or
  `VerificationState` toward certification. C-1.1 / R-2.3: promotion toward CERTIFIED requires a
  human action attributable to a licensed professional identity, and that action lives in the host's
  Entity Authority System (audit_core, Phase 3), **not** in any plugin-reachable surface.
- **Plugins operate only on `AI_PROPOSED` / advisory paths** unless the geometry they emit is
  human-attributed through the host's own promotion gate. All plugin-originated geometry enters the
  model as `AI_PROPOSED` with `SourceAgent = plugin:<plugin_id>` and a `Confidence`, exactly as AI
  geometry does (R-5.4, ADR-0003) — the storage layer (audit_core), not the ABI, records this.
- **No authority-metadata forgery.** The ABI carries no way to set `SourceAgent` to a human identity,
  to backfill `ApprovedBy/At`, or to write into the append-only hash-chained audit log (C-1.2). The
  host stamps `SourceAgent` from the loaded plugin's identity; the plugin cannot assert it. Authority
  semantics live in entity metadata only — never in layers/colors/filenames (C-1.3) — so a plugin
  cannot smuggle authority through a naming convention either.
- **Storage-layer enforcement, defense in depth.** Even if a plugin attempts a certify-shaped call,
  audit_core rejects AI-origin promotion to CERTIFIED at the storage layer (C-1.1, plan §6). The ABI's
  *absence* of the primitive is the first line; the storage rejection is the backstop. An attempted
  forgery returns `ING_ERR_AUTHORITY_DENIED` and is itself an auditable event (risk R-2 authority
  creep).
- **Certified Snapshot is read-only to plugins.** Certified deliverables derive only from the
  Certified Snapshot (C-1.4), which is generated by the host from APPROVED entities; plugins may read
  proposed/working state but cannot author into the Snapshot.

## 6. Capability / Permission Model

Plugins are **deny-by-default**. A plugin declares the capabilities it needs in `ing_plugin_desc`
(§2.1); the host grants a subset; ungranted calls return `ING_ERR_CAP_DENIED`. Capabilities are a
stable `uint64_t` bitset (additive, §7). Surfaces exposed are deliberately narrow:

| Capability | Grants | Authority class of any emitted geometry |
|---|---|---|
| `CAP_READ_PROJECT` | read working/proposed entities, project metadata | n/a (read-only) |
| `CAP_PROPOSE_GEOMETRY` | submit geometry as `AI_PROPOSED` (§5) | always `AI_PROPOSED`, `SourceAgent=plugin:*` |
| `CAP_IMPORT` / `CAP_EXPORT` | parse/emit open formats (DXF/LandXML/IFC) | imported geometry enters as `AI_PROPOSED` |
| `CAP_COGO` / `CAP_REPORT` | call deterministic COGO/report engines | proposals / non-authoritative reports |
| `CAP_GEOMETRY_QUERY` | call geometry engine queries (predicates, TIN queries) | n/a (pure query) |

Explicitly **not** exposed on any capability:

- **No OCCT / B-rep / NURBS types** (C-4.3) — geometry crosses only as coordinate POD
  (`ing_point3d {double x,y,z;}`, `ing_triangle`, index handles). B-rep solids/NURBS live behind
  `interop_core` and never enter the public plugin ABI.
- **No certify / promotion surface** (§5).
- **No audit-chain write surface** (C-1.2).
- **No raw client survey data egress path** — consistent with the privacy posture (C-3.1); any
  cloud/network behavior is the closed layer's concern, not an open-plugin capability.
- **No direct GPU/RHI access** — the renderer touches the GPU only through the internal RHI seam
  (C-4.4); plugins get renderer-friendly *data*, not GPU handles.

Out-of-process plugins (the ODA bridge, sandboxed third parties) may additionally be confined by OS
sandboxing; the capability bitset is the in-band contract, the subprocess boundary the out-of-band one.

## 7. ABI Stability & Compatibility Policy

- **Additive-only within a major.** New functions, new enum values (appended), new capability bits,
  and new **trailing** struct fields are minor bumps. Readers use `struct_size` to know which trailing
  fields are present; missing trailing fields are treated as zero/default.
- **Reserved fields.** Hot structs may carry reserved padding (`uint32_t reserved[N]`, zero-init) to
  allow additive growth without a size change where layout stability matters for IPC.
- **Never within a major:** reordering/removing struct fields, changing a field's type or meaning,
  removing or renumbering an enum value, removing a function, or tightening a precondition. Any of
  these is a **major** bump (`INGENEER_PLUGIN_ABI_VERSION` high word) and triggers `ING_ERR_ABI_INCOMPAT`
  against old plugins.
- **Version gates.** The numeric ABI version (§2.1) is the gate; semver is documentation. Deprecation
  is soft: a deprecated-but-present function stays through the remainder of a major; removal waits for
  the next major.
- **Determinism is part of the contract.** Geometry/measurement results returned across the ABI are
  bound by the engines' determinism rules (C-4.6, no wall-clock/RNG/locale; plan H-22 `-ffp-contract`
  / cross-platform tolerance). The ABI does not weaken these — a plugin sees the same deterministic
  outputs the host computes.
- **SPDX / license.** SDK headers ship Apache-2.0 (ADR-0021); the ABI surface contains no ODA or
  GPL-encumbered symbol (C-2.1, C-2.2, ADR-0020).

## 8. Open Questions / Deferred Decisions

| # | Question | Disposition |
|---|---|---|
| Q1 | In-process `dlopen` vs out-of-process **for third parties** as the default | Spec keeps both; default per-class is an ADR decision (ODA is already out-of-process per ADR-0020). |
| Q2 | Plugin **discovery/manifest** format (where plugins are found, signing, trust) | Deferred — separate spec; this doc covers the load-time ABI handshake only. |
| Q3 | Versioned **serialization** for the IPC transport (which encoding for the length-prefixed structs) | Spec mandates length-prefixed C-layout POD; concrete framing → ADR with the sync-conflict transport work (Phase 10 second half). |
| Q4 | Stable error-context payload schema beyond the code enum | Spec mandates code + thread-local getter; richer structured context deferred. |
| Q5 | Whether `CAP_PROPOSE_GEOMETRY` is offered to **open third-party** plugins in v1 or held for the closed layer first | Decision needed (see §9). |
| Q6 | ABI for **streaming** large point clouds to/from plugins (zero-copy arena handoff, plan H-27) | Deferred to Phase 7/8 once the RHI/interop buffer-lifetime contract is fixed. |

**Becomes an ADR (this spec → §10 deliverable):** §2 hourglass shape, §3 error model, §4 ownership
rules, §5 authority-safety guarantees, §6 capability model, §7 additive-only stability policy. These
are durable platform-interface decisions, peers of ADR-0006 / ADR-0011 / ADR-0014.

**Stays spec / lower-level docs:** exact per-engine entry points, the manifest/discovery format (Q2),
IPC framing bytes (Q3), and the error-context payload (Q4).

## 9. Decision Needed (human / owner sign-off)

1. **ADR creation.** Approve promoting §2–§7 into a new ADR (proposed **ADR-0026 — Plugin SDK
   hourglass C ABI**; ADR-0023/0024/0025 are taken), referenced from ARCHITECTURE §11 Open
   Question #1, which then closes.
2. **Default transport for third-party plugins** (Q1): in-process `dlopen` (lower latency, weaker
   isolation) vs out-of-process default (stronger isolation, matches ODA). Affects the threat model.
3. **`CAP_PROPOSE_GEOMETRY` exposure to open third parties in v1** (Q5): do untrusted plugins get a
   write-proposal surface day one, or is proposal initially closed-layer-only with third parties
   read/import/export/report only? Authority-safety holds either way (§5), but the attack surface and
   support burden differ.
4. **Numeric ABI version seed** — confirm `1.0` (`0x00010000`) as the published v1 ABI baseline.
5. **Plugin identity / signing posture** (relates to Q2): whether v1 requires signed plugins and how
   `plugin_id` trust is established — gates the capability model's real-world safety.

## Sources

Grounded in the following repo documents (read 2026-06-11):

- `docs/architecture/CONSTRAINTS.md` — C-1.1, C-1.2, C-1.3, C-1.4; C-2.1, C-2.2, C-2.3, C-2.4;
  C-3.1; C-4.1, C-4.3, C-4.4, C-4.5, C-4.6; C-5.1, C-5.4.
- `docs/architecture/ARCHITECTURE.md` — §2 governing doctrine, §4 open/closed seam (D7), §5 Entity
  Authority System, §11 Open Question #1 (plugin SDK ABI strategy).
- `docs/architecture/REQUIREMENTS.md` — R-2.1, R-2.3, R-2.6; R-3.5; R-5.3, R-5.4.
- `docs/adr/README.md` — ADR index.
- `docs/adr/ADR-0006-market-open-core.md` — open/closed seam; plugin SDK + automation API are open.
- `docs/adr/ADR-0021-open-core-license-apache-2.0.md` — Apache-2.0 for `libs/` + open plugin SDK.
- `docs/adr/ADR-0020-oda-terms-verification.md` — closed ODA subprocess bridge; no ODA in open `libs/`.
- `docs/adr/ADR-0003-entity-authority-system.md` (via index) — AI-never-certifies authority doctrine.
- `docs/superpowers/plans/2026-06-11-agentic-work-memory-hardening.md` — §3.3 (error handling +
  hourglass plugin SDK), §6 (AI-layer rules), §8 H-20 / H-27, §9 Phase 10 (this deliverable).
- `docs/superpowers/specs/2026-04-12-llm-intent-generator-design.md` — spec format/tone reference.
