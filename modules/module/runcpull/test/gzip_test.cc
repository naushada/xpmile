// Phase D.2 — Gzip streaming inflater (5 tests).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <zlib.h>

#include "layer_unpack.hpp"

using runcpull::GunzipAll;
using runcpull::GzipInflater;
using runcpull::gunzip;
using runcpull::UnpackError;

namespace {

// Helper: gzip-compress a string using zlib (so the test fixtures are
// independent of the production GzipInflater under test).
std::string gzip_compress(const std::string &in) {
  z_stream zs{};
  zs.zalloc = Z_NULL; zs.zfree = Z_NULL; zs.opaque = Z_NULL;
  // 16 + 15 = gzip wrapper.
  deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + 15, 8,
                Z_DEFAULT_STRATEGY);
  std::string out;
  std::vector<unsigned char> buf(16384);
  zs.next_in  = reinterpret_cast<unsigned char *>(const_cast<char *>(in.data()));
  zs.avail_in = static_cast<unsigned int>(in.size());
  int rc = Z_OK;
  do {
    zs.next_out  = buf.data();
    zs.avail_out = buf.size();
    rc = deflate(&zs, Z_FINISH);
    out.append(reinterpret_cast<char *>(buf.data()), buf.size() - zs.avail_out);
  } while (rc == Z_OK);
  deflateEnd(&zs);
  return out;
}

}  // namespace

TEST(GzipTest, Decompress_KnownStream_MatchesExpected) {
  const std::string original = "hello, runc-pull";
  const std::string gz       = gzip_compress(original);
  auto out = gunzip(gz);
  EXPECT_EQ(out.error, UnpackError::NONE);
  EXPECT_EQ(out.bytes, original);
}

TEST(GzipTest, Streamed_NChunks_EqualsOneShot) {
  // 100 KB of pseudo-random-looking data so the chunk boundaries actually
  // split gzip frames meaningfully.
  std::string original;
  original.reserve(100 * 1024);
  for (std::size_t i = 0; i < 100 * 1024; ++i) {
    original.push_back(static_cast<char>((i * 1103515245u + 12345u) & 0xff));
  }
  const std::string gz = gzip_compress(original);

  // One-shot.
  std::string one_shot;
  {
    GzipInflater g;
    g.write(gz.data(), gz.size(), one_shot);
  }
  EXPECT_EQ(one_shot, original);

  // N-chunk delivery.
  std::string chunked;
  {
    GzipInflater g;
    constexpr std::size_t step = 137;  // weird step to expose boundary bugs
    for (std::size_t i = 0; i < gz.size(); i += step) {
      std::size_t n = std::min(step, gz.size() - i);
      g.write(gz.data() + i, n, chunked);
    }
  }
  EXPECT_EQ(chunked, original);
}

TEST(GzipTest, TruncatedStream_Errors) {
  const std::string original = "the quick brown fox jumps over the lazy dog";
  std::string gz             = gzip_compress(original);
  gz.resize(gz.size() / 2);  // cut in half
  auto out = gunzip(gz);
  EXPECT_EQ(out.error, UnpackError::GZIP_TRUNCATED);
}

TEST(GzipTest, InvalidMagic_Errors) {
  const std::string nonsense = "this is not gzip data at all!!";
  auto out = gunzip(nonsense);
  EXPECT_NE(out.error, UnpackError::NONE);
}

TEST(GzipTest, Empty_LegalGzipOfEmpty_OK) {
  const std::string empty;
  const std::string gz = gzip_compress(empty);
  auto out = gunzip(gz);
  EXPECT_EQ(out.error, UnpackError::NONE);
  EXPECT_EQ(out.bytes, "");
}
