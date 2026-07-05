# ADR-0029: Plugin SDK Hourglass C ABI — Deny-by-Default Capabilities, No Certify Primitive

**Status:** Proposed (awaiting owner sign-off on the open questions below; NOT accepted)
**Date:** 2026-06-12
**Deciders:** Pending — owner sign-off required on every item in "Open questions blocking acceptance"
**Related:** ADR-0006 (open/closed seam D7; plugin SDK is open), ADR-0021 (Apache-2.0 open Core), ADR-0020 (closed ODA subprocess bridge), ADR-0003 (AI-never-certifies authority doctrine), C-1.1/C-1.2/C-1.3/C-1.4, C-2.1/C-2.2/C-2.4, C-3.1, C-4.3/C-4.4/C-4.5/C-4.6, C-5.1/C-5.4, R-2.3/R-5.4, plan §3.3/§8 H-20/H-27, ARCHITECTURE §11 Open Question #1, ABI spec (`docs/superpowers/specs/2026-06-11-plugin-sdk-abi-spec.md`)

## Context

ADR-0006 fixes the plugin SDK as one of the four open surfaces and calls its interface
"the platform's most important interface." Phase 10 of the agentic plan delivered the
spec for that interface — spec-first per C-5.1, authorizing no implementation —
as `docs/superpowers/specs/2026-06-11-plugin-sdk-abi-spec.md`. The spec defines the
stable binary contract through which both open third-party plugins (in-process
`dlopen`) and closed components (the Aura Intelligence layer and the out-of-process
ODA DWG bridge per ADR-0020 §3) extend the deterministic Core without linking Core
authority internals (C-2.2) and without compromising the authority doctrine: AI and
plugins never certify geometry (C-1.1, R-2.3).

The spec's §8 designates its §2–§7 as the durable platform-interface decisions to be
promoted into this ADR — a peer of ADR-0006 / ADR-0011 / ADR-0014 — and its §9
enumerates the owner decisions that must be ruled before acceptance. This ADR drafts
that promotion. ADR-0028's Consequences recorded the renumbering of this proposed ADR
from 0028 to 0029. On acceptance, ARCHITECTURE §11 Open Question #1 (plugin SDK ABI
strategy) closes (spec §9 item 1).

## Decision (proposed — pending the sign-offs below)

The load-bearing choices of the ABI spec, proposed for ratification:

1. **Hourglass shape, one contract over two transports** (spec §1, §2, §2.1). Rich
   C++23 on both sides; a narrow versioned `extern "C"` waist at the seam. At the
   waist: no C++ name mangling, no STL/template types in signatures, no OCCT (or
   Boost.Geometry/GEOS/Eigen) types in the public ABI (C-4.3), no exceptions crossing
   (C-4.5; every export wrapped in the `ING_FFI_BOUNDARY` catch-all guard, plan §8
   H-20), versioned size-prefixed structs (`uint32_t struct_size` first). C++ modules
   are a build/encapsulation tool, not an ABI tool. Negotiation happens once at load
   via a numeric `INGENEER_PLUGIN_ABI_VERSION` gate (`(major<<16)|minor`); major
   mismatch refuses with `ING_ERR_ABI_INCOMPAT`. The same struct/versioning/error
   rules hold for in-process `dlopen` (open third parties) and out-of-process IPC
   (the closed ODA bridge; length-prefixed C structs over a pipe/socket).
2. **Error model: result code + out-param** (spec §3). Every fallible waist function
   returns `ing_status` (`int32_t`, `0 == ING_OK`, negative = error); values come back
   through trailing out-pointers. The status space is a stable additive-only enum,
   partitioned (ABI/protocol, argument, capability/authority, domain/geometry,
   host/resource); domain codes mirror the internal `GeomErr` enum one-to-one so the
   audit-visible failure taxonomy survives the FFI round-trip. Rich error context
   comes from a thread-local last-error getter. A caught C++ exception at the waist
   always collapses to `ING_ERR_HOST_INTERNAL`, never `ING_OK`.
