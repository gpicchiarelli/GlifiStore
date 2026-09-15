#include "glifistore/persistence/compaction.hpp"
#include "glifistore/persistence/compaction_intent.hpp"
#include "glifistore/segment/crc32c.hpp"
#include "glifistore/segment/segment_header.hpp"
#include "hex_fixture.hpp"
#include "test.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace {

inline constexpr std::size_t kChecksumOffset = 56;

void put_u32(const std::span<std::byte> out, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        out[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void refresh_checksum(const std::span<std::byte> bytes) {
    put_u32(bytes, kChecksumOffset, 0);
    put_u32(bytes, kChecksumOffset, glifistore::crc32c(bytes));
}

auto intent_fixture() -> glifistore::DurableCompactionIntent {
    glifistore::Manifest old{
        .store_id = {std::byte{0x61}, std::byte{0x62}, std::byte{0x63}},
        .manifest_generation = 9,
        .worker_count = 1,
        .routing_epoch = 7,
        .next_segment_id = glifistore::SegmentId{5},
        .next_segment_generation = glifistore::GenerationId{1},
        .segments =
            {
                {.segment_id = glifistore::SegmentId{1},
                 .generation = glifistore::GenerationId{1},
                 .owner_worker = glifistore::WorkerId{0},
                 .role = glifistore::ManifestSegmentRole::sealed},
                {.segment_id = glifistore::SegmentId{2},
                 .generation = glifistore::GenerationId{2},
                 .owner_worker = glifistore::WorkerId{0},
                 .role = glifistore::ManifestSegmentRole::sealed},
                {.segment_id = glifistore::SegmentId{3},
                 .generation = glifistore::GenerationId{1},
                 .owner_worker = glifistore::WorkerId{0},
                 .role = glifistore::ManifestSegmentRole::sealed},
                {.segment_id = glifistore::SegmentId{4},
                 .generation = glifistore::GenerationId{1},
                 .owner_worker = glifistore::WorkerId{0},
                 .role = glifistore::ManifestSegmentRole::active},
            },
    };
    auto limits = glifistore::DurableResourceLimits{};
    limits.max_store_bytes = 10ULL * glifistore::kSegmentSizeBytes;
    limits.max_segment_count = 10;
    limits.max_temporary_compaction_bytes = 4ULL * glifistore::kSegmentSizeBytes;
    const auto plan = glifistore::plan_durable_worker_compaction(old, glifistore::WorkerId{0}, 1, limits);
    GLIFI_REQUIRE(plan.has_value());
    return {.worker_id = glifistore::WorkerId{0},
            .old_manifest = std::move(old),
            .next_manifest = plan->next_manifest};
}

} // namespace

GLIFI_TEST("compaction intent v1 matches its independent golden fixture") {
    const auto intent = intent_fixture();
    const auto encoded = glifistore::encode_compaction_intent(intent);
    GLIFI_REQUIRE(encoded.has_value());
    const auto old_size = glifistore::encoded_manifest_size(intent.old_manifest);
    const auto next_size = glifistore::encoded_manifest_size(intent.next_manifest);
    GLIFI_REQUIRE(old_size.has_value());
    GLIFI_REQUIRE(next_size.has_value());
    GLIFI_REQUIRE(encoded->size() == glifistore::kCompactionIntentHeaderBytes + *old_size + *next_size);

    const auto fixture = glifistore::test::read_hex_fixture(std::filesystem::path{GLIFISTORE_SOURCE_DIR} /
                                                            "tests/fixtures/compaction_intent_v1.hex");
    GLIFI_REQUIRE(*encoded == fixture);

    const auto decoded = glifistore::decode_compaction_intent(fixture);
    GLIFI_REQUIRE(decoded.has_value());
    GLIFI_REQUIRE(*decoded == intent);
}

GLIFI_TEST("compaction intent rejects truncation trailing bytes and checksum corruption") {
    auto encoded = glifistore::encode_compaction_intent(intent_fixture());
    GLIFI_REQUIRE(encoded.has_value());

    auto decoded = glifistore::decode_compaction_intent(
        std::span<const std::byte>{*encoded}.first(glifistore::kCompactionIntentHeaderBytes - 1U));
    GLIFI_REQUIRE(!decoded.has_value());
    GLIFI_REQUIRE(decoded.error().code == glifistore::ErrorCode::invalid_record);

    encoded->push_back(std::byte{0});
    decoded = glifistore::decode_compaction_intent(*encoded);
    GLIFI_REQUIRE(!decoded.has_value());
    GLIFI_REQUIRE(decoded.error().code == glifistore::ErrorCode::invalid_record);
    encoded->pop_back();

    (*encoded)[glifistore::kCompactionIntentHeaderBytes] ^= std::byte{1};
    decoded = glifistore::decode_compaction_intent(*encoded);
    GLIFI_REQUIRE(!decoded.has_value());
    GLIFI_REQUIRE(decoded.error().code == glifistore::ErrorCode::checksum_mismatch);
}

GLIFI_TEST("compaction intent binds its header to canonical old and next manifests") {
    auto intent = intent_fixture();
    intent.next_manifest.next_segment_id.value += 1;
    auto encoded = glifistore::encode_compaction_intent(intent);
    GLIFI_REQUIRE(!encoded.has_value());
    GLIFI_REQUIRE(encoded.error().code == glifistore::ErrorCode::invalid_argument);

    encoded = glifistore::encode_compaction_intent(intent_fixture());
    GLIFI_REQUIRE(encoded.has_value());
    (*encoded)[16] ^= std::byte{1};
    refresh_checksum(*encoded);
    const auto decoded = glifistore::decode_compaction_intent(*encoded);
    GLIFI_REQUIRE(!decoded.has_value());
    GLIFI_REQUIRE(decoded.error().code == glifistore::ErrorCode::corrupted_data);
}

GLIFI_TEST("compaction intent fails closed for unknown versions and nonzero reserved bytes") {
    auto encoded = glifistore::encode_compaction_intent(intent_fixture());
    GLIFI_REQUIRE(encoded.has_value());
    (*encoded)[4] = std::byte{2};
    refresh_checksum(*encoded);
    auto decoded = glifistore::decode_compaction_intent(*encoded);
    GLIFI_REQUIRE(!decoded.has_value());
    GLIFI_REQUIRE(decoded.error().code == glifistore::ErrorCode::invalid_record);

    encoded = glifistore::encode_compaction_intent(intent_fixture());
    GLIFI_REQUIRE(encoded.has_value());
    (*encoded)[60] = std::byte{1};
    refresh_checksum(*encoded);
    decoded = glifistore::decode_compaction_intent(*encoded);
    GLIFI_REQUIRE(!decoded.has_value());
    GLIFI_REQUIRE(decoded.error().code == glifistore::ErrorCode::invalid_record);
}
