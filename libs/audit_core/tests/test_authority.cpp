// Entity Authority System guards (C-1.1, R-2.2/2.3/2.4): the storage layer — not the UI —
// refuses AI-origin certification and excludes non-CERTIFIED entities from the snapshot.
#include <cstdio>
#include <string>

#include "check.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

static void run() {
    const std::string path = "audit_authority_test.sqlite";
    std::remove(path.c_str());

    auto s = Store::open(path, "proj");
    CHECK(s.has_value());
    auto& store = *s;

    // AI proposes geometry (R-5.4): created as AI_PROPOSED by an agent.
    CreateEntityRequest c{};
    c.entity_id = "E1";
    c.initial_class = AuthorityClass::AiProposed;
    c.source_agent = "opus-4-8";
    c.created_by = "agent:opus-4-8";
    c.created_by_kind = ActorKind::Agent;
    c.confidence = 0.82;
    c.timestamp = "2026-06-11T00:00:00+00:00";
    auto created = store.create_entity(c);
    CHECK(created.has_value());
    std::int64_t head = created->seq;

    // An agent may move it to REVIEWED (intermediate state).
    PromotionRequest toRev{};
    toRev.entity_id = "E1";
    toRev.to_class = AuthorityClass::Reviewed;
    toRev.actor = "agent:opus-4-8";
    toRev.actor_kind = ActorKind::Agent;
    toRev.reason = "auto-review";
    toRev.expected_head_seq = head;
    toRev.timestamp = "2026-06-11T00:01:00+00:00";
    auto rev = store.promote(toRev);
    CHECK(rev.has_value());
    head = rev->seq;

    // GUARD: an agent MUST NOT approve (R-2.3). Storage rejects it.
    PromotionRequest agentApprove{};
    agentApprove.entity_id = "E1";
    agentApprove.to_class = AuthorityClass::Approved;
    agentApprove.actor = "agent:opus-4-8";
    agentApprove.actor_kind = ActorKind::Agent;
    agentApprove.expected_head_seq = head;
    agentApprove.timestamp = "2026-06-11T00:02:00+00:00";
    auto bad = store.promote(agentApprove);
    CHECK(!bad.has_value());
    if (!bad) CHECK_EQ(bad.error().code, AuditErrc::AuthorityViolation);

    // GUARD: stale head_seq => ConcurrencyConflict.
    PromotionRequest stale{};
    stale.entity_id = "E1";
    stale.to_class = AuthorityClass::Approved;
    stale.actor = "PLS:jane.doe";
    stale.actor_kind = ActorKind::Human;
    stale.expected_head_seq = head - 1;  // wrong
    stale.timestamp = "2026-06-11T00:02:30+00:00";
    auto conflict = store.promote(stale);
    CHECK(!conflict.has_value());
    if (!conflict) CHECK_EQ(conflict.error().code, AuditErrc::ConcurrencyConflict);

    // A human (licensed professional) approves, then certifies.
    PromotionRequest humanApprove{};
    humanApprove.entity_id = "E1";
    humanApprove.to_class = AuthorityClass::Approved;
    humanApprove.actor = "PLS:jane.doe";
    humanApprove.actor_kind = ActorKind::Human;
    humanApprove.expected_head_seq = head;
    humanApprove.timestamp = "2026-06-11T00:03:00+00:00";
    auto appr = store.promote(humanApprove);
    CHECK(appr.has_value());
    head = appr->seq;

    PromotionRequest humanCertify{};
    humanCertify.entity_id = "E1";
    humanCertify.to_class = AuthorityClass::Certified;
    humanCertify.actor = "PLS:jane.doe";
    humanCertify.actor_kind = ActorKind::Human;
    humanCertify.expected_head_seq = head;
    humanCertify.timestamp = "2026-06-11T00:04:00+00:00";
    auto cert = store.promote(humanCertify);
    CHECK(cert.has_value());

    auto cls = store.authority_of("E1");
    CHECK(cls.has_value());
    CHECK(*cls == AuthorityClass::Certified);

    // A second entity stays AI_PROPOSED and must be EXCLUDED from the snapshot (R-2.4).
    CreateEntityRequest c2{};
    c2.entity_id = "E2";
    c2.initial_class = AuthorityClass::AiProposed;
    c2.source_agent = "opus-4-8";
    c2.created_by = "agent:opus-4-8";
    c2.created_by_kind = ActorKind::Agent;
    c2.timestamp = "2026-06-11T00:05:00+00:00";
    CHECK(store.create_entity(c2).has_value());

    auto snap = store.certified_snapshot();
    CHECK(snap.has_value());
    CHECK_EQ(snap->size(), static_cast<std::size_t>(1));
    if (snap->size() == 1) {
        CHECK_EQ((*snap)[0].entity_id, std::string("E1"));
        CHECK_EQ((*snap)[0].approved_by.value_or(""), std::string("PLS:jane.doe"));
    }

    // GUARD: an agent cannot create an entity directly at CERTIFIED.
    CreateEntityRequest c3{};
    c3.entity_id = "E3";
    c3.initial_class = AuthorityClass::Certified;
    c3.created_by = "agent:opus-4-8";
    c3.created_by_kind = ActorKind::Agent;
    c3.timestamp = "2026-06-11T00:06:00+00:00";
    auto bad3 = store.create_entity(c3);
    CHECK(!bad3.has_value());
    if (!bad3) CHECK_EQ(bad3.error().code, AuditErrc::AuthorityViolation);

    CHECK(store.verify_chain().has_value());

    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

TEST_MAIN_RUN()
