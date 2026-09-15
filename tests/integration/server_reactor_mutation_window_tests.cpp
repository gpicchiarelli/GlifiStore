#include "glyphastore/server/mutation_window.hpp"
#include "glyphastore/server/protocol.hpp"
#include "glyphastore/server/server.hpp"
#include "server_reactor_test_support.hpp"
#include "test.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace glyphastore::test::server_reactor_support;
using glyphastore::server::kMaximumMutationWindow;

namespace {

[[nodiscard]] auto socket_has_readable(const int socket, const int timeout_ms) -> bool {
    pollfd descriptor{.fd = socket, .events = POLLIN, .revents = 0};
    const int result = ::poll(&descriptor, 1, timeout_ms);
    return result > 0 && (descriptor.revents & POLLIN) != 0;
}

[[nodiscard]] auto expect_ok(const int socket, const std::uint64_t request_id,
                             const std::string_view expected_value = {}) -> bool {
    const auto frame = receive_response(socket);
    const auto response = glyphastore::server::decode_response(frame);
    if (!response.has_value()) {
        return false;
    }
    if (response->frame.request_id != request_id ||
        response->frame.status != glyphastore::server::ResponseStatus::ok) {
        return false;
    }
    if (!expected_value.empty() && text(response->frame.value) != expected_value) {
        return false;
    }
    return true;
}

} // namespace

