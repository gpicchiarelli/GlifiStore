#include "glyphastore/store/store.hpp"
#include "test.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace {

auto bytes(const std::string_view value) -> std::span<const std::byte> {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

auto open_create(const std::filesystem::path& store_path, const glyphastore::StoreConfig& config)
    -> glyphastore::Result<std::unique_ptr<glyphastore::Store>> {
    auto cfg = config;
    cfg.data_directory = store_path;
    cfg.durable_open_mode = glyphastore::DurableOpenMode::create_new;
    return glyphastore::Store::open(cfg);
}

auto open_existing(const std::filesystem::path& store_path, const glyphastore::StorageMode mode)
    -> glyphastore::Result<std::unique_ptr<glyphastore::Store>> {
    return glyphastore::Store::open({.worker_config = {.explicit_count = 2},
                                     .storage_mode = mode,
                                     .data_directory = store_path,
                                     .durable_open_mode = glyphastore::DurableOpenMode::open_existing});
}

auto prove_erase_batch_tombstones_across_reopen(const glyphastore::StoreConfig& create_config,
                                                const glyphastore::StorageMode reopen_mode,
                                                const std::string_view key_prefix) -> void {
    auto pattern = (std::filesystem::temp_directory_path() / "glyphastore-erase-batch-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    GLYPHA_REQUIRE(::mkdtemp(writable.data()) != nullptr);
    const std::filesystem::path root{writable.data()};
    const auto store_path = root / "store";

    {
        auto opened = open_create(store_path, create_config);
        GLYPHA_REQUIRE(opened.has_value());
        auto& store = **opened;

        std::vector<std::string> keys;
        std::vector<glyphastore::Store::PutItem> puts;
        keys.reserve(8);
        puts.reserve(9);
        for (int index = 0; index < 8; ++index) {
            keys.push_back(std::string(key_prefix) + "-batch-" + std::to_string(index));
            puts.push_back(glyphastore::Store::PutItem{.key = keys.back(), .value = bytes("v")});
        }
        const std::string survivor = std::string(key_prefix) + "-survivor";
        puts.push_back(glyphastore::Store::PutItem{.key = survivor, .value = bytes("keep")});
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
        const auto kept = store.get(survivor);
        GLYPHA_REQUIRE(kept.has_value());
        GLYPHA_REQUIRE(std::string_view(reinterpret_cast<const char*>(kept->bytes.data()),
                                        kept->bytes.size()) == "keep");

        const std::string same = std::string(key_prefix) + "-same-key";
        GLYPHA_REQUIRE(store.put(same, bytes("present")).has_value());
        const std::vector<glyphastore::Store::EraseItem> same_key_erases{{.key = same}, {.key = same}};
        const auto same_statuses = store.erase_batch(same_key_erases);
        GLYPHA_REQUIRE(same_statuses.size() == 2);
        GLYPHA_REQUIRE(same_statuses[0].has_value());
        GLYPHA_REQUIRE(!same_statuses[1].has_value());
        GLYPHA_REQUIRE(same_statuses[1].error().code == glyphastore::ErrorCode::not_found);
        GLYPHA_REQUIRE(store.close().has_value());
    }

    {
        auto reopened = open_existing(store_path, reopen_mode);
        GLYPHA_REQUIRE(reopened.has_value());
        auto& store = **reopened;
        for (int index = 0; index < 8; ++index) {
            const auto key = std::string(key_prefix) + "-batch-" + std::to_string(index);
            const auto got = store.get(key);
            GLYPHA_REQUIRE(!got.has_value());
            GLYPHA_REQUIRE(got.error().code == glyphastore::ErrorCode::not_found);
        }
        const auto kept = store.get(std::string(key_prefix) + "-survivor");
        GLYPHA_REQUIRE(kept.has_value());
        GLYPHA_REQUIRE(std::string_view(reinterpret_cast<const char*>(kept->bytes.data()),
                                        kept->bytes.size()) == "keep");
        const auto same = store.get(std::string(key_prefix) + "-same-key");
        GLYPHA_REQUIRE(!same.has_value());
        GLYPHA_REQUIRE(same.error().code == glyphastore::ErrorCode::not_found);
        GLYPHA_REQUIRE(store.close().has_value());
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace

GLYPHA_TEST("paired durable_sync erase_batch persists tombstones across reopen") {
    prove_erase_batch_tombstones_across_reopen(
        {.worker_config = {.explicit_count = 2}, .storage_mode = glyphastore::StorageMode::durable_sync},
        glyphastore::StorageMode::durable_sync, "durable-sync-erase");
}

GLYPHA_TEST("paired durable_group erase_batch persists tombstones across reopen") {
    // Writer-backed durable_group path (async lane + dedicated Writer), not embedded sync-only.
    prove_erase_batch_tombstones_across_reopen(
        {.worker_config = {.explicit_count = 2},
         .paired = {.async_lane_capacity = 8,
                    .async_lane_payload_bytes = 1U * 1024U * 1024U,
                    .reader_epoch_lease = true},
         .storage_mode = glyphastore::StorageMode::durable_group,
         .durable_group = {.max_records = 32, .max_bytes = 65'536, .max_wait_ms = 10, .min_records = 1},
         .maintenance = {.mode = glyphastore::MaintenanceMode::disabled}},
        glyphastore::StorageMode::durable_group, "durable-group-erase");
}

GLYPHA_TEST("paired durable_sync Writer-path erase_batch persists tombstones across reopen") {
    // durable_sync with async_lane_capacity > 0 uses the dedicated Writer path.
    prove_erase_batch_tombstones_across_reopen(
        {.worker_config = {.explicit_count = 2},
         .paired = {.async_lane_capacity = 8,
                    .async_lane_payload_bytes = 1U * 1024U * 1024U,
                    .reader_epoch_lease = true},
         .storage_mode = glyphastore::StorageMode::durable_sync,
         .maintenance = {.mode = glyphastore::MaintenanceMode::disabled}},
        glyphastore::StorageMode::durable_sync, "durable-sync-writer-erase");
}

GLYPHA_TEST("paired durable_periodic erase_batch persists tombstones across reopen") {
    // Large sync_interval so durability comes from close()/shutdown flush, not the timer.
    prove_erase_batch_tombstones_across_reopen(
        {.worker_config = {.explicit_count = 2},
         .storage_mode = glyphastore::StorageMode::durable_periodic,
         .durable_periodic = {.sync_interval_ms = 60'000},
         .maintenance = {.mode = glyphastore::MaintenanceMode::disabled}},
        glyphastore::StorageMode::durable_periodic, "durable-periodic-erase");
}

GLYPHA_TEST("paired durable_periodic Writer-path erase_batch persists tombstones across reopen") {
    prove_erase_batch_tombstones_across_reopen(
        {.worker_config = {.explicit_count = 2},
         .paired = {.async_lane_capacity = 8,
                    .async_lane_payload_bytes = 1U * 1024U * 1024U,
                    .reader_epoch_lease = true},
         .storage_mode = glyphastore::StorageMode::durable_periodic,
         .durable_periodic = {.sync_interval_ms = 60'000},
         .maintenance = {.mode = glyphastore::MaintenanceMode::disabled}},
        glyphastore::StorageMode::durable_periodic, "durable-periodic-writer-erase");
}
