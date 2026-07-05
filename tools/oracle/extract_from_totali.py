#!/usr/bin/env python3
"""Oracle fixture extraction from TOTaLi (ADR-0023; research/totali/ORACLE_FIXTURE_PROCEDURE.md).

Generates the surface_core oracle fixtures from the pinned TOTaLi survey corpus using
TOTaLi's own semantics:

* TIN fixture (<out_fixture>): scipy.spatial.Delaunay on the xy columns, exactly as
  totali/extraction/extractor.py::_build_dtm does (faces UNFILTERED — the max-edge filter is
  config-dependent post-processing; the fixture pins the raw Delaunay set).
* Contour/volume fixture (optional <out_cv_fixture>, Phase 6.3):
  - contours: verbatim replication of
    totali/extraction/extractor.py::DeterministicExtractor._contour_at_elevation
    (strict-crossing test, linear interpolation, one segment per straddling face) at levels
    chosen to avoid any vertex z exactly on a level (asserted), over the same unfiltered
    face set as the TIN fixture. TOTaLi-extracted.
  - volumes: TOTaLi has NO volume pipeline (groundtruthos-data/pipeline/features.py only
    reads precomputed cut/fill metadata), so per the ADR-0023 fallback these quantities are
    a scipy/numpy CROSS-CHECK pinned as constants: exact TIN-prism with positive-part
    clipping of the per-triangle linear difference field, self-checked against the signed
    prism sum (cut - fill == sum A*(mean d)).

Coordinates are emitted as C99 hex floats (float.hex()) so the C++ side reparses them
bit-exactly. Triangles are canonicalized (vertex indices ascending within each triangle;
triangle list sorted lexicographically) so a set comparison is order-independent. Contour
segments are canonicalized (endpoints ordered lexicographically, segment list sorted).

usage: extract_from_totali.py <totali_repo> <out_fixture> [<out_cv_fixture>] [<out_overlay_fixture>]
"""

import hashlib
import pathlib
import subprocess
import sys
from fractions import Fraction

import numpy as np
from scipy.spatial import Delaunay

# Phase 6.3 oracle parameters (corpus z range is ~[100.0, 159.8]).
CONTOUR_LEVELS = [105.0, 115.0, 130.0]
VOLUME_PLANE_Z = 115.0

# Phase 6 overlay oracle parameters (independent-two-TIN volume_between cross-check).
# Surface B: deterministic regular grid strictly covering the corpus xy footprint
# (x, y in ~[100.1, 199.9]), z sampled from an exact dyadic plane crossing the corpus
# z mid-range (corpus z ~[100.0, 159.8]; plane range over the grid ~[124.6, 135.4])
# so both cut and fill are nonzero. All constants dyadic => grid coordinates and plane
# evaluations are EXACT in float64.
GRID_ORIGIN = 96.0  # both axes
GRID_SPACING = 8.0
GRID_NODES = 15  # per axis => extent [96, 208]^2, 225 nodes
PLANE_Z0 = 130.0  # value at (150, 150)
PLANE_GX_NUM, PLANE_GX_DEN = 1, 16  # dz/dx = +1/16
PLANE_GY_NUM, PLANE_GY_DEN = -1, 32  # dz/dy = -1/32

TOTALI = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT_CV = pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else None
OUT_OVERLAY = pathlib.Path(sys.argv[4]) if len(sys.argv) > 4 else None
CORPUS = TOTALI / "tests" / "fixtures" / "survey_corpus" / "synthetic_500pt.npy"

sha = subprocess.run(["git", "-C", str(TOTALI), "rev-parse", "HEAD"],
                     capture_output=True, text=True, check=True).stdout.strip()
pts = np.load(CORPUS)
assert pts.shape == (500, 3) and pts.dtype == np.float64
input_hash = hashlib.sha256(pts.tobytes()).hexdigest()

tri = Delaunay(pts[:, :2])
simplices = tri.simplices  # (T, 3) int32

