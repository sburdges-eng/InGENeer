// Cross-language round-trip helper (spec §6), driven by tests/test_roundtrip_python.py:
//   chain_roundtrip_tool write <db> <jsonl>   build a chain (incl. non-ASCII) and export
//   chain_roundtrip_tool verify <jsonl>       verify a JSONL chain (e.g. Python-written)
#include <cstdio>
#include <cstring>
#include <string>

#include "ingeneer/audit/jsonl.hpp"
#include "ingeneer/audit/store.hpp"

using namespace ingeneer::audit;

namespace {

int do_write(const char* db_path, const char* jsonl_path) {
    auto s = Store::open(db_path, "roundtrip");
    if (!s) {
        std::fprintf(stderr, "open: %s\n", s.error().message.c_str());
        return 1;
    }
    // Includes a non-ASCII payload so the ensure_ascii surface is exercised end-to-end.
    const char* payloads[] = {
        "{\"note\": \"M\\u00fcller\\u2014\\u6d4b\\u8bd5\"}",
        "{\"i\": 2, \"ok\": true}",
        "{\"nested\": {\"b\": 1, \"a\": [1, 2.5, null]}}",
    };
    int i = 0;
    for (const char* p : payloads) {
        Event ev{"NOTE", p, "2026-06-11T00:00:0" + std::to_string(i++) + "+00:00"};
        if (auto r = s->append(ev); !r) {
            std::fprintf(stderr, "append: %s\n", r.error().message.c_str());
            return 1;
        }
    }
    if (auto r = s->export_jsonl(jsonl_path); !r) {
        std::fprintf(stderr, "export: %s\n", r.error().message.c_str());
        return 1;
    }
    return 0;
}

int do_verify(const char* jsonl_path) {
    if (auto r = verify_jsonl_chain(jsonl_path); !r) {
        std::fprintf(stderr, "verify: %s\n", r.error().message.c_str());
        return 1;
    }
    std::printf("chain OK\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "write") == 0) {
        return do_write(argv[2], argv[3]);
    }
    if (argc == 3 && std::strcmp(argv[1], "verify") == 0) {
        return do_verify(argv[2]);
    }
    std::fprintf(stderr, "usage: %s write <db> <jsonl> | verify <jsonl>\n", argv[0]);
    return 2;
}
