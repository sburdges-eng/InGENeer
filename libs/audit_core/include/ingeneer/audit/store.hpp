// audit_core — append-only hash-chained store with storage-layer authority enforcement.
// Spec: docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md
#ifndef INGENEER_AUDIT_STORE_HPP
#define INGENEER_AUDIT_STORE_HPP

#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "ingeneer/audit/error.hpp"
#include "ingeneer/audit/event.hpp"

struct sqlite3;

namespace ingeneer::audit {

// Single-writer per Store (H-21). Readers may open additional Stores on the same file.
class Store {
public:
    // Open (creating if needed) a chain database. Applies WAL + pragmas and the schema.
    static std::expected<Store, AuditError> open(const std::string& path);

    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    ~Store();

    // Append a raw chain event (no authority semantics) — used by both chains.
    std::expected<AppendResult, AuditError> append(const Event& ev);

    // Create an entity with authority metadata (R-2.1). Records a creation promotion +
    // event atomically. Rejects creating directly at APPROVED/CERTIFIED by a non-human.
    std::expected<AppendResult, AuditError> create_entity(const CreateEntityRequest& req);

    // Promote an entity along the ladder (R-2.2) with storage-layer guards (spec §3.3).
    std::expected<AppendResult, AuditError> promote(const PromotionRequest& req);

    // Recompute every hash and check prev linkage offline (R-2.6).
    std::expected<void, AuditError> verify_chain() const;

    // Current projected authority class for an entity.
    std::expected<AuthorityClass, AuditError> authority_of(const std::string& entity_id) const;

    // Certified Snapshot: CERTIFIED entities only (R-2.4/R-2.5, C-1.4).
    std::expected<std::vector<Entity>, AuditError> certified_snapshot() const;

    // Count of chain events (test/diagnostic).
    std::expected<std::int64_t, AuditError> event_count() const;

private:
    Store() = default;
    sqlite3* db_ = nullptr;
};

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_STORE_HPP