# Canonicalize: sort vertices within each triangle, then sort the triangle list.
canon = np.sort(simplices, axis=1)
canon = canon[np.lexsort((canon[:, 2], canon[:, 1], canon[:, 0]))]

hull_size = len(tri.convex_hull)
n = len(pts)
t = len(canon)
# Euler check for a triangulated point set: T = 2n - 2 - h.
assert t == 2 * n - 2 - hull_size, f"Euler mismatch: T={t} n={n} h={hull_size}"

# Total 2D area (sum of unsigned triangle areas) for a tolerance-based cross-check.
a, b, c = (pts[canon[:, k], :2] for k in range(3))
total_area = float(np.abs(np.cross(b - a, c - a)).sum() * 0.5)

with OUT.open("w") as f:
    f.write("# oracle_fixture\n")
    f.write("# id: totali-extract-corpus-500pt-v1\n")
    f.write("# source_repo: TOTaLi\n")
    f.write(f"# source_git_sha: {sha}\n")
    f.write("# extraction_script: tools/oracle/extract_from_totali.py\n")
    f.write("# input: tests/fixtures/survey_corpus/synthetic_500pt.npy\n")
    f.write(f"# input_sha256: {input_hash}\n")
    f.write("# extracted_at: 2026-06-11\n")
    f.write("# oracle_impl: scipy.spatial.Delaunay (Qhull) on xy, per totali extractor.py\n")
    f.write("# tolerances: elevation_m=1e-4 area_m2=1e-2 volume_m3=1e-1\n")
    f.write("# quantities: triangle_count, triangle_set, hull_size, total_area_m2\n")
    f.write(f"points {n}\n")
    for p in pts:
        f.write(f"{float(p[0]).hex()} {float(p[1]).hex()} {float(p[2]).hex()}\n")
    f.write(f"triangles {t}\n")
    for s in canon:
        f.write(f"{s[0]} {s[1]} {s[2]}\n")
    f.write(f"hull_size {hull_size}\n")
    f.write(f"total_area {total_area.hex()}\n")

print(f"wrote {OUT}: {n} points, {t} triangles, hull {hull_size}, area {total_area:.4f}")
print(f"TOTaLi sha {sha}, input sha256 {input_hash[:16]}...")

if OUT_CV is None:
    sys.exit(0)

# ---- Phase 6.3: contour + volume oracle quantities --------------------------------------


def contour_segments(vertices: np.ndarray, faces: np.ndarray, elev: float) -> list:
    """Verbatim semantics of totali/extraction/extractor.py::_contour_at_elevation
    (TOTaLi @ the pinned sha, lines 306-329): strict crossing test
    (z[i]-elev)*(z[j]-elev) < 0, linear interpolation, one 2-point segment per face with
    exactly two crossings."""
    segments = []
    for face in faces:
        v = vertices[face]
        z = v[:, 2]
        crossings = []
        for i in range(3):
            j = (i + 1) % 3
            if (z[i] - elev) * (z[j] - elev) < 0:
                t_ = (elev - z[i]) / (z[j] - z[i])
                pt = v[i] + t_ * (v[j] - v[i])
                crossings.append(pt[:2])
        if len(crossings) == 2:
            segments.append((crossings[0], crossings[1]))
    return segments


