// 3.6 fuzz target for the project-container record reader (every parser gets a fuzzer —
// plan §3.8). Compiled with libFuzzer on a capable toolchain, or with standalone_main.cpp
// as a deterministic CTest under ASan/UBSan.
//
// Invariants under ANY input bytes: no crash/UB/OOB; if canonicalize_json accepts the
// input, re-canonicalizing its own output must succeed and be a fixed point (idempotence);
// if parse_jsonl_record accepts a line, its decoded fields re-escape losslessly.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "ingeneer/audit/canonical.hpp"
#include "ingeneer/audit/jsonl.hpp"

using namespace ingeneer::audit;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string_view in(reinterpret_cast<const char*>(data), size);

    // String unescape never crashes; success or structured error only.
    (void)json_unescape(in);

    // Canonicalization: accepted input must be idempotent under re-canonicalization.
    if (auto c1 = canonicalize_json(in)) {
        auto c2 = canonicalize_json(*c1);
        if (!c2) std::abort();         // canonical output must re-parse
        if (*c2 != *c1) std::abort();  // and be a fixed point
    }

    // Record reader: accepted records re-escape losslessly and re-parse identically.
    if (auto rec = parse_jsonl_record(in)) {
        const std::string rebuilt =
            std::string("{\"seq\": ") + std::to_string(rec->seq) + ", \"timestamp\": \"" +
            json_escape(rec->timestamp) + "\", \"project_id\": \"" + json_escape(rec->project_id) +
            "\", \"event\": \"" + json_escape(rec->event) + "\", \"data\": " + rec->data_canonical +
            ", \"prev_hash\": \"" + json_escape(rec->prev_hash) + "\", \"hash\": \"" +
            json_escape(rec->hash) + "\"}";
        auto rec2 = parse_jsonl_record(rebuilt);
        if (!rec2) std::abort();
        if (rec2->seq != rec->seq || rec2->timestamp != rec->timestamp ||
            rec2->project_id != rec->project_id || rec2->event != rec->event ||
            rec2->data_canonical != rec->data_canonical || rec2->prev_hash != rec->prev_hash ||
            rec2->hash != rec->hash) {
            std::abort();
        }
    }
    return 0;
}
