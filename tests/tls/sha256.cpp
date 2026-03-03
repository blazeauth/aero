#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aero/tls/sha256.hpp"

namespace {

  using aero::tls::sha256;

  std::span<const std::byte> as_bytes(std::string_view text) {
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(text.data()), text.size()};
  }

  struct sha256_test_vector {
    std::string_view input;
    std::string_view expected_hex;
  };

  constexpr std::array<sha256_test_vector, 5> sha256_vectors{
    sha256_test_vector{
      .input = "",
      .expected_hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    sha256_test_vector{
      .input = "abc",
      .expected_hex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    },
    sha256_test_vector{
      .input = "message digest",
      .expected_hex = "f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650",
    },
    sha256_test_vector{
      .input = "123hjkqdwhjkidas98",
      .expected_hex = "47aa8d9b5bcefeb300d5167794c49b539c38e3ee9b714e8091ad5659ccd56a5a",
    },
    sha256_test_vector{
      .input = "hello from aero library!",
      .expected_hex = "47081341d30eb99c7990b9c469841611e5e61c8904b8eabc08f49a7c5f8a7393",
    },
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

TEST(TlsSha256, ProducesExpectedHexDigestForBasicInput) {
  constexpr std::string_view input{"hello aero!"};
  constexpr std::string_view expected_hex{"8788ef0b2ba1b0a72faf024f733f68c7a667e94b02dedc7c4e5bb8300e32ad5b"};

  EXPECT_EQ(expected_hex, sha256::hash_to_hex(input));
  EXPECT_EQ(expected_hex, sha256::hash_to_hex(as_bytes(input)));
}

TEST(TlsSha256, MatchesKnownTestVectors) {
  for (const auto& test_vector : sha256_vectors) {
    SCOPED_TRACE(test_vector.input);

    EXPECT_EQ(test_vector.expected_hex, sha256::hash_to_hex(test_vector.input));
    EXPECT_EQ(test_vector.expected_hex, sha256::hash_to_hex(as_bytes(test_vector.input)));

    const auto expected_digest = sha256::hash(test_vector.input);

    EXPECT_EQ(expected_digest, sha256::hash(test_vector.input));
    EXPECT_EQ(expected_digest, sha256::hash(as_bytes(test_vector.input)));
  }
}

TEST(TlsSha256, ProducesSameDigestForStringViewAndByteSpan) {
  constexpr std::string_view input{"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"};

  EXPECT_EQ(sha256::hash_to_hex(input), sha256::hash_to_hex(as_bytes(input)));
  EXPECT_EQ(sha256::hash(input), sha256::hash(as_bytes(input)));
}

TEST(TlsSha256, WritesDigestIntoProvidedBuffer) {
  constexpr std::string_view input{"hello aero!"};

  const auto expected = sha256::hash(input);

  sha256 hasher;
  hasher.update(input);

  std::array<std::byte, sha256::digest_size> written_digest{};
  hasher.final(written_digest);

  EXPECT_EQ(expected, written_digest);
}

TEST(TlsSha256, SupportsIncrementalUpdatesAcrossBlockBoundaries) {
  auto data = make_pattern_bytes(512);
  std::span<const std::byte> full_span{data};

  const auto expected_digest = sha256::hash(full_span);
  const auto expected_hex = sha256::hash_to_hex(full_span);

  const std::array<std::size_t, 10> split_points{0, 1, 2, 3, 63, 64, 65, 127, 128, 255};

  for (const auto split_point : split_points) {
    SCOPED_TRACE(split_point);

    sha256 hasher;
    hasher.update({data.data(), split_point});
    hasher.update({data.data() + split_point, data.size() - split_point});

    EXPECT_EQ(expected_digest, hasher.final());
  }

  for (const auto split_point : split_points) {
    SCOPED_TRACE(split_point);

    sha256 hasher;
    hasher.update({data.data(), split_point});
    hasher.update({data.data() + split_point, data.size() - split_point});

    EXPECT_EQ(expected_hex, hasher.final_hex());
  }
}

TEST(TlsSha256, EmptyUpdateDoesNotAffectDigest) {
  constexpr std::string_view input{"abc"};
  constexpr std::string_view expected_hex{"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};

  sha256 hasher;
  hasher.update(input);

  std::array<std::byte, 0> empty{};
  hasher.update(empty);

  EXPECT_EQ(expected_hex, hasher.final_hex());
}

TEST(TlsSha256, HashesZeroFilledBuffersAtBlockBoundaries) {
  auto zeros_63 = make_zero_bytes(63);
  auto zeros_64 = make_zero_bytes(64);
  auto zeros_65 = make_zero_bytes(65);

  EXPECT_EQ("c7723fa1e0127975e49e62e753db53924c1bd84b8ac1ac08df78d09270f3d971", sha256::hash_to_hex(zeros_63));
  EXPECT_EQ("f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b", sha256::hash_to_hex(zeros_64));
  EXPECT_EQ("98ce42deef51d40269d542f5314bef2c7468d401ad5d85168bfab4c0108f75f7", sha256::hash_to_hex(zeros_65));
}

TEST(TlsSha256, TreatsEmbeddedNullBytesAsData) {
  std::string binary{};
  binary.push_back('a');
  binary.push_back('\0');
  binary.push_back('b');
  binary.push_back('\0');
  binary.push_back('c');

  std::string_view binary_view{binary};
  std::span<const std::byte> binary_bytes{reinterpret_cast<const std::byte*>(binary.data()), binary.size()};

  EXPECT_EQ("8badde10c760e9b702defb4b5e225de79c515b1d2a5cfb000e140f3c6fbb5629", sha256::hash_to_hex(binary_bytes));
  EXPECT_EQ(sha256::hash_to_hex(binary_view), sha256::hash_to_hex(binary_bytes));
  EXPECT_EQ(sha256::hash(binary_view), sha256::hash(binary_bytes));
}

TEST(TlsSha256, HashesOneMillionAsCorrectly) {
  std::string million_a;
  million_a.resize(1'000'000, 'a');

  EXPECT_EQ("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
    sha256::hash_to_hex(std::string_view{million_a}));
}
