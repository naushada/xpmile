// Phase C, Step C.4 — SHA-256 digest streaming + parse_digest_string.
// 8 tests with the `Digest*` prefix.

#include "digest.hpp"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

namespace {

using runcpull::Digest;
using runcpull::parse_digest_string;

// ── DigestTest ────────────────────────────────────────────────────────────

TEST(DigestTest, SHA256_OfEmptyString_MatchesKnownHex) {
  Digest d;
  // No update() calls — finalize on the empty input.
  const std::string hex = d.finalize();
  EXPECT_EQ(hex, runcpull::kEmptySha256Hex);
}

TEST(DigestTest, SHA256_OfKnownBytes_MatchesKnownHex) {
  // sha256("abc") is one of the canonical NIST test vectors.
  Digest d;
  d.update("abc");
  const std::string hex = d.finalize();
  EXPECT_EQ(
      hex,
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(DigestTest, Streaming_NChunks_EqualsOneShot) {
  // Same input split three different ways must produce the same digest.
  const std::string payload =
      "Phase C of the runc-pull TDD plan exercises digest streaming.";

  Digest one_shot;
  one_shot.update(payload);
  const std::string h1 = one_shot.finalize();

  Digest two_chunks;
  two_chunks.update(payload.data(), 10);
  two_chunks.update(payload.data() + 10, payload.size() - 10);
  const std::string h2 = two_chunks.finalize();

  Digest many_chunks;
  for (char c : payload) {
    many_chunks.update(&c, 1);
  }
  const std::string h3 = many_chunks.finalize();

  EXPECT_EQ(h1, h2);
  EXPECT_EQ(h2, h3);
}

TEST(DigestTest, Streaming_LargePayload_NoOverflow) {
  // 4 MiB in 4 KiB chunks — well past a single update() — must produce
  // the same digest as a single update over the whole buffer. This
  // guards against off-by-one / unsigned-overflow regressions in the
  // length accumulator.
  const std::size_t total = 4 * 1024 * 1024;
  std::vector<unsigned char> buf(total);
  std::mt19937 rng(0xCA11AB1Eu);
  for (auto &b : buf) b = static_cast<unsigned char>(rng() & 0xFFu);

  Digest one_shot;
  one_shot.update(buf.data(), buf.size());
  const std::string h_oneshot = one_shot.finalize();

  Digest chunked;
  const std::size_t chunk = 4096;
  for (std::size_t off = 0; off < total; off += chunk) {
    std::size_t n = std::min(chunk, total - off);
    chunked.update(buf.data() + off, n);
  }
  const std::string h_chunked = chunked.finalize();

  EXPECT_EQ(h_oneshot, h_chunked);
  EXPECT_EQ(h_oneshot.size(), 64u);
}

TEST(DigestTest, Mismatch_DetectedOnFinalize) {
  // verify() is the production-side path the orchestrator uses.
  // A digester that fed bytes for "abc" must verify true against the
  // known "abc" digest and false against any other.
  Digest matching;
  matching.update("abc");
  EXPECT_TRUE(matching.verify(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  Digest mismatching;
  mismatching.update("abc");
  EXPECT_FALSE(mismatching.verify(
      // single character flipped at the end
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae"));
}

TEST(DigestTest, ParseDigestString_ExtractsAlgAndHex) {
  const std::string s =
      "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  auto parsed = parse_digest_string(s);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->alg, "sha256");
  EXPECT_EQ(
      parsed->hex,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  // Upper-case hex tolerated, normalised to lower-case.
  auto upper = parse_digest_string(
      "SHA256:E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
  ASSERT_TRUE(upper.has_value());
  EXPECT_EQ(upper->alg, "sha256");
  EXPECT_EQ(
      upper->hex,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(DigestTest, ParseDigestString_RejectsBadHex) {
  // Too short.
  EXPECT_FALSE(parse_digest_string("sha256:abc").has_value());
  // Too long.
  EXPECT_FALSE(
      parse_digest_string(
          "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85500")
          .has_value());
  // Non-hex character.
  EXPECT_FALSE(
      parse_digest_string(
          "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85g")
          .has_value());
  // Empty hex.
  EXPECT_FALSE(parse_digest_string("sha256:").has_value());
  // Missing colon.
  EXPECT_FALSE(
      parse_digest_string(
          "sha256e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
          .has_value());
  // Missing algorithm.
  EXPECT_FALSE(
      parse_digest_string(
          ":e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
          .has_value());
  // Empty string.
  EXPECT_FALSE(parse_digest_string("").has_value());
  // Extra colon — `sha256:abc...:tail` is not a legal digest reference.
  EXPECT_FALSE(
      parse_digest_string(
          "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855:tail")
          .has_value());
}

TEST(DigestTest, ParseDigestString_RejectsUnsupportedAlg_Sha512) {
  // 128 hex chars — valid sha512 length, but alg is rejected.
  const std::string s =
      "sha512:"
      "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
      "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e";
  EXPECT_FALSE(parse_digest_string(s).has_value());

  // Also reject sha1 / md5 / unknown.
  EXPECT_FALSE(parse_digest_string("sha1:da39a3ee5e6b4b0d3255bfef95601890afd80709")
                   .has_value());
  EXPECT_FALSE(parse_digest_string("md5:d41d8cd98f00b204e9800998ecf8427e")
                   .has_value());
  EXPECT_FALSE(
      parse_digest_string(
          "blake3:af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262")
          .has_value());
}

} // namespace