def positive_part_volume(p: np.ndarray, d: np.ndarray) -> float:
    """Volume of the positive part of the linear field d over triangle p ((3,2) plan
    coords): clip against d >= 0, fan-integrate. Numpy counterpart of
    libs/surface_core/src/volume.cpp::positive_part_volume."""
    poly = []
    for i in range(3):
        j = (i + 1) % 3
        if d[i] >= 0.0:
            poly.append((p[i][0], p[i][1], d[i]))
        if (d[i] > 0.0 > d[j]) or (d[i] < 0.0 < d[j]):
            s = d[i] / (d[i] - d[j])
            poly.append((p[i][0] + s * (p[j][0] - p[i][0]),
                         p[i][1] + s * (p[j][1] - p[i][1]), 0.0))
    if len(poly) < 3:
        return 0.0
    v = 0.0
    for k in range(1, len(poly) - 1):
        ax, ay = poly[k][0] - poly[0][0], poly[k][1] - poly[0][1]
        bx, by = poly[k + 1][0] - poly[0][0], poly[k + 1][1] - poly[0][1]
        area = 0.5 * abs(ax * by - ay * bx)
        v += area * (poly[0][2] + poly[k][2] + poly[k + 1][2]) / 3.0
    return v


# Levels must not coincide with any vertex z (TOTaLi's strict-crossing test and the
# engine's z+eps symbolic perturbation only agree away from exact hits).
for lev in CONTOUR_LEVELS:
    margin = float(np.abs(pts[:, 2] - lev).min())
    assert margin > 1e-6, f"level {lev} too close to a vertex z (min |dz|={margin})"

level_data = []
for lev in CONTOUR_LEVELS:
    segs = contour_segments(pts, simplices, lev)
    canon_segs = []
    for p0, p1 in segs:
        e0, e1 = (float(p0[0]), float(p0[1])), (float(p1[0]), float(p1[1]))
        if e1 < e0:
            e0, e1 = e1, e0
        canon_segs.append((e0, e1))
    canon_segs.sort()
    total_len = float(sum(np.hypot(s[1][0] - s[0][0], s[1][1] - s[0][1])
                          for s in canon_segs))
    level_data.append((lev, canon_segs, total_len))

cut = fill = signed = 0.0
for s in simplices:
    p = pts[s, :2]
    d = pts[s, 2] - VOLUME_PLANE_Z
    cut += positive_part_volume(p, d)
    fill += positive_part_volume(p, -d)
    ax, ay = p[1] - p[0]
    bx, by = p[2] - p[0]
    signed += 0.5 * abs(ax * by - ay * bx) * float(d.sum()) / 3.0
# Self-check: clipped cut/fill must reproduce the signed prism sum.
assert abs((cut - fill) - signed) < 1e-6, f"clip/signed mismatch: {cut - fill} vs {signed}"

with OUT_CV.open("w") as f:
    f.write("# oracle_fixture\n")
    f.write("# id: totali-corpus-500pt-cv-v1\n")
    f.write("# source_repo: TOTaLi\n")
    f.write(f"# source_git_sha: {sha}\n")
    f.write("# extraction_script: tools/oracle/extract_from_totali.py\n")
    f.write("# input: tests/fixtures/survey_corpus/synthetic_500pt.npy\n")
    f.write(f"# input_sha256: {input_hash}\n")
    f.write("# extracted_at: 2026-06-11\n")
    f.write("# contour_oracle: TOTaLi-extracted (verbatim _contour_at_elevation semantics,\n")
    f.write("#   totali/extraction/extractor.py L306-329, unfiltered Delaunay faces per\n")
    f.write("#   totali-extract-corpus-500pt-v1)\n")
    f.write("# volume_oracle: scipy/numpy cross-check (TOTaLi has no volume pipeline);\n")
    f.write("#   exact TIN-prism, positive-part clipping; cut = integral max(z - plane, 0)\n")
    f.write("# tolerances: elevation_m=1e-4 area_m2=1e-2 volume_m3=1e-1 length_m=1e-2\n")
    f.write("# quantities: contour_segments, contour_total_length_m, volume_cut_fill_plane\n")
    f.write("# points: see totali-corpus-500pt-v1.txt (same input, same sha)\n")
    for lev, canon_segs, total_len in level_data:
        f.write(f"contour_level {float(lev).hex()} {len(canon_segs)} {total_len.hex()}\n")
        for (x0, y0), (x1, y1) in canon_segs:
            f.write(f"{x0.hex()} {y0.hex()} {x1.hex()} {y1.hex()}\n")
    f.write(f"volume_plane {float(VOLUME_PLANE_Z).hex()} {cut.hex()} {fill.hex()}\n")

