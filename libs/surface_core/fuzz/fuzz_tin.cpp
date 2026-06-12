// Phase 6.5 — fuzz target for the full TIN public API (insert + insert_breakline under
// both crossing policies). Compiled two ways, mirroring libs/audit_core/fuzz/:
//   (1) with libFuzzer (-fsanitize=fuzzer,address,undefined) on a capable toolchain
//       (Homebrew LLVM / Linux CI; Apple clang ships no libFuzzer runtime);
//   (2) with the deterministic standalone driver (standalone_main.cpp) as a CTest under
//       whatever sanitizer the preset selects.
//
// Input bytes are an OP STREAM. Coordinates are drawn from generators chosen to maximize
// exact-predicate degeneracy collisions — raw doubles (including non-finite, which must
// be rejected loudly), a small integer lattice, midpoint rationals (denominators 2/4),
// and points exactly ON the lines through existing constrained edges and their
// extensions (derived from CURRENT TIN state — the class where the Phase 6.5 wart and
// the geometry_core incircle Layer-A filter defect were found). Breaklines are 2-4
// vertex polylines alternating Reject/Split by an input bit.
//
// After EVERY op the harness asserts:
//   * Tin::debug_audit() — CCW, neighbor symmetry, edge valence <= 2, Euler
//     T = 2n - 2 - h, constrained edges present, local constrained-Delaunay (both
//     directional incircle tests; see debug_audit for the known-predicate-defect rule).
//   * insert(p) with finite coordinates and p inside the closed convex hull NEVER fails
//     (computed independently: some finite triangle contains p by exact orient2d). This
//     is the Phase 6.5 wart-fix guarantee.
//   * ROLLBACK on any failed op: vertex_count, constrained_edge_count, breakline_count
//     and the canonical triangle set are all unchanged (the PR #24 regression class).
//
// Per-iteration cost is bounded (kMaxOps ops, kMaxVertices vertices) so the corpus
// explores deeply instead of growing one huge mesh.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ingeneer/geom/predicates.h"
#include "ingeneer/surface/tin.h"

using namespace ingeneer::surface;
using ingeneer::geom::predicates::orient2d;
using ingeneer::geom::predicates::Orientation;
using ingeneer::geom::predicates::Point2;

namespace {

constexpr std::size_t kMaxOps = 24;
constexpr std::size_t kMaxVertices = 40;

struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;

    bool done() const { return pos >= size; }
    std::uint8_t u8() { return pos < size ? data[pos++] : 0; }
    double f64() {
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) bits = (bits << 8) | u8();
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }
};

[[noreturn]] void die(const char* what) {
    std::fprintf(stderr, "fuzz_tin invariant violated: %s\n", what);
    std::abort();
}

// Coordinate generator. Mode is taken from the stream so the fuzzer controls the mix.
void gen_coord(Reader& r, const Tin& tin, double& x, double& y) {
    const std::uint8_t mode = r.u8() & 0x7u;
    switch (mode) {
        case 0: {  // raw doubles (may be non-finite: rejection paths must roll back)
            x = r.f64();
            y = r.f64();
            return;
        }
        case 1: {  // small integer lattice (massive cocircular ties)
            x = static_cast<double>(r.u8() % 9);
            y = static_cast<double>(r.u8() % 9);
            return;
        }
        case 2: {  // midpoint rationals, denominator 2
            x = static_cast<double>(r.u8() % 17) * 0.5;
            y = static_cast<double>(r.u8() % 17) * 0.5;
            return;
        }
        case 3: {  // quarter rationals, denominator 4
            x = static_cast<double>(r.u8() % 33) * 0.25;
            y = static_cast<double>(r.u8() % 33) * 0.25;
            return;
        }
        default: {  // point on a constrained-edge line or its extension (state-derived)
            std::vector<std::array<VertexId, 2>> cedges;
            for (VertexId a = 0; a < tin.vertex_count(); ++a) {
                for (VertexId b = a + 1; b < tin.vertex_count(); ++b) {
                    if (tin.is_constrained(a, b)) cedges.push_back({a, b});
                }
            }
            if (cedges.empty()) {  // no constraints yet: lattice fallback
                x = static_cast<double>(r.u8() % 9);
                y = static_cast<double>(r.u8() % 9);
                return;
            }
            const auto [a, b] = cedges[r.u8() % cedges.size()];
            // Parameter t in {-1, ..., 2} steps of 1/64: on-segment AND extensions; the
            // arithmetic is plain double, so the point usually lands microscopically off
            // the post-rounding edge — exactly the wart class.
            const double t = (static_cast<double>(r.u8() % 193) - 64.0) / 64.0;
            const TinVertex& va = tin.vertex(a);
            const TinVertex& vb = tin.vertex(b);
            x = va.x + t * (vb.x - va.x);
            y = va.y + t * (vb.y - va.y);
            return;
        }
    }
}

