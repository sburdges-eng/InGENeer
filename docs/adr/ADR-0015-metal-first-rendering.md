# ADR-0015 — Metal-First Rendering Behind an RHI Seam

**Status:** Accepted (D16).

## Decision
Native Metal renderer for point clouds, TIN surfaces, and 2D drafting, built against a thin internal RHI (render hardware interface) abstraction so a Vulkan backend can be added when Windows/Linux activate. Exploit Apple Silicon unified memory for zero-copy point-cloud → GPU paths; renderer-friendly data layouts are an engine design constraint from day one.

## Rejected
- wgpu/Dawn: faster cross-platform path but perf ceiling and younger ecosystem at CAD scale.
- Qt RHI: only sensible with a Qt UI, which was rejected (ADR-0016).
- Defer/OpenGL interim: risks baking renderer-hostile data layouts into the engines.

## Consequences
Engine code touches the GPU only through the RHI seam (C-4.4); auracad's legacy immediate-mode OpenGL viewport is not migrated.
