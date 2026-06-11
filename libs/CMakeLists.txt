# Engine libraries. Each lib is wired here only when its phase begins real implementation
# — we do not add placeholder targets (CONSTRAINTS C-5.4 "no placeholder implementations").
#
# Wired now:
#   audit_core      — Phase 3 (first foundational module)
#
# Skeleton (directory + .gitkeep, wired when their phase starts):
#   geometry_core   — Phase 5      surface_core  — Phase 6
#   pointcloud_core — Phase 7      coordinate_core — Phase 7
#   interop_core    — Phase 4/10   ai_core       — Phase 9
#   parcel_core, survey_core       — later
add_subdirectory(audit_core)
