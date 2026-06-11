#include "check.hpp"
#include "ingeneer/audit/canonical.hpp"
#include "ingeneer/audit/sha256.hpp"

#include <string>

using namespace ingeneer::audit;

static void run() {
    CHECK_EQ(json_escape("a\"b\\c"), std::string("a\\\"b\\\\c"));
    CHECK_EQ(json_escape(std::string("x\ny")), std::string("x\\ny"));

    // Frozen record shape: sorted keys, Python-default separators, genesis prev_hash.
    const std::string genesis(64, '0');
    const std::string rec =
        canonical_record(1, "2026-06-11T00:00:00+00:00", "proj", "ENTITY_CREATED", "{}", genesis);
    CHECK_EQ(rec, std::string("{\"data\": {}, \"event\": \"ENTITY_CREATED\", \"prev_hash\": \"") +
                      genesis +
                      "\", \"project_id\": \"proj\", \"seq\": 1, "
                      "\"timestamp\": \"2026-06-11T00:00:00+00:00\"}");

    // Cross-language ORACLE (spec §6): these hashes were produced by orchestrator audit.py's
    // json.dumps(sort_keys=True) + hashlib.sha256. Byte-parity is pinned here.
    CHECK_EQ(sha256_hex(rec),
             std::string("28d2256f2893fec2714c020ee24987188cd40e8361cce9c6d21635a260db06c4"));
    const std::string rec2 = canonical_record(
        2, "2026-06-11T00:01:00+00:00", "proj", "PROMOTION",
        "{\"entity_id\": \"E1\", \"from_class\": 0, \"to_class\": 1}", std::string(64, 'a'));
    CHECK_EQ(sha256_hex(rec2),
             std::string("467fc0e83d2574adb277d15fc0d44488bf34241e046110edc5062f46ace92308"));
}

TEST_MAIN_RUN()
