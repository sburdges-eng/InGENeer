// 3.6 record reader: canonicalization semantics (recursive key sort by decoded string,
// raw scalar preservation), JSONL chain verify, tamper detection, and the C++->C++
// export/verify round trip with non-ASCII content.
#include <cstdio>
#include <fstream>
#include <string>

#include "check.hpp"
#include "ingeneer/audit/jsonl.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

static void cleanup(const std::string& p) {
    std::remove(p.c_str());
    std::remove((p + "-wal").c_str());
    std::remove((p + "-shm").c_str());
}

static void run() {
    // --- json_unescape -----------------------------------------------------------------
    auto u1 = json_unescape("M\\u00fcller\\u2014\\u6d4b\\u8bd5 \\u007f \\ud83d\\ude00");
    CHECK(u1.has_value());
    CHECK_EQ(*u1, std::string(
                      "M\xc3\xbcller\xe2\x80\x94\xe6\xb5\x8b\xe8\xaf\x95 \x7f \xf0\x9f\x98\x80"));
    CHECK(!json_unescape("\\ud83d").has_value());   // lone high surrogate
    CHECK(!json_unescape("\\ude00x").has_value());  // lone low surrogate
    CHECK(!json_unescape("\\x41").has_value());     // invalid escape

    // --- canonicalize_json -------------------------------------------------------------
    // Recursive key sort, ", "/": " separators, scalar tokens byte-preserved.
    auto c1 = canonicalize_json("{\"b\":{\"z\":1.50,\"a\":2},\"a\":[true,null,1e+16]}");
    CHECK(c1.has_value());
    CHECK_EQ(*c1, std::string("{\"a\": [true, null, 1e+16], \"b\": {\"a\": 2, \"z\": 1.50}}"));
    // Sort is by DECODED key: "é" (é, U+00E9) sorts AFTER "z" by code point.
    auto c2 = canonicalize_json("{\"\\u00e9\": 1, \"z\": 2}");
    CHECK(c2.has_value());
    CHECK_EQ(*c2, std::string("{\"z\": 2, \"\\u00e9\": 1}"));
    // Idempotence: canonical(canonical(x)) == canonical(x).
    auto c3 = canonicalize_json(*c1);
    CHECK(c3.has_value());
    CHECK_EQ(*c3, *c1);
    // Malformed inputs are rejected, not mangled.
    CHECK(!canonicalize_json("{\"a\":}").has_value());
    CHECK(!canonicalize_json("{} trailing").has_value());
    CHECK(!canonicalize_json("\"unterminated").has_value());

    // libFuzzer regression (evaluator N4): raw invalid UTF-8 (stray continuation bytes)
    // inside a string must be REJECTED — Python json.loads rejects it, and accepting it
    // breaks lossless re-escaping (the original crash input had 0x9f 0x94 in "hash").
    CHECK(!canonicalize_json("{\"hash\": \"\x9f\x94\"}").has_value());
    CHECK(!parse_jsonl_record("{\"seq\": 1, \"timestamp\": \"t\", \"project_id\": \"p\","
                              " \"event\": \"E\", \"data\": {}, \"prev_hash\": \"00\","
                              " \"hash\": \"\x9f\x94\"}")
               .has_value());

    // Python number-token fixed points (evaluator N1): tokens Python json.dumps would
    // re-emit byte-identically are accepted; anything else is rejected.
    for (const char* good : {"0", "-7", "12345678901234567890", "2.5", "0.1", "-0.0", "0.0",
                             "100000.0", "0.0001", "1e+16", "1e-05", "1.2345678901234568e+17"}) {
        CHECK(is_python_number_token(good));
    }
    for (const char* bad : {"-0", "1.50", "0.10", "1e5", "1E5", "1e+5", "1e16", "0.00010",
                            "10000000000000000.0" /* Python: 1e+16 */}) {
        CHECK(!is_python_number_token(bad));
    }
    CHECK(validate_python_number_tokens("{\"a\": [2.5, 1e+16, -7]}").has_value());
    CHECK(!validate_python_number_tokens("{\"a\": [1.50]}").has_value());

    // --- C++ chain -> export_jsonl -> verify_jsonl_chain (with non-ASCII payload) -------
    const std::string db = "jsonl_test.sqlite";
    const std::string jsonl = "jsonl_test.jsonl";
    cleanup(db);
    std::remove(jsonl.c_str());
    {
        auto s = Store::open(db, "proj-\xc3\xa9");  // non-ASCII chain id
        CHECK(s.has_value());
        CHECK(s->append({"NOTE", "{\"note\": \"M\\u00fcller\"}", "2026-06-11T00:00:00+00:00"})
                  .has_value());
        CHECK(s->append({"NOTE", "{\"i\": 2}", "2026-06-11T00:00:01+00:00"}).has_value());
        // Storage-layer N1 guard: a non-fixed-point float token is rejected, never hashed.
        auto badnum = s->append({"NOTE", "{\"x\": 1.50}", "2026-06-11T00:00:02+00:00"});
        CHECK(!badnum.has_value());
        if (!badnum) CHECK_EQ(badnum.error().code, AuditErrc::InvalidArgument);
        CHECK(s->export_jsonl(jsonl).has_value());
    }
    CHECK(verify_jsonl_chain(jsonl).has_value());

    // Tamper with one byte of the payload -> ChainBroken at that record.
    {
        std::ifstream in(jsonl);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        const auto pos = all.find("\"i\": 2");
        CHECK(pos != std::string::npos);
        all[pos + 5] = '3';
        std::ofstream out(jsonl, std::ios::trunc);
        out << all;
    }
    auto tampered = verify_jsonl_chain(jsonl);
    CHECK(!tampered.has_value());
    if (!tampered) CHECK_EQ(tampered.error().code, AuditErrc::ChainBroken);

    cleanup(db);
    std::remove(jsonl.c_str());
}

TEST_MAIN_RUN()
