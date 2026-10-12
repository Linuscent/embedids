/*
 * Copyright 2026 Linuscent Ltd
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "core/integrity/hash_util.h"
#include "core/integrity/sha256.h"
#include "support/test_support.h"

using namespace edgeml;

TEST(Sha256Test, KnownVectors) {
  // FIPS 180-4 / NIST CAVP examples.
  EXPECT_EQ(sha256_string(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha256_string("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(
      sha256_string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, MillionAs) {
  const std::string input(1000000, 'a');
  EXPECT_EQ(sha256_string(input),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256Test, IncrementalUpdatesMatchOneShot) {
  const std::string input = "The quick brown fox jumps over the lazy dog";
  edgeml_sha256_ctx_t ctx;
  edgeml_sha256_init(&ctx);
  for (char c : input) {
    edgeml_sha256_update(&ctx, &c, 1);
  }
  uint8_t digest[EDGEML_SHA256_DIGEST_LEN];
  char hex[EDGEML_SHA256_HEX_LEN];
  edgeml_sha256_final(&ctx, digest);
  edgeml_sha256_to_hex(digest, hex);
  EXPECT_EQ(std::string(hex), sha256_string(input));
  EXPECT_EQ(std::string(hex),
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(Sha256Test, HashesFiles) {
  test::TempDir dir;
  const std::string path = dir.file("data.bin");
  {
    std::ofstream out(path, std::ios::binary);
    out << "abc";
  }
  auto hash = sha256_file(path);
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(*hash, sha256_string("abc"));
  EXPECT_FALSE(sha256_file(dir.file("missing")).has_value());
}

TEST(Sha256Test, ShortHash) {
  EXPECT_EQ(short_hash("0123456789abcdef", 4), "0123...");
  EXPECT_EQ(short_hash("abc", 4), "abc");
}
