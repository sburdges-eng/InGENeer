// SPDX-License-Identifier: Apache-2.0
// FIPS 180-4 SHA-256 against NIST vectors + file hash.
#include "ingeneer/pointcloud/sha256.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "check.hpp"

using namespace ingeneer::pointcloud;

static void run() {
    CHECK_EQ(sha256_hex(""),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "ingeneer_sha256_probe.bin";
    {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        CHECK(f != nullptr);
        std::fwrite("abc", 1, 3, f);
        std::fclose(f);
    }
    auto fh = sha256_file_hex(p);
    CHECK(fh.has_value());
    CHECK_EQ(*fh, sha256_hex("abc"));
    fs::remove(p);

    auto missing = sha256_file_hex(fs::temp_directory_path() / "ingeneer_does_not_exist.bin");
    CHECK(!missing.has_value());
    CHECK_EQ(missing.error().code, OctreeErrc::Io);
}

TEST_MAIN_RUN()
