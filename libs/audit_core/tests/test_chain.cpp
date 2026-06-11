// Chain integrity: append, verify, reopen, and tamper-detection (R-2.6, C-1.2).
#include <sqlite3.h>

#include <cstdio>
#include <string>

#include "check.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

static std::string tmp_path() {
    // Unique-ish temp path; tests run in CTest's isolated cwd.
    return std::string("audit_chain_test.sqlite");
}

static void run() {
    const std::string path = tmp_path();
    std::remove(path.c_str());

    {
        auto s = Store::open(path, "proj");
        CHECK(s.has_value());
        auto& store = *s;

        for (int i = 0; i < 3; ++i) {
            Event ev{"NOTE", "{\"i\": " + std::to_string(i) + "}",
                     "2026-06-11T00:00:0" + std::to_string(i) + "+00:00"};
            auto r = store.append(ev);
            CHECK(r.has_value());
            CHECK_EQ(r->seq, static_cast<std::int64_t>(i + 1));
        }
        auto n = store.event_count();
        CHECK(n.has_value());
        CHECK_EQ(*n, static_cast<std::int64_t>(3));

        auto v = store.verify_chain();
        CHECK(v.has_value());
    }

    // Reopen: chain still verifies from disk.
    {
        auto s = Store::open(path, "proj");
        CHECK(s.has_value());
        CHECK(s->verify_chain().has_value());
    }

    // Tamper: an external writer drops the append-only trigger and rewrites a hash.
    {
        sqlite3* raw = nullptr;
        CHECK_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
        sqlite3_exec(raw, "DROP TRIGGER no_update_event;", nullptr, nullptr, nullptr);
        sqlite3_exec(raw, "UPDATE event SET payload_json='{\"i\":99}' WHERE seq=2;", nullptr,
                     nullptr, nullptr);
        sqlite3_close(raw);

        auto s = Store::open(path, "proj");
        CHECK(s.has_value());
        auto v = s->verify_chain();
        CHECK(!v.has_value());
        if (!v) CHECK_EQ(v.error().code, AuditErrc::ChainBroken);
    }

    std::remove(path.c_str());
    // Also clean WAL/SHM sidecars.
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

TEST_MAIN_RUN()
