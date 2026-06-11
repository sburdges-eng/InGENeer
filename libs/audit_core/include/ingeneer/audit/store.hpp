// audit_core — append-only hash-chained store with storage-layer authority enforcement.
// Spec: docs/superpowers/specs/2026-06-11-audit-core-storage-schema-spec.md
#ifndef INGENEER_AUDIT_STORE_HPP
#define INGENEER_AUDIT_STORE_HPP

#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ingeneer/audit/error.hpp"
#include "ingeneer/audit/event.hpp"

struct sqlite3;

namespace ingeneer::audit {

// One Store = one chain database identified by `chain_id` (the canonical record's
// "project_id"). Writes on a Store are serialized by an internal writer mutex (H-21);
// cross-connection writers are serialized by BEGIN IMMEDIATE + busy_timeout. Readers may
// open additional Stores on the same file.
class Store {
public:
    // Open (creating if needed) a chain database. Applies WAL + pragmas and the schema.
    // `chain_id` stamps every appended record (product chain: project id; agent chain:
    // session scope).
    static std::expected<Store, AuditError> open(const std::string& path, std::string chain_id);

    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    ~Store();

    // Append a raw chain event (no authority semantics) — used by both chains.
    std::expected<AppendResult, AuditError> append(const Event& ev);

    // Create an entity with authority metadata (R-2.1). Records a creation promotion +
    // event atomically. Creating at APPROVED/CERTIFIED requires a human actor (C-1.1);
    // AI_PROPOSED and REVIEWED are agent-accessible by design (spec §3.3 guards 1–2 only
    // bind APPROVED and above).
    std::expected<AppendResult, AuditError> create_entity(const CreateEntityRequest& req);

    // Promote an entity along the ladder (R-2.2) with storage-layer guards (spec §3.3).
    std::expected<AppendResult, AuditError> promote(const PromotionRequest& req);

    // Recompute every hash and check prev linkage offline (R-2.6). A NULL value in a
    // NOT NULL column is reported as ChainBroken ("corrupted"), distinct from a hash
    // mismatch ("tampered"), so forensics can tell the cases apart.
    std::expected<void, AuditError> verify_chain() const;

    // Current projected authority class for an entity.
    std::expected<AuthorityClass, AuditError> authority_of(const std::string& entity_id) const;

    // Certified Snapshot: CERTIFIED entities only (R-2.4/R-2.5, C-1.4).
    std::expected<std::vector<Entity>, AuditError> certified_snapshot() const;

    // Count of chain events (test/diagnostic).
    std::expected<std::int64_t, AuditError> event_count() const;

    // Export the chain as JSONL in the Python AuditLogger line format (spec §6): keys in
    // insertion order seq/timestamp/project_id/event/data/prev_hash/hash, ", "/": "
    // separators. The result verifies under Python AuditLogger.verify_chain().
    std::expected<void, AuditError> export_jsonl(const std::string& out_path) const;

private:
    Store() = default;
    sqlite3* db_ = nullptr;
    std::string chain_id_;
    // unique_ptr keeps Store movable; guards all write paths (H-21 single-writer).
    std::unique_ptr<std::mutex> write_mutex_;
};

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_STORE_HPP
