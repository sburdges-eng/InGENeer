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

usage: extract_from_totali.py <totali_repo> <out_fixture> [<out_cv_fixture>]
"""

import hashlib
import pathlib
import subprocess
import sys

import numpy as np
from scipy.spatial import Delaunay

# Phase 6.3 oracle parameters (corpus z range is ~[100.0, 159.8]).
CONTOUR_LEVELS = [105.0, 115.0, 130.0]
VOLUME_PLANE_Z = 115.0

TOTALI = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT_CV = pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else None
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
