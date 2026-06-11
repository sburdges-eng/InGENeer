// Phase 3.5 — the agent-work chain (plan §2 "one schema, two chains"). The SAME chaining
// code (Store::append) backs a SEPARATE database for agent sessions; it carries NO product
// authority (no entity/promotion rows) and verifies independently of the product chain.
// Consolidation MVP: session events -> appended CONSOLIDATION summary -> handoff event.
#include <cstdio>
#include <string>

#include "check.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

static void cleanup(const std::string& p) {
    std::remove(p.c_str());
    std::remove((p + "-wal").c_str());
    std::remove((p + "-shm").c_str());
}

static void run() {
    const std::string product_path = "agent_product.sqlite";
    const std::string agent_path = "agent_work.sqlite";
    cleanup(product_path);
    cleanup(agent_path);

    // Product chain: a real entity is created (authority-bearing).
    auto product = Store::open(product_path);
    CHECK(product.has_value());
    CreateEntityRequest e{};
    e.entity_id = "E1";
    e.created_by = "agent:opus-4-8";
    e.created_by_kind = ActorKind::Agent;
    e.source_agent = "opus-4-8";
    e.timestamp = "2026-06-11T00:00:00+00:00";
    CHECK(product->create_entity(e).has_value());

    // Agent-work chain: a SEPARATE db. Only append() — never create_entity/promote.
    auto agent = Store::open(agent_path);
    CHECK(agent.has_value());

    CHECK(agent
              ->append({"SESSION_START", "{\"goal\": \"phase-3.5\"}", "sess-1",
                        "2026-06-11T00:00:00+00:00"})
              .has_value());
    CHECK(agent
              ->append({"FEATURE_RESULT", "{\"feature\": \"agent_chain\", \"pass\": true}",
                        "sess-1", "2026-06-11T00:01:00+00:00"})
              .has_value());

    // Consolidation MVP: extract a summary and append it, then a handoff marker.
    auto consolidate = agent->append({"CONSOLIDATION", "{\"events\": 2, \"features_passed\": 1}",
                                      "sess-1", "2026-06-11T00:02:00+00:00"});
    CHECK(consolidate.has_value());
    CHECK(agent
              ->append(
                  {"HANDOFF", "{\"next\": \"phase-3.6\"}", "sess-1", "2026-06-11T00:03:00+00:00"})
              .has_value());

    // Both chains verify independently (offline, R-2.6).
    CHECK(agent->verify_chain().has_value());
    CHECK(product->verify_chain().has_value());

    // Isolation: the agent chain carries NO product authority. Its certified snapshot is
    // empty and it created no entities — agents never certify (C-1.1).
    auto agent_snap = agent->certified_snapshot();
    CHECK(agent_snap.has_value());
    CHECK_EQ(agent_snap->size(), static_cast<std::size_t>(0));

    auto agent_count = agent->event_count();
    CHECK(agent_count.has_value());
    CHECK_EQ(*agent_count, static_cast<std::int64_t>(4));  // start, result, consolidation, handoff

    // The product chain is unaffected by agent-chain activity (separate files).
    auto product_count = product->event_count();
    CHECK(product_count.has_value());
    CHECK_EQ(*product_count, static_cast<std::int64_t>(1));  // only ENTITY_CREATED

    cleanup(product_path);
    cleanup(agent_path);
}

TEST_MAIN_RUN()
