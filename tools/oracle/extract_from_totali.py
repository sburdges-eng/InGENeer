#!/usr/bin/env python3
"""Oracle fixture extraction from TOTaLi (ADR-0023; research/totali/ORACLE_FIXTURE_PROCEDURE.md).

Generates the surface_core TIN oracle fixture from the pinned TOTaLi survey corpus using
TOTaLi's own triangulation semantics (scipy.spatial.Delaunay on the xy columns, exactly as
totali/extraction/extractor.py does).

Coordinates are emitted as C99 hex floats (float.hex()) so the C++ side reparses them
bit-exactly. Triangles are canonicalized (vertex indices ascending within each triangle;
triangle list sorted lexicographically) so a set comparison is order-independent.

usage: extract_from_totali.py <totali_repo> <out_fixture>
"""

import hashlib
import pathlib
import subprocess
import sys

import numpy as np
from scipy.spatial import Delaunay

TOTALI = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
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