struct Snapshot {
    std::size_t vertices;
    std::size_t constrained;
    std::size_t breaklines;
    std::vector<std::array<VertexId, 3>> tris;
};

Snapshot snap(const Tin& tin) {
    Snapshot s{tin.vertex_count(), tin.constrained_edge_count(), tin.breakline_count(),
               tin.triangles()};
    for (auto& t : s.tris) std::sort(t.begin(), t.end());
    std::sort(s.tris.begin(), s.tris.end());
    return s;
}

void check_rollback(const Tin& tin, const Snapshot& before) {
    if (tin.vertex_count() != before.vertices) die("rollback: vertex_count changed");
    if (tin.constrained_edge_count() != before.constrained) {
        die("rollback: constrained_edge_count changed");
    }
    if (tin.breakline_count() != before.breaklines) die("rollback: breakline_count changed");
    auto now = tin.triangles();
    for (auto& t : now) std::sort(t.begin(), t.end());
    std::sort(now.begin(), now.end());
    if (now != before.tris) die("rollback: canonical triangle set changed");
}

bool accepted_pt(double x, double y, double z) {
    return coordinate_in_domain(x) && coordinate_in_domain(y) && coordinate_in_domain(z);
}

// p inside the CLOSED convex hull <=> some finite triangle contains p (exact orient2d:
// not strictly RIGHT of any directed CCW edge). Independent of the engine's locate().
bool inside_hull(const Tin& tin, double x, double y) {
    for (const auto& t : tin.triangles()) {
        bool inside = true;
        for (int i = 0; i < 3 && inside; ++i) {
            const TinVertex& u = tin.vertex(t[static_cast<std::size_t>((i + 1) % 3)]);
            const TinVertex& w = tin.vertex(t[static_cast<std::size_t>((i + 2) % 3)]);
            if (orient2d(Point2{u.x, u.y}, Point2{w.x, w.y}, Point2{x, y}) == Orientation::RIGHT) {
                inside = false;
            }
        }
        if (inside) return true;
    }
    return false;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Reader r{data, size};
    Tin tin;

    for (std::size_t op = 0; op < kMaxOps && !r.done(); ++op) {
        const Snapshot before = snap(tin);
        const std::uint8_t kind = r.u8();

        if ((kind & 0x3u) != 0 || tin.vertex_count() < 3) {
            // ---- insert ----------------------------------------------------------------
            if (tin.vertex_count() >= kMaxVertices) break;
            double x = 0.0;
            double y = 0.0;
            gen_coord(r, tin, x, y);
            const double z = static_cast<double>(r.u8());
            const bool ok_pt = accepted_pt(x, y, z);
            const bool contained = ok_pt && inside_hull(tin, x, y);
            const auto res = tin.insert(x, y, z);
            if (!res) {
                if (!ok_pt) {
                    const TinErrc code = res.error().code;
                    if (code != TinErrc::NonFiniteCoordinate &&
                        code != TinErrc::CoordinateOutOfDomain) {
                        die("rejected coordinate: wrong error code");
                    }
                } else if (contained) {
                    // Phase 6.5 guarantee: an accepted (in-domain, finite) point inside
                    // the closed hull NEVER fails to insert.
                    die("insert failed for a point inside the convex hull");
                }
                check_rollback(tin, before);
            }
        } else {
            // ---- insert_breakline ------------------------------------------------------
            const std::size_t nv = 2 + (r.u8() % 3u);  // 2-4 vertices
            if (tin.vertex_count() + nv > kMaxVertices) break;
            std::vector<TinVertex> poly(nv);
            for (auto& pv : poly) {
                gen_coord(r, tin, pv.x, pv.y);
                pv.z = static_cast<double>(r.u8());
            }
            const CrossingPolicy policy =
                (kind & 0x4u) ? CrossingPolicy::Split : CrossingPolicy::Reject;
            const auto res = tin.insert_breakline(poly, policy);
            if (!res) check_rollback(tin, before);
        }

        // Full invariant set after EVERY op (also exercised internally per op in builds
        // with INGENEER_KERNEL_DEBUG_AUDIT; explicit here so libFuzzer release-ish
        // builds audit too).
        tin.debug_audit();
    }
    return 0;
}
