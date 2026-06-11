// 3.3 property tests (spec §6): (a) NO sequence of agent-only actions ever yields an
// entity at APPROVED or CERTIFIED — checked both exhaustively over single transitions and
// under seeded-random op sequences, then re-checked AGAINST THE STORAGE ITSELF (every
// entity >= APPROVED must have a human-attributed promotion row); (b) the entity table is
// a pure projection: replaying the promotion log reproduces authority_class and head_seq.
#include <sqlite3.h>

#include <cstdio>
#include <map>
#include <string>

#include "check.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

namespace {

void cleanup(const std::string& p) {
    std::remove(p.c_str());
    std::remove((p + "-wal").c_str());
    std::remove((p + "-shm").c_str());
}

// Deterministic LCG (no RNG dependence in tests — C-4.6 discipline).
struct Lcg {
    std::uint64_t state = 0x2545F4914F6CDD1Dull;
    std::uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(state >> 33);
    }
};

std::int64_t head_of(Store& s, sqlite3* raw, const std::string& id) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(raw, "SELECT head_seq FROM entity WHERE entity_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::int64_t h = (sqlite3_step(st) == SQLITE_ROW) ? sqlite3_column_int64(st, 0) : -1;
    sqlite3_finalize(st);
    (void)s;
    return h;
}

}  // namespace

