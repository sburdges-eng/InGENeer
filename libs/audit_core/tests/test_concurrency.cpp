// Spec §6 concurrency obligation (H-21, evaluator N3): two writers on the same database —
// separate Store connections, separate threads — must serialize, never corrupt. Cross-
// connection serialization is BEGIN IMMEDIATE + busy_timeout; in-Store serialization is
// the writer mutex. Run under the tsan preset for the full data-race check.
#include <cstdio>
#include <string>
#include <thread>

#include "check.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

static void cleanup(const std::string& p) {
    std::remove(p.c_str());
    std::remove((p + "-wal").c_str());
    std::remove((p + "-shm").c_str());
}

static void run() {
    const std::string path = "concurrency_test.sqlite";
    cleanup(path);

    constexpr int kPerWriter = 50;
    int ok_a = 0;
    int ok_b = 0;

    {
        auto a = Store::open(path, "proj");
        auto b = Store::open(path, "proj");
        CHECK(a.has_value());
        CHECK(b.has_value());

        auto writer = [](Store& s, const char* tag, int& ok) {
            for (int i = 0; i < kPerWriter; ++i) {
                Event ev{"NOTE",
                         std::string("{\"w\": \"") + tag + "\", \"i\": " + std::to_string(i) + "}",
                         "2026-06-11T00:00:00+00:00"};
                if (s.append(ev)) {
                    ++ok;
                }
            }
        };
        std::thread ta(writer, std::ref(*a), "a", std::ref(ok_a));
        std::thread tb(writer, std::ref(*b), "b", std::ref(ok_b));
        ta.join();
        tb.join();

        // Every append must have succeeded (busy_timeout serializes; nothing is dropped).
        CHECK_EQ(ok_a, kPerWriter);
        CHECK_EQ(ok_b, kPerWriter);

        auto n = a->event_count();
        CHECK(n.has_value());
        CHECK_EQ(*n, static_cast<std::int64_t>(2 * kPerWriter));

        // The interleaved chain is still a single valid hash chain.
        CHECK(a->verify_chain().has_value());
        CHECK(b->verify_chain().has_value());
    }

    cleanup(path);
}

TEST_MAIN_RUN()