print(f"wrote {OUT_CV}:")
for lev, canon_segs, total_len in level_data:
    print(f"  level {lev}: {len(canon_segs)} segments, total length {total_len:.4f}")
print(f"  plane {VOLUME_PLANE_Z}: cut {cut:.4f} fill {fill:.4f} (net {cut - fill:.4f})")

if OUT_OVERLAY is None:
    sys.exit(0)

# ---- Phase 6: independent-two-TIN overlay volume oracle ---------------------------------
#
# Surface A: the pinned corpus Delaunay TIN (same simplices as totali-corpus-500pt-v1).
# Surface B: the regular grid above with z from the exact plane
#     z_B(x, y) = PLANE_Z0 + (x - 150) * (1/16) - (y - 150) * (1/32).
# B shares NO xy support with A (asserted), so volume_between(A, B) must take the
# general overlay path.
#
# Independent computation (does NOT mirror the C++ overlay's structure): because z_B is
# globally affine and hull(B) = [96, 208]^2 strictly contains hull(A), the integration
# region hull(A) ∩ hull(B) is exactly hull(A) and, with A in the design role and B in
# the existing role of volume_between(A, B) (cut where existing is ABOVE design),
#     cut  = Σ_{tA} ∫_{tA} max(z_B - z_A, 0) dA,
#     fill = Σ_{tA} ∫_{tA} max(z_A - z_B, 0) dA,
# i.e. NO polygon-pair overlay is needed at all — each A-triangle integrates one linear
# difference field d = z_B - z_A. Evaluated EXACTLY in Q via fractions.Fraction
# (float coordinates convert exactly), with two self-checks:
#   1. exact identity cut - fill == Σ area_t * mean(d_t) (Fraction equality), and
#   2. numpy midpoint-sampling sanity estimate at two resolutions (convergence margin
#      documented below; the PINNED values come from the exact path, never the sampler).


def plane_z_exact(x: Fraction, y: Fraction) -> Fraction:
    return (Fraction(PLANE_Z0) + (x - 150) * Fraction(PLANE_GX_NUM, PLANE_GX_DEN)
            + (y - 150) * Fraction(PLANE_GY_NUM, PLANE_GY_DEN))


def positive_part_volume_exact(p: list, d: list) -> Fraction:
    """Exact-rational positive-part prism: clip triangle p ((Fraction x, y) plan coords)
    against the linear field d >= 0, fan-integrate. All arithmetic in Q."""
    poly = []
    zero = Fraction(0)
    for i in range(3):
        j = (i + 1) % 3
        if d[i] >= zero:
            poly.append((p[i][0], p[i][1], d[i]))
        if (d[i] > zero > d[j]) or (d[i] < zero < d[j]):
            s_ = d[i] / (d[i] - d[j])
            poly.append((p[i][0] + s_ * (p[j][0] - p[i][0]),
                         p[i][1] + s_ * (p[j][1] - p[i][1]), zero))
    if len(poly) < 3:
        return zero
    v = zero
    for k in range(1, len(poly) - 1):
        ax, ay = poly[k][0] - poly[0][0], poly[k][1] - poly[0][1]
        bx, by = poly[k + 1][0] - poly[0][0], poly[k + 1][1] - poly[0][1]
        area = abs(ax * by - ay * bx) / 2
        v += area * (poly[0][2] + poly[k][2] + poly[k + 1][2]) / 3
    return v