GLYPHA_TEST("volatile mutation window PUT then GET sees RAW values in FIFO order") {
    auto opened = glyphastore::server::Server::create({.port = 0, .maximum_connections = 1});
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    constexpr std::uint32_t kWindow = 8;
    std::vector<std::byte> pipeline;
    pipeline.reserve(kWindow * 96U);
    for (std::uint32_t index = 0; index < kWindow; ++index) {
        const auto key = "mw-put-" + std::to_string(index);
        const auto value = "mw-val-" + std::to_string(index);
        const auto put = glyphastore::server::encode_request({
            .opcode = glyphastore::server::RequestOpcode::put,
            .request_id = 1'000 + index,
            .key = bytes(key),
            .value = bytes(value),
        });
        GLYPHA_REQUIRE(put.has_value());
        pipeline.insert(pipeline.end(), put->begin(), put->end());
    }
    const auto get_first = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::get,
        .request_id = 2'000,
        .key = bytes("mw-put-0"),
    });
    const auto get_last = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::get,
        .request_id = 2'001,
        .key = bytes("mw-put-7"),
    });
    GLYPHA_REQUIRE(get_first.has_value());
    GLYPHA_REQUIRE(get_last.has_value());
    pipeline.insert(pipeline.end(), get_first->begin(), get_first->end());
    pipeline.insert(pipeline.end(), get_last->begin(), get_last->end());
    GLYPHA_REQUIRE(send_all(socket, pipeline));

    for (std::uint32_t index = 0; index < kWindow; ++index) {
        GLYPHA_REQUIRE(expect_ok(socket, 1'000 + index));
    }
    GLYPHA_REQUIRE(expect_ok(socket, 2'000, "mw-val-0"));
    GLYPHA_REQUIRE(expect_ok(socket, 2'001, "mw-val-7"));

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("volatile mutation window ERASE then GET observes tombstone after barrier") {
    auto opened = glyphastore::server::Server::create({.port = 0, .maximum_connections = 1});
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    const auto put = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::put,
        .request_id = 10,
        .key = bytes("mw-erase"),
        .value = bytes("present"),
    });
    const auto erase = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::erase,
        .request_id = 11,
        .key = bytes("mw-erase"),
    });
    const auto get = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::get,
        .request_id = 12,
        .key = bytes("mw-erase"),
    });
    GLYPHA_REQUIRE(put.has_value());
    GLYPHA_REQUIRE(erase.has_value());
    GLYPHA_REQUIRE(get.has_value());
    std::vector<std::byte> pipeline;
    pipeline.insert(pipeline.end(), put->begin(), put->end());
    pipeline.insert(pipeline.end(), erase->begin(), erase->end());
    pipeline.insert(pipeline.end(), get->begin(), get->end());
    GLYPHA_REQUIRE(send_all(socket, pipeline));

    GLYPHA_REQUIRE(expect_ok(socket, 10));
    GLYPHA_REQUIRE(expect_ok(socket, 11));
    const auto get_frame = receive_response(socket);
    const auto get_response = glyphastore::server::decode_response(get_frame);
    GLYPHA_REQUIRE(get_response.has_value());
    GLYPHA_REQUIRE(get_response->frame.request_id == 12);
    GLYPHA_REQUIRE(get_response->frame.status == glyphastore::server::ResponseStatus::not_found);

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("volatile mutation window admits more than 32 contiguous PUTs across resumes") {
    GLYPHA_REQUIRE(kMaximumMutationWindow == 32);
    auto opened = glyphastore::server::Server::create({.port = 0, .maximum_connections = 1});
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    constexpr std::uint32_t kCount = kMaximumMutationWindow + 1U;
    std::vector<std::byte> pipeline;
    pipeline.reserve(kCount * 96U);
    for (std::uint32_t index = 0; index < kCount; ++index) {
        const auto key = "mw-cap-" + std::to_string(index);
        const auto put = glyphastore::server::encode_request({
            .opcode = glyphastore::server::RequestOpcode::put,
            .request_id = 3'000 + index,
            .key = bytes(key),
            .value = bytes("x"),
        });
        GLYPHA_REQUIRE(put.has_value());
        pipeline.insert(pipeline.end(), put->begin(), put->end());
    }
    GLYPHA_REQUIRE(send_all(socket, pipeline));
    for (std::uint32_t index = 0; index < kCount; ++index) {
        GLYPHA_REQUIRE(expect_ok(socket, 3'000 + index));
    }

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("durable_sync GET barrier waits for open mutation window before response") {
    ServerTemporaryDirectory temporary;
    BlockingFileSync blocker;
    auto opened = glyphastore::server::Server::create(
        {.port = 0, .maximum_connections = 1},
        {.storage_mode = glyphastore::StorageMode::durable_sync,
         .data_directory = temporary.store_path(),
         .durable_open_mode = glyphastore::DurableOpenMode::create_new,
         .filesystem_hooks = {.file_io = {.context = &blocker, .sync_file = &BlockingFileSync::sync_file}}});
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    SyncReleaseGuard release_on_exit{blocker};
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    const auto put = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::put,
        .request_id = 40,
        .key = bytes("mw-barrier"),
        .value = bytes("held"),
    });
    const auto get = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::get,
        .request_id = 41,
        .key = bytes("mw-barrier"),
    });
    GLYPHA_REQUIRE(put.has_value());
    GLYPHA_REQUIRE(get.has_value());
    std::vector<std::byte> pipeline;
    pipeline.insert(pipeline.end(), put->begin(), put->end());
    pipeline.insert(pipeline.end(), get->begin(), get->end());

    blocker.arm();
    GLYPHA_REQUIRE(send_all(socket, pipeline));
    GLYPHA_REQUIRE(blocker.wait_until_blocked());
    // While the PUT window is in flight, GET must not produce a response frame.
    GLYPHA_REQUIRE(!socket_has_readable(socket, 100));
    blocker.release();

    GLYPHA_REQUIRE(expect_ok(socket, 40));
    GLYPHA_REQUIRE(expect_ok(socket, 41, "held"));

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("durable_sync PING drains open mutation window like GET barrier") {
    ServerTemporaryDirectory temporary;
    BlockingFileSync blocker;
    auto opened = glyphastore::server::Server::create(
        {.port = 0, .maximum_connections = 1},
        {.storage_mode = glyphastore::StorageMode::durable_sync,
         .data_directory = temporary.store_path(),
         .durable_open_mode = glyphastore::DurableOpenMode::create_new,
         .filesystem_hooks = {.file_io = {.context = &blocker, .sync_file = &BlockingFileSync::sync_file}}});
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    SyncReleaseGuard release_on_exit{blocker};
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    const auto put = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::put,
        .request_id = 50,
        .key = bytes("mw-ping"),
        .value = bytes("before-ping"),
    });
    const auto ping = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 51,
        .value = bytes("pong"),
    });
    GLYPHA_REQUIRE(put.has_value());
    GLYPHA_REQUIRE(ping.has_value());
    std::vector<std::byte> pipeline;
    pipeline.insert(pipeline.end(), put->begin(), put->end());
    pipeline.insert(pipeline.end(), ping->begin(), ping->end());

    blocker.arm();
    GLYPHA_REQUIRE(send_all(socket, pipeline));
    GLYPHA_REQUIRE(blocker.wait_until_blocked());
    // Non-mutation opcodes are drain boundaries: no PING response while PUT is blocked.
    GLYPHA_REQUIRE(!socket_has_readable(socket, 100));
    blocker.release();

    GLYPHA_REQUIRE(expect_ok(socket, 50));
    GLYPHA_REQUIRE(expect_ok(socket, 51, "pong"));

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}
