// audit_core — value types for the authority model (REQUIREMENTS R-2.1/R-2.2).
#ifndef INGENEER_AUDIT_EVENT_HPP
#define INGENEER_AUDIT_EVENT_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace ingeneer::audit {

// Storage-canonical, ordered: monotonic non-decreasing under promotion (spec §1).
enum class AuthorityClass : int {
    AiProposed = 0,
    Reviewed = 1,
    Approved = 2,
    Certified = 3,
};

enum class ActorKind { Human, Agent };

// One appended chain record (spec §3.1). `payload_json` must be valid JSON; the store
// canonicalizes it on append (recursive sorted keys, frozen separators, scalar tokens
// preserved) so the hashed/stored bytes always match Python's sort_keys re-dump — invalid
// JSON is rejected, never hashed. The chain id ("project_id" in the canonical record) is
// owned by the Store, not the event.
struct Event {
    std::string event_type;
    std::string payload_json = "{}";
    std::string timestamp;  // RFC3339 UTC, injected by caller (determinism, C-4.6)
};

struct AppendResult {
    std::int64_t seq = 0;
    std::string hash;
};

// Entity authority metadata (R-2.1, nine fields). `head_seq` links to the latest promotion.
struct Entity {
    std::string entity_id;
    AuthorityClass authority_class = AuthorityClass::AiProposed;
    std::optional<std::string> source_agent;
    std::string created_by;
    std::string created_at;
    std::optional<std::string> approved_by;
    std::optional<std::string> approved_at;
    std::optional<double> confidence;
    std::string verification_state = "UNVERIFIED";
    std::int64_t head_seq = 0;
};

struct CreateEntityRequest {
    std::string entity_id;
    AuthorityClass initial_class = AuthorityClass::AiProposed;
    std::optional<std::string> source_agent;  // set if AI-originated (R-5.4)
    std::string created_by;
    ActorKind created_by_kind = ActorKind::Agent;
    std::optional<double> confidence;
    std::string timestamp;
};

// A promotion request (spec §3.3). Authority guards apply before insert.
struct PromotionRequest {
    std::string entity_id;
    AuthorityClass to_class = AuthorityClass::Reviewed;
    std::string actor;
    ActorKind actor_kind = ActorKind::Human;
    std::string reason;
    std::int64_t expected_head_seq = 0;  // optimistic concurrency (must equal current head)
    std::string timestamp;
};

}  // namespace ingeneer::audit

#endif  // INGENEER_AUDIT_EVENT_HPP