3. **Memory ownership: whoever allocates, frees, through the matching ABI call**
   (spec §4). Objects cross as opaque handles with paired `*_create`/`*_destroy`
   (mapping internally to the Core's index-based handles); variable-length output
   uses caller-owned buffers with two-call sizing (`ING_ERR_BUFFER_TOO_SMALL`);
   inputs cross as borrowed C-layout POD spans not retained past return; callbacks
   are C function pointers plus `void* user_data`. No STL containers, no cross-heap
   free, no callee-allocated returns the caller must free; shared-buffer lifetime
   follows the plan §8 H-27 arena-outlives-handle contract.
4. **Authority safety: the ABI exposes no certify primitive** (spec §5). No waist
   function promotes an entity to `CERTIFIED` or mutates `AuthorityClass`,
   `ApprovedBy`, `ApprovedAt`, or `VerificationState` toward certification (C-1.1,
   R-2.3). All plugin-originated geometry enters as `AI_PROPOSED` with
   `SourceAgent = plugin:<plugin_id>` and a `Confidence`, exactly as AI geometry does
   (R-5.4, ADR-0003); the host stamps `SourceAgent` — the plugin cannot assert it,
   and there is no audit-chain write surface (C-1.2). Defense in depth: even a
   certify-shaped attempt is rejected at the storage layer by `audit_core`, returns
   `ING_ERR_AUTHORITY_DENIED`, and is itself an auditable event. The Certified
   Snapshot is read-only to plugins (C-1.4).
5. **Capability model: deny-by-default** (spec §6). A plugin declares requested
   capabilities (stable additive `uint64_t` bitset) in its `ing_plugin_desc`; the
   host grants a subset; ungranted calls return `ING_ERR_CAP_DENIED`. The exposed
   surfaces are deliberately narrow (`CAP_READ_PROJECT`, `CAP_PROPOSE_GEOMETRY`,
   `CAP_IMPORT`/`CAP_EXPORT`, `CAP_COGO`/`CAP_REPORT`, `CAP_GEOMETRY_QUERY`).
   Explicitly not exposed on any capability: OCCT/B-rep/NURBS types (geometry crosses
   only as coordinate POD and index handles), certify/promotion, audit-chain writes,
   raw client survey-data egress (C-3.1), and direct GPU/RHI access (C-4.4).
6. **Stability policy: additive-only within a major** (spec §7). New functions,
   appended enum values, new capability bits, and trailing struct fields are minor
   bumps read via `struct_size`; reordering/removing/retyping anything is a major
   bump gated by `ING_ERR_ABI_INCOMPAT`. The numeric ABI version is the gate; semver
   is documentation; deprecation is soft within a major. Determinism is part of the
   contract (C-4.6, plan H-22) — plugins see the same deterministic outputs the host
   computes. SDK headers ship Apache-2.0 (ADR-0021) with no ODA or GPL-encumbered
   symbol in the ABI surface (C-2.1, C-2.2, ADR-0020).

Per spec §8, the following stay in spec / lower-level docs and are **not** decided
here: exact per-engine entry points, the plugin discovery/manifest format (spec Q2),
IPC framing bytes (spec Q3), the structured error-context payload (spec Q4), and the
streaming/zero-copy point-cloud ABI (spec Q6, deferred to Phase 7/8 behind H-27).

## Open questions blocking acceptance (owner sign-off checklist)

Carried verbatim from spec §9 ("Decision Needed — human / owner sign-off"). None may
be resolved by an agent; this ADR remains **Proposed** until each is ruled:

1. **ADR creation.** Approve promoting §2–§7 into a new ADR (proposed **ADR-0029 —
   Plugin SDK hourglass C ABI**; ADR-0023–0028 are taken — 0028 went to the
   out-of-core octree ratification, which records this renumbering), referenced from
   ARCHITECTURE §11 Open Question #1, which then closes.
2. **Default transport for third-party plugins** (spec Q1): in-process `dlopen`
   (lower latency, weaker isolation) vs out-of-process default (stronger isolation,
   matches ODA). Affects the threat model.
3. **`CAP_PROPOSE_GEOMETRY` exposure to open third parties in v1** (spec Q5): do
   untrusted plugins get a write-proposal surface day one, or is proposal initially
   closed-layer-only with third parties read/import/export/report only?
   Authority-safety holds either way (spec §5), but the attack surface and support
   burden differ.
4. **Numeric ABI version seed** — confirm `1.0` (`0x00010000`) as the published v1
   ABI baseline.
5. **Plugin identity / signing posture** (relates to spec Q2): whether v1 requires
   signed plugins and how `plugin_id` trust is established — gates the capability
   model's real-world safety.

## Consequences

- Acceptance closes ARCHITECTURE §11 Open Question #1 and fixes the open Core's
  load-bearing extension interface as a peer of ADR-0006/0011/0014.
- Acceptance authorizes **no implementation**: the SDK remains spec-first under
  C-5.1, and no placeholder implementations land from the spec (C-5.4). Per-engine
  entry points wait for `libs/` and per-engine specs.
- The closed ODA bridge and Aura Intelligence layer get their boundary contract: the
  same hourglass waist, serialized over IPC, never a link against Core authority
  internals (C-2.2, C-2.4).
- The authority doctrine gains a structural guarantee at the open boundary: the
  certify primitive is absent from the ABI by construction, with the audit_core
  storage rejection as backstop — plugins are non-human agents for authority
  purposes, end to end.
- Spec Q2 (discovery/manifest), Q3 (IPC framing), Q4 (error-context payload), and Q6
  (streaming ABI) remain open in the spec and are decided in their own follow-on
  specs/ADRs — none are ratified or foreclosed here. Q3's concrete framing was
  pointed by the spec at the ADR work accompanying the sync-conflict transport
  (Phase 10 second half).

## Alternatives considered (from the spec; rejections pending ratification with this ADR)

- **C++ ABI / modules at the boundary:** rejected — C++ modules are a
  build/encapsulation tool, not a binary-stability tool; name mangling, STL layout,
  and exceptions make a C++ seam unshippable as a stable public ABI (spec §2).
- **Exceptions or `std::expected` across the waist:** forbidden by C-4.5; the
  result-code + out-param pattern preserves the internal `std::expected` discipline
  on both sides while keeping the seam C-pure (spec §3).
- **Callee-allocated returns / cross-heap ownership transfer:** rejected — memory is
  never freed across an allocator boundary; two-call sizing and paired
  create/destroy calls replace it (spec §4).
- **Exposing any certify/promotion or authority-metadata surface to plugins:**
  rejected as the load-bearing safety property — promotion toward CERTIFIED lives
  only in the host's Entity Authority System behind a human gate (spec §5; C-1.1,
  R-2.3).
- **Allow-by-default or unscoped plugin permissions:** rejected — deny-by-default
  capability bitset, host-granted, with narrow enumerated surfaces (spec §6).