# Grid covers the corpus footprint strictly (hull(B) ⊃ hull(A)) and shares no xy support.
grid_hi = GRID_ORIGIN + (GRID_NODES - 1) * GRID_SPACING
assert GRID_ORIGIN < pts[:, 0].min() and grid_hi > pts[:, 0].max()
assert GRID_ORIGIN < pts[:, 1].min() and grid_hi > pts[:, 1].max()
grid_pts = []
for iy in range(GRID_NODES):
    for ix in range(GRID_NODES):
        gx = GRID_ORIGIN + ix * GRID_SPACING  # exact in float64 (dyadic)
        gy = GRID_ORIGIN + iy * GRID_SPACING
        gz = float(plane_z_exact(Fraction(gx), Fraction(gy)))  # exact (dyadic plane)
        grid_pts.append((gx, gy, gz))
corpus_xy = {(float(p[0]), float(p[1])) for p in pts}
assert not corpus_xy & {(gx, gy) for gx, gy, _ in grid_pts}, "grid shares support with A"

ov_cut = ov_fill = ov_net = ov_area = Fraction(0)
for s in simplices:
    p = [(Fraction(float(pts[k, 0])), Fraction(float(pts[k, 1]))) for k in s]
    # d = z_B - z_A: cut where the existing surface (B) is above the design (A).
    d = [plane_z_exact(*p[i]) - Fraction(float(pts[k, 2])) for i, k in enumerate(s)]
    ov_cut += positive_part_volume_exact(p, d)
    ov_fill += positive_part_volume_exact(p, [-di for di in d])
    ax, ay = p[1][0] - p[0][0], p[1][1] - p[0][1]
    bx, by = p[2][0] - p[0][0], p[2][1] - p[0][1]
    area_t = abs(ax * by - ay * bx) / 2
    ov_area += area_t
    ov_net += area_t * (d[0] + d[1] + d[2]) / 3
# Self-check 1 (exact): clipped cut/fill reproduce the signed prism sum identically in Q.
assert ov_cut - ov_fill == ov_net, "exact clip/signed identity violated"
# Both signs must occur (the plane crosses the corpus surface).
assert ov_cut > 0 and ov_fill > 0, "plane does not cross surface A"
# Integration region == hull(A): exact triangle-area sum vs the v1 fixture's total_area.
assert abs(float(ov_area) - total_area) < 1e-9, "overlap area != hull(A) area"


def sampled_cut_fill(res: int) -> tuple:
    """Numpy midpoint-sampling sanity estimate over hull(A)'s bbox (sanity ONLY; the
    pinned values come from the exact Fraction path above)."""
    xs = np.linspace(pts[:, 0].min(), pts[:, 0].max(), res, endpoint=False)
    ys = np.linspace(pts[:, 1].min(), pts[:, 1].max(), res, endpoint=False)
    dx = (pts[:, 0].max() - pts[:, 0].min()) / res
    dy = (pts[:, 1].max() - pts[:, 1].min()) / res
    gx, gy = np.meshgrid(xs + dx / 2, ys + dy / 2)
    q = np.column_stack([gx.ravel(), gy.ravel()])
    simp = tri.find_simplex(q)
    inside = simp >= 0
    q, simp = q[inside], simp[inside]
    t_ = tri.transform[simp]
    b2 = np.einsum("ijk,ik->ij", t_[:, :2, :], q - t_[:, 2, :])
    bary = np.column_stack([b2, 1.0 - b2.sum(axis=1)])
    za = (pts[tri.simplices[simp], 2] * bary).sum(axis=1)
    dvals = (PLANE_Z0 + (q[:, 0] - 150.0) * (PLANE_GX_NUM / PLANE_GX_DEN)
             + (q[:, 1] - 150.0) * (PLANE_GY_NUM / PLANE_GY_DEN)) - za
    cell = dx * dy
    return float(np.maximum(dvals, 0.0).sum() * cell), float(np.maximum(-dvals, 0.0).sum() * cell)