static void run() {
    const std::string path = "property_authority_test.sqlite";
    cleanup(path);

    auto s = Store::open(path, "proj");
    CHECK(s.has_value());
    auto& store = *s;

    // ---- (1) Exhaustive single-step guard check ----------------------------------------
    // Creation: every (initial_class, actor_kind) pair.
    for (int cls = 0; cls <= 3; ++cls) {
        for (int kind = 0; kind <= 1; ++kind) {
            const bool human = kind == 0;
            CreateEntityRequest c{};
            c.entity_id = "X" + std::to_string(cls) + std::to_string(kind);
            c.initial_class = static_cast<AuthorityClass>(cls);
            c.created_by = human ? "PLS:jane" : "agent:bot";
            c.created_by_kind = human ? ActorKind::Human : ActorKind::Agent;
            c.timestamp = "2026-06-11T00:00:00+00:00";
            const bool allowed = human || cls < 2;  // agents only below APPROVED
            CHECK_EQ(store.create_entity(c).has_value(), allowed);
        }
    }
    // Promotion: from every reachable class, to every higher class, by each actor kind.
    int n = 0;
    for (int from = 0; from <= 2; ++from) {
        for (int to = from + 1; to <= 3; ++to) {
            for (int kind = 0; kind <= 1; ++kind) {
                const bool human = kind == 0;
                const std::string id = "P" + std::to_string(n++);
                CreateEntityRequest c{};
                c.entity_id = id;
                c.initial_class = static_cast<AuthorityClass>(from);
                // set up the starting state legally
                c.created_by = from >= 2 ? "PLS:jane" : "agent:bot";
                c.created_by_kind = from >= 2 ? ActorKind::Human : ActorKind::Agent;
                c.timestamp = "2026-06-11T00:00:00+00:00";
                auto created = store.create_entity(c);
                CHECK(created.has_value());

                PromotionRequest p{};
                p.entity_id = id;
                p.to_class = static_cast<AuthorityClass>(to);
                p.actor = human ? "PLS:jane" : "agent:bot";
                p.actor_kind = human ? ActorKind::Human : ActorKind::Agent;
                p.expected_head_seq = created->seq;
                p.timestamp = "2026-06-11T00:01:00+00:00";
                const bool allowed = human || to < 2;  // agents never reach APPROVED+
                CHECK_EQ(store.promote(p).has_value(), allowed);
            }
        }
    }

    // ---- (2) Seeded-random agent-only op storm -----------------------------------------
    // 500 random ops by AGENTS ONLY; afterwards no entity may sit at APPROVED/CERTIFIED.
    Lcg rng;
    for (int i = 0; i < 500; ++i) {
        const std::string id = "R" + std::to_string(rng.next() % 40);
        if ((rng.next() & 1u) == 0u) {
            CreateEntityRequest c{};
            c.entity_id = id;
            c.initial_class = static_cast<AuthorityClass>(rng.next() % 4);  // tries all!
            c.created_by = "agent:bot";
            c.created_by_kind = ActorKind::Agent;
            c.timestamp = "2026-06-11T00:02:00+00:00";
            (void)store.create_entity(c);  // may legitimately fail; guard is what matters
        } else {
            PromotionRequest p{};
            p.entity_id = id;
            p.to_class = static_cast<AuthorityClass>(rng.next() % 4);
            p.actor = "agent:bot";
            p.actor_kind = ActorKind::Agent;
            p.timestamp = "2026-06-11T00:03:00+00:00";
            // use the true head so the concurrency guard never masks the authority guard
            sqlite3* raw = nullptr;
            sqlite3_open(path.c_str(), &raw);
            p.expected_head_seq = head_of(store, raw, id);
            sqlite3_close(raw);
            (void)store.promote(p);
        }
    }

    // ---- (3) Storage-level invariants over EVERYTHING above ----------------------------
    sqlite3* raw = nullptr;
    CHECK_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);

    // (3a) Every entity at APPROVED+ has a human-attributed promotion reaching that class.
    {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(raw,
                           "SELECT COUNT(*) FROM entity e WHERE e.authority_class >= 2 AND"
                           " NOT EXISTS (SELECT 1 FROM promotion p WHERE"
                           "   p.entity_id = e.entity_id AND p.actor_kind = 'human' AND"
                           "   p.to_class >= e.authority_class)",
                           -1, &st, nullptr);
        CHECK_EQ(sqlite3_step(st), SQLITE_ROW);
        CHECK_EQ(sqlite3_column_int64(st, 0), static_cast<std::int64_t>(0));
        sqlite3_finalize(st);
    }
    // (3b) No agent-only "R*" entity (op storm) reached APPROVED+ at all.
    {
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(raw,
                           "SELECT COUNT(*) FROM entity WHERE entity_id LIKE 'R%' AND"
                           " authority_class >= 2",
                           -1, &st, nullptr);
        CHECK_EQ(sqlite3_step(st), SQLITE_ROW);
        CHECK_EQ(sqlite3_column_int64(st, 0), static_cast<std::int64_t>(0));
        sqlite3_finalize(st);
    }
    // (3c) Projection equality: replaying the promotion log per entity reproduces
    // authority_class and head_seq exactly.
    {
        std::map<std::string, std::pair<int, std::int64_t>> replay;  // id -> (class, head)
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(raw, "SELECT seq, entity_id, to_class FROM promotion ORDER BY seq", -1,
                           &st, nullptr);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const std::string id = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            replay[id] = {sqlite3_column_int(st, 2), sqlite3_column_int64(st, 0)};
        }
        sqlite3_finalize(st);

        sqlite3_stmt* en = nullptr;
        sqlite3_prepare_v2(raw, "SELECT entity_id, authority_class, head_seq FROM entity", -1, &en,
                           nullptr);
        std::int64_t entities = 0;
        while (sqlite3_step(en) == SQLITE_ROW) {
            ++entities;
            const std::string id = reinterpret_cast<const char*>(sqlite3_column_text(en, 0));
            const auto it = replay.find(id);
            CHECK(it != replay.end());
            CHECK_EQ(sqlite3_column_int(en, 1), it->second.first);
            CHECK_EQ(sqlite3_column_int64(en, 2), it->second.second);
        }
        sqlite3_finalize(en);
        CHECK(entities > 0);
    }
    sqlite3_close(raw);

    // ---- (4) The chain over everything above still verifies ----------------------------
    CHECK(store.verify_chain().has_value());

    cleanup(path);
}

TEST_MAIN_RUN()
