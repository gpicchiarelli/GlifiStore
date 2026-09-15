#include "glyphastore/store/store.hpp"
#include "test.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto bytes(const std::string_view value) -> std::span<const std::byte> {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

} // namespace

GLYPHA_TEST("paired Store erase_batch removes keys and keeps RAW visibility") {
    auto opened = glyphastore::Store::open({.worker_config = {.explicit_count = 2}});
    GLYPHA_REQUIRE(opened.has_value());
    auto& store = **opened;

    std::vector<std::string> keys;
    std::vector<std::string> values;
    keys.reserve(64);
    values.reserve(64);
    std::vector<glyphastore::Store::PutItem> puts;
    puts.reserve(64);
    for (int index = 0; index < 64; ++index) {
        keys.push_back("erase-batch-key-" + std::to_string(index));
        values.push_back("erase-batch-value-" + std::to_string(index));
        puts.push_back(glyphastore::Store::PutItem{
            .key = keys.back(),
            .value = bytes(values.back()),
        });
    }
    const auto put_statuses = store.put_batch(puts);
    GLYPHA_REQUIRE(put_statuses.size() == puts.size());
    for (const auto& status : put_statuses) {
        GLYPHA_REQUIRE(status.has_value());
    }

    std::vector<glyphastore::Store::EraseItem> erases;
    erases.reserve(keys.size());
    for (const auto& key : keys) {
        erases.push_back(glyphastore::Store::EraseItem{.key = key});
    }
    const auto erase_statuses = store.erase_batch(erases);
    GLYPHA_REQUIRE(erase_statuses.size() == erases.size());
    for (const auto& status : erase_statuses) {
        GLYPHA_REQUIRE(status.has_value());
    }
    for (const auto& key : keys) {
        const auto got = store.get(key);
        GLYPHA_REQUIRE(!got.has_value());
        GLYPHA_REQUIRE(got.error().code == glyphastore::ErrorCode::not_found);
    }
    GLYPHA_REQUIRE(store.close().has_value());
}

GLYPHA_TEST("paired Store erase_batch preserves same-key FIFO within one batch") {
    auto opened = glyphastore::Store::open({.worker_config = {.explicit_count = 1}});
    GLYPHA_REQUIRE(opened.has_value());
    auto& store = **opened;
    const std::string key = "erase-same-key";
    const std::string value = "present";
    GLYPHA_REQUIRE(store.put(key, bytes(value)).has_value());
    const std::vector<glyphastore::Store::EraseItem> items{
        {.key = key},
        {.key = key},
    };
    const auto statuses = store.erase_batch(items);
    GLYPHA_REQUIRE(statuses.size() == 2);
    GLYPHA_REQUIRE(statuses[0].has_value());
    // Second erase observes the first: missing key is a rejected erase, not a success.
    GLYPHA_REQUIRE(!statuses[1].has_value());
    GLYPHA_REQUIRE(statuses[1].error().code == glyphastore::ErrorCode::not_found);
    const auto got = store.get(key);
    GLYPHA_REQUIRE(!got.has_value());
    GLYPHA_REQUIRE(got.error().code == glyphastore::ErrorCode::not_found);
    GLYPHA_REQUIRE(store.close().has_value());
}

GLYPHA_TEST("legacy_mutex Store erase_batch removes keys and preserves same-key FIFO") {
    auto opened = glyphastore::Store::open({.worker_config = {.explicit_count = 2},
                                            .concurrency = glyphastore::StoreConcurrencyMode::legacy_mutex});
    GLYPHA_REQUIRE(opened.has_value());
    auto& store = **opened;

    std::vector<std::string> keys;
    std::vector<glyphastore::Store::PutItem> puts;
    keys.reserve(16);
    puts.reserve(16);
    for (int index = 0; index < 16; ++index) {
        keys.push_back("legacy-erase-batch-" + std::to_string(index));
        puts.push_back(glyphastore::Store::PutItem{.key = keys.back(), .value = bytes("v")});
    }
    const auto put_statuses = store.put_batch(puts);
    GLYPHA_REQUIRE(put_statuses.size() == puts.size());
    for (const auto& status : put_statuses) {
        GLYPHA_REQUIRE(status.has_value());
    }

    std::vector<glyphastore::Store::EraseItem> erases;
    erases.reserve(keys.size());
    for (const auto& key : keys) {
        erases.push_back(glyphastore::Store::EraseItem{.key = key});
    }
    const auto erase_statuses = store.erase_batch(erases);
    GLYPHA_REQUIRE(erase_statuses.size() == erases.size());
    for (const auto& status : erase_statuses) {
        GLYPHA_REQUIRE(status.has_value());
    }
    for (const auto& key : keys) {
        const auto got = store.get(key);
        GLYPHA_REQUIRE(!got.has_value());
        GLYPHA_REQUIRE(got.error().code == glyphastore::ErrorCode::not_found);
    }

    const std::string same = "legacy-erase-same-key";
    GLYPHA_REQUIRE(store.put(same, bytes("present")).has_value());
    const std::vector<glyphastore::Store::EraseItem> same_key_erases{{.key = same}, {.key = same}};
    const auto same_statuses = store.erase_batch(same_key_erases);
    GLYPHA_REQUIRE(same_statuses.size() == 2);
    GLYPHA_REQUIRE(same_statuses[0].has_value());
    GLYPHA_REQUIRE(!same_statuses[1].has_value());
    GLYPHA_REQUIRE(same_statuses[1].error().code == glyphastore::ErrorCode::not_found);
    GLYPHA_REQUIRE(store.close().has_value());
}