# Self-check 2 (sanity): midpoint sampling agrees with the exact values within 1%
# relative at BOTH res=800 and res=1600 (boundary-cell + facet-crossing error is
# O(1/res); observed agreement is well inside this; never used as the pinned source).
est_lo = sampled_cut_fill(800)
est_hi = sampled_cut_fill(1600)
for exact, lo, hi, name in ((ov_cut, est_lo[0], est_hi[0], "cut"),
                            (ov_fill, est_lo[1], est_hi[1], "fill")):
    ex = float(exact)
    assert abs(lo - ex) < 0.01 * ex, f"sampling sanity (800) failed for {name}: {lo} vs {ex}"
    assert abs(hi - ex) < 0.01 * ex, f"sampling sanity (1600) failed for {name}: {hi} vs {ex}"

ov_cut_f, ov_fill_f, ov_area_f = float(ov_cut), float(ov_fill), float(ov_area)

with OUT_OVERLAY.open("w") as f:
    f.write("# oracle_fixture\n")
    f.write("# id: totali-corpus-500pt-overlay-v1\n")
    f.write("# source_repo: TOTaLi\n")
    f.write(f"# source_git_sha: {sha}\n")
    f.write("# extraction_script: tools/oracle/extract_from_totali.py\n")
    f.write("# input: tests/fixtures/survey_corpus/synthetic_500pt.npy\n")
    f.write(f"# input_sha256: {input_hash}\n")
    f.write("# extracted_at: 2026-06-12\n")
    f.write("# surface_a: corpus Delaunay TIN per totali-corpus-500pt-v1.txt (same input,\n")
    f.write("#   same sha; build from that fixture's points)\n")
    f.write("# surface_b: deterministic 15x15 regular grid, origin (96, 96), spacing 8 m\n")
    f.write("#   (extent [96, 208]^2, strictly covering hull(A)); z from the exact dyadic\n")
    f.write("#   plane z = 130 + (x - 150)/16 - (y - 150)/32. Affine z => any Delaunay\n")
    f.write("#   diagonal choice on the cocircular grid squares yields the SAME surface,\n")
    f.write("#   so the oracle is triangulation-independent. No shared xy support with A\n")
    f.write("#   (asserted) => volume_between(A, B) takes the general overlay path.\n")
    f.write("# volume_oracle: independent exact-rational computation (TOTaLi has no volume\n")
    f.write("#   pipeline, per ADR-0023 fallback): z_B globally affine + hull(B) ⊃ hull(A)\n")
    f.write("#   reduce the overlay to per-A-triangle positive-part integrals of the linear\n")
    f.write("#   difference field, evaluated in Q via fractions.Fraction (no polygon-pair\n")
    f.write("#   clipping; structurally independent of the C++ overlay). Self-checks:\n")
    f.write("#   exact cut - fill == signed prism sum; numpy midpoint-sampling convergence\n")
    f.write("#   (res 800/1600, 1% margin, sanity only). overlap_area == hull(A) area,\n")
    f.write("#   exact triangle-area sum.\n")
    f.write("# orientation: volume_between(A, B) with A = design, B = existing;\n")
    f.write("#   cut = integral max(z_B - z_A, 0), fill = integral max(z_A - z_B, 0)\n")
    f.write("# tolerances: area_m2=1e-2 volume_m3=1e-1\n")
    f.write("# quantities: grid_points, overlay_volume (cut fill area)\n")
    f.write(f"grid_points {len(grid_pts)}\n")
    for gx, gy, gz in grid_pts:
        f.write(f"{gx.hex()} {gy.hex()} {gz.hex()}\n")
    f.write(f"overlay_volume {ov_cut_f.hex()} {ov_fill_f.hex()} {ov_area_f.hex()}\n")

print(f"wrote {OUT_OVERLAY}: {len(grid_pts)} grid points")
print(f"  overlay: cut {ov_cut_f:.4f} fill {ov_fill_f:.4f} (net {ov_cut_f - ov_fill_f:.4f}) "
      f"area {ov_area_f:.4f}")
print(f"  sampling sanity (res 1600): cut {est_hi[0]:.4f} fill {est_hi[1]:.4f}")
