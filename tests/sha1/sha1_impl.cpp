#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aero/detail/impl/sha1.hpp"

namespace {

  using aero::detail::sha1;

  std::span<const std::byte> as_bytes(std::string_view text) {
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(text.data()), text.size()};
  }

  struct sha1_test_vector {
    std::string_view input;
    std::string_view expected_hex;
  };

  constexpr std::array<sha1_test_vector, 5> sha1_vectors{
    sha1_test_vector{.input = "", .expected_hex = "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
    sha1_test_vector{.input = "abc", .expected_hex = "a9993e364706816aba3e25717850c26c9cd0d89d"},
    sha1_test_vector{.input = "message digest", .expected_hex = "c12252ceda8be8994d5fa0290a47231c1d16aae3"},
    sha1_test_vector{.input = "123hjkqdwhjkidas98", .expected_hex = "2b06d83cb291b73002ece0467096019f25743a37"},
    sha1_test_vector{.input = "hello from aero library!", .expected_hex = "7c1254133f0b01bd23f0c042855d412ce80d2ff8"},
  };

  std::vector<std::byte> make_pattern_bytes(std::size_t size) {
    auto data = std::vector<std::byte>{};
    data.reserve(size);

    for (std::size_t i{}; i < size; ++i) {
      data.push_back(static_cast<std::byte>(i * 10U));
    }

    return data;
  }

  std::vector<std::byte> make_zero_bytes(std::size_t size) {
    return {size, std::byte{0}};
  }

} // namespace

TEST(Sha1Impl, ProducesExpectedHexDigestForBasicInput) {
  constexpr std::string_view input{"hello aero!"};
  constexpr std::string_view expected_hex{"201ea2e5633eeee279dd5c0be6e127473431ab81"};

  EXPECT_EQ(expected_hex, sha1::hash_to_hex(input));
  EXPECT_EQ(expected_hex, sha1::hash_to_hex(as_bytes(input)));
}

TEST(Sha1Impl, MatchesKnownTestVectors) {
  for (const auto& test_vector : sha1_vectors) {
    SCOPED_TRACE(test_vector.input);

    EXPECT_EQ(test_vector.expected_hex, sha1::hash_to_hex(test_vector.input));
    EXPECT_EQ(test_vector.expected_hex, sha1::hash_to_hex(as_bytes(test_vector.input)));

    const auto expected_digest = sha1::hash(test_vector.input);

    EXPECT_EQ(expected_digest, sha1::hash(test_vector.input));
    EXPECT_EQ(expected_digest, sha1::hash(as_bytes(test_vector.input)));
  }
}

TEST(Sha1Impl, ProducesSameDigestForStringViewAndByteSpan) {
  constexpr std::string_view input{"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"};

  EXPECT_EQ(sha1::hash_to_hex(input), sha1::hash_to_hex(as_bytes(input)));
  EXPECT_EQ(sha1::hash(input), sha1::hash(as_bytes(input)));
}

TEST(Sha1Impl, WritesDigestIntoProvidedBuffer) {
  constexpr std::string_view input{"hello aero!"};

  const auto expected = sha1::hash(input);

  sha1 hasher;
  hasher.update(input);

  std::array<std::byte, sha1::digest_size> written_digest{};
  hasher.final(written_digest);

  EXPECT_EQ(expected, written_digest);
}

TEST(Sha1Impl, SupportsIncrementalUpdatesAcrossBlockBoundaries) {
  auto data = make_pattern_bytes(512);
  std::span<const std::byte> full_span{data};

  const auto expected_digest = sha1::hash(full_span);
  const auto expected_hex = sha1::hash_to_hex(full_span);

  const std::array<std::size_t, 10> split_points{0, 1, 2, 3, 63, 64, 65, 127, 128, 255};

  for (const auto split_point : split_points) {
    SCOPED_TRACE(split_point);

    sha1 hasher;
    hasher.update({data.data(), split_point});
    hasher.update({data.data() + split_point, data.size() - split_point});

    EXPECT_EQ(expected_digest, hasher.final());
  }

  for (const auto split_point : split_points) {
    SCOPED_TRACE(split_point);

    sha1 hasher;
    hasher.update({data.data(), split_point});
    hasher.update({data.data() + split_point, data.size() - split_point});

    EXPECT_EQ(expected_hex, hasher.final_hex());
  }
}

TEST(Sha1Impl, EmptyUpdateDoesNotAffectDigest) {
  constexpr std::string_view input{"abc"};
  constexpr std::string_view expected_hex{"a9993e364706816aba3e25717850c26c9cd0d89d"};

  sha1 hasher;
  hasher.update(input);

  std::array<std::byte, 0> empty{};
  hasher.update(empty);

  EXPECT_EQ(expected_hex, hasher.final_hex());
}

TEST(Sha1Impl, HashesZeroFilledBuffersAtBlockBoundaries) {
  auto zeros_63 = make_zero_bytes(63);
  auto zeros_64 = make_zero_bytes(64);
  auto zeros_65 = make_zero_bytes(65);

  EXPECT_EQ("0b8bf9fc37ad802cefa6733ec62b09d5f43a1b75", sha1::hash_to_hex(zeros_63));
  EXPECT_EQ("c8d7d0ef0eedfa82d2ea1aa592845b9a6d4b02b7", sha1::hash_to_hex(zeros_64));
  EXPECT_EQ("f0fa45906bd0f4c3668fcd0d8f68d4b298b30e5b", sha1::hash_to_hex(zeros_65));
}

TEST(Sha1Impl, TreatsEmbeddedNullBytesAsData) {
  std::string binary{};
  binary.push_back('a');
  binary.push_back('\0');
  binary.push_back('b');
  binary.push_back('\0');
  binary.push_back('c');

  std::string_view binary_view{binary};
  std::span<const std::byte> binary_bytes{reinterpret_cast<const std::byte*>(binary.data()), binary.size()};

  EXPECT_EQ("52aa71588488269464589bd81be624861498ca7b", sha1::hash_to_hex(binary_bytes));
  EXPECT_EQ(sha1::hash_to_hex(binary_view), sha1::hash_to_hex(binary_bytes));
  EXPECT_EQ(sha1::hash(binary_view), sha1::hash(binary_bytes));
}

TEST(Sha1Impl, HashesOneMillionAsCorrectly) {
  std::string million_a;
  million_a.resize(1'000'000, 'a');

  EXPECT_EQ("34aa973cd4c4daa4f61eeb2bdbad27316534016f", sha1::hash_to_hex(std::string_view{million_a}));
}
