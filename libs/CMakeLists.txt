# Engine libraries. Each lib is wired here only when its phase begins real implementation
# — we do not add placeholder targets (CONSTRAINTS C-5.4 "no placeholder implementations").
#
# Wired now:
#   audit_core      — Phase 3 (first foundational module)
#   geometry_core   — Phase 5 (numeric policy + robust predicates; CDT/nanoflann wrappers)
#
# Skeleton (directory + .gitkeep, wired when their phase starts):
#   surface_core    — Phase 6      pointcloud_core — Phase 7
#   coordinate_core — Phase 7      interop_core    — Phase 4/10
#   ai_core         — Phase 9      parcel_core, survey_core — later
add_subdirectory(audit_core)
add_subdirectory(geometry_core)
