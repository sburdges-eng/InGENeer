# Architecture Decision Records — Index

Baseline V1, approved 2026-06-11. Format: Status / Context / Decision / Consequences.
D-numbers trace to the Architecture Discovery Interview (Rounds 1–3).

| ADR | Title | Decisions | Status |
|-----|-------|-----------|--------|
| [0001](ADR-0001-platform-topology-monorepo.md) | Platform topology: single monorepo | D2 (superseded) | Accepted |
| [0002](ADR-0002-clean-sheet-knowledge-first.md) | Clean-sheet architecture, knowledge-first migration | D3, D22, R9-rule | Accepted |
| [0003](ADR-0003-entity-authority-system.md) | AI authority doctrine & Entity Authority System | D1, D20 | Accepted |
| [0004](ADR-0004-orchestrate-first-ai.md) | Orchestrate-first AI strategy | D4 | Accepted |
| [0005](ADR-0005-v1-wedge.md) | V1 wedge: topo field-to-finish + legal descriptions | D5 | Accepted |
| [0006](ADR-0006-market-open-core.md) | Dual market & open-core licensing boundary | D6, D7 | Accepted |
| [0007](ADR-0007-local-first-inference.md) | Local-first inference, cloud opt-in | D8 | Accepted |
| [0008](ADR-0008-carlson-bridge.md) | Carlson/IntelliCAD bridge: transitional first | D9 | Accepted |
| [0009](ADR-0009-project-container-sync.md) | Project container + optional sync service | D10 | Accepted |
| [0010](ADR-0010-us-only-v1.md) | US-only v1 jurisdiction | D11 | Accepted |
| [0011](ADR-0011-geometry-stack.md) | Geometry stack: survey-native core, permissive backend, OCCT satellite | D12 | Accepted |
| [0012](ADR-0012-custom-tin-engine.md) | Custom in-house TIN engine | D13 | Accepted |
| [0013](ADR-0013-survey-core-engines.md) | Survey Core engine decomposition | D14 | Accepted |
| [0014](ADR-0014-languages.md) | Languages: C++23 core, Swift app, Python intelligence | D15 | Accepted |
| [0015](ADR-0015-metal-first-rendering.md) | Metal-first rendering behind RHI seam | D16 | Accepted |
| [0016](ADR-0016-swiftui-metal-ui.md) | SwiftUI + Metal viewport UI | D17 | Accepted |
| [0017](ADR-0017-decision-based-flywheel.md) | Decision-based learning flywheel | D18, D21 | Accepted |
| [0018](ADR-0018-oda-dwg-codec.md) | DWG via ODA membership | D19 | Accepted |
| [0019](ADR-0019-monorepo-layout-migration.md) | Monorepo layout & five-stage migration | Stage 1–5 | Accepted |
| [0020](ADR-0020-oda-terms-verification.md) | ODA terms vs open-core structure (R-10) | R-10 | Accepted |
| [0021](ADR-0021-open-core-license-apache-2.0.md) | Open Core license: Apache-2.0 | D7 | Accepted |
| [0022](ADR-0022-stage1-relocation-ruling.md) | Stage 1 relocation ruling (Option C) | C-5.5 | Accepted |
| [0023](ADR-0023-totali-oracle-discipline.md) | TOTaLi oracle discipline (frozen semantics, fixtures) | Stage 2 | Accepted |
| [0024](ADR-0024-stage2-component-triage.md) | Stage 2 component triage (promote vs rewrite) | Stage 2 | Accepted |
| [0025](ADR-0025-swift-cpp-interop-direct-c-abi.md) | Swift↔C++ interop: direct C ABI, zero-copy buffers | R-11, A-6 | Accepted |
| [0026](ADR-0026-licensed-professional-identity.md) | Licensed professional identity: cryptographic signatures, offline-capable | R-2.3, C-1.1 | Accepted |
| [0027](ADR-0027-baseline-freeze-protocol.md) | Baseline freeze protocol: SHA-256, manifests, freeze tags | ADR-0023 | Accepted |

Rejected alternatives are recorded inside each ADR. Risk register: `../architecture/RISK_REGISTER.md`.
