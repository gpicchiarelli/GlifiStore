#include "glyphastore/core/fault_injection.hpp"
#include "glyphastore/server/protocol.hpp"
#include "glyphastore/server/server.hpp"
#include "glyphastore/store/store.hpp"
#include "server_reactor_test_support.hpp"
#include "test.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace glyphastore::test::server_reactor_support;

// Connection/request/accept rate limits and idle/request timeouts (split for structure budget).

GLYPHA_TEST("server connection rate limit returns overloaded") {
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 8,
        .worker_count = 1,
        .abuse =
            {
                .connection_max_requests_per_sec = 2,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    // INIT + BIND consume the two-request budget; the next PING must be overloaded.
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));
    const auto ping = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 99,
        .value = bytes("x"),
    });
    GLYPHA_REQUIRE(ping.has_value());
    GLYPHA_REQUIRE(send_all(socket, *ping));
    const auto frame = receive_response(socket);
    const auto response = glyphastore::server::decode_response(frame);
    GLYPHA_REQUIRE(response.has_value());
    GLYPHA_REQUIRE(response->frame.status == glyphastore::server::ResponseStatus::overloaded);

    const auto stats = probe_lifecycle(socket, glyphastore::server::RequestOpcode::stats, 100);
    GLYPHA_REQUIRE(stats.has_value());
    GLYPHA_REQUIRE(stats->decoded.frame.status == glyphastore::server::ResponseStatus::ok);
    const auto report = text(stats->decoded.frame.value);
    GLYPHA_REQUIRE(report.find("abuse_connection_rate_rejected=") != std::string_view::npos);

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("connection rate limit OVERLOADED survives trailing decode failure") {
    // INIT+BIND exhaust the budget; PING queues OVERLOADED then a bad-version frame
    // used to make read_ready close before flush — silent EOF, no rate-limit signal.
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 8,
        .worker_count = 1,
        .abuse =
            {
                .connection_max_requests_per_sec = 2,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    const auto ping = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 99,
        .value = bytes("x"),
    });
    GLYPHA_REQUIRE(ping.has_value());
    std::vector<std::byte> pipeline(ping->begin(), ping->end());
    std::array<std::byte, glyphastore::server::kRequestHeaderBytes> bad{};
    bad[0] = std::byte{static_cast<unsigned char>(glyphastore::server::kRequestHeaderBytes)};
    bad[4] = std::byte{0xff}; // unsupported version
    pipeline.insert(pipeline.end(), bad.begin(), bad.end());
    GLYPHA_REQUIRE(send_all(socket, pipeline));

    const auto frame = receive_response(socket);
    const auto response = glyphastore::server::decode_response(frame);
    GLYPHA_REQUIRE(response.has_value());
    GLYPHA_REQUIRE(response->frame.request_id == 99);
    GLYPHA_REQUIRE(response->frame.status == glyphastore::server::ResponseStatus::overloaded);

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("server idle timeout closes quiet connections") {
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 4,
        .worker_count = 1,
        .abuse =
            {
                .idle_timeout_ms = 50,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    bool closed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        char byte{};
        const auto received = ::recv(socket, &byte, 1, 0);
        if (received == 0) {
            closed = true;
            break;
        }
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(closed);

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("server request timeout closes in-flight cold read and cancels") {
    ServerTemporaryDirectory temporary;
    const auto path = temporary.store_path();
    {
        auto seed = glyphastore::Store::open({.worker_config = {.explicit_count = 1},
                                              .storage_mode = glyphastore::StorageMode::durable_sync,
                                              .data_directory = path,
                                              .durable_open_mode = glyphastore::DurableOpenMode::create_new});
        GLYPHA_REQUIRE(seed.has_value());
        GLYPHA_REQUIRE((*seed)->put("timeout-read", bytes("value")).has_value());
        GLYPHA_REQUIRE((*seed)->close().has_value());
    }

    BlockingColdRead blocker;
    auto opened = glyphastore::server::Server::create(
        {.port = 0,
         .maximum_connections = 1,
         .disk_read_thread_count = 1,
         .abuse = {.request_timeout_ms = 50}},
        {.storage_mode = glyphastore::StorageMode::durable_sync,
         .data_directory = path,
         .durable_open_mode = glyphastore::DurableOpenMode::open_existing,
         .filesystem_hooks = {
             .file_io = {.context = &blocker, .read_some_at = &BlockingColdRead::read_some_at}}});
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));
    blocker.arm();
    const auto get = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::get,
        .request_id = 71,
        .key = bytes("timeout-read"),
    });
    GLYPHA_REQUIRE(get.has_value());
    GLYPHA_REQUIRE(send_all(socket, *get));
    GLYPHA_REQUIRE(blocker.wait_until_blocked());

    bool closed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        char byte{};
        const auto received = ::recv(socket, &byte, 1, 0);
        if (received == 0) {
            closed = true;
            break;
        }
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(closed);

    const auto report = server.stats_report();
    GLYPHA_REQUIRE(report.has_value());
    const auto marker = report->find("abuse_request_timeout_closed=");
    GLYPHA_REQUIRE(marker != std::string::npos);
    const auto value_start = marker + std::strlen("abuse_request_timeout_closed=");
    GLYPHA_REQUIRE(value_start < report->size());
    GLYPHA_REQUIRE((*report)[value_start] != '0');

    blocker.release();
    GLYPHA_REQUIRE(blocker.wait_until_finished());

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("server request timeout closes partial request frames") {
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 4,
        .worker_count = 1,
        .abuse =
            {
                .request_timeout_ms = 50,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    // Incomplete frame starts the partial-assembly budget; peer must be closed.
    constexpr std::array<std::byte, 8> partial{
        std::byte{0x28}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    GLYPHA_REQUIRE(send_all(socket, partial));

    bool closed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        char byte{};
        const auto received = ::recv(socket, &byte, 1, 0);
        if (received == 0) {
            closed = true;
            break;
        }
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            continue;
        }
        if (received < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(closed);

    const auto report = server.stats_report();
    GLYPHA_REQUIRE(report.has_value());
    GLYPHA_REQUIRE(report->find("abuse_request_timeout_closed=") != std::string::npos);
    const auto marker = report->find("abuse_request_timeout_closed=");
    const auto value_start = marker + std::strlen("abuse_request_timeout_closed=");
    GLYPHA_REQUIRE(value_start < report->size());
    GLYPHA_REQUIRE((*report)[value_start] != '0');

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("partial request timeout drains prior decided response before close") {
    // A small server send buffer makes the large PING retain decided user-space
    // bytes when a trailing partial frame times out. Keep the client receive window
    // normal so completion does not depend on platform TCP retransmit intervals.
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 4,
        .worker_count = 1,
        .accepted_socket_send_buffer_bytes = 4U * 1024U,
        .abuse =
            {
                .request_timeout_ms = 80,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    constexpr std::size_t kPayload = 64U * 1024U;
    std::vector<std::byte> payload(kPayload, std::byte{0x5a});
    const auto ping = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 55,
        .value = payload,
    });
    GLYPHA_REQUIRE(ping.has_value());
    GLYPHA_REQUIRE(send_all(socket, *ping));

    // Incomplete follow-up starts the partial-assembly budget while PING output is
    // still bounded by the accepted socket's deliberately small send buffer.
    constexpr std::array<std::byte, 8> partial{
        std::byte{0x28}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    GLYPHA_REQUIRE(send_all(socket, partial));
    std::this_thread::sleep_for(std::chrono::milliseconds{150});

    std::vector<std::byte> received;
    received.reserve(kPayload + 64);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<std::byte, 16U * 1024U> chunk{};
        const auto n = ::recv(socket, chunk.data(), chunk.size(), 0);
        if (n > 0) {
            received.insert(received.end(), chunk.begin(), chunk.begin() + n);
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(received.size() >= glyphastore::server::kResponseHeaderBytes);
    const auto decoded = glyphastore::server::decode_response(received);
    GLYPHA_REQUIRE(decoded.has_value());
    GLYPHA_REQUIRE(decoded->complete);
    GLYPHA_REQUIRE(decoded->frame.request_id == 55);
    GLYPHA_REQUIRE(decoded->frame.status == glyphastore::server::ResponseStatus::ok);
    GLYPHA_REQUIRE(decoded->frame.value.size() == kPayload);

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("half-close with pending large response drains before poller teardown") {
    // Large PING + small server SO_SNDBUF leaves decided bytes queued; SHUT_WR must
    // still deliver them. Hangup is handled before raw poller error so co-reported
    // error|hangup cannot hard-close and discard the remainder after EAGAIN.
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 4,
        .worker_count = 1,
        .accepted_socket_send_buffer_bytes = 4U * 1024U,
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    constexpr std::size_t kPayload = 64U * 1024U;
    std::vector<std::byte> payload(kPayload, std::byte{0x5c});
    const auto ping = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 59,
        .value = payload,
    });
    GLYPHA_REQUIRE(ping.has_value());
    GLYPHA_REQUIRE(send_all(socket, *ping));

    bool saw_response = false;
    const auto peek_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < peek_deadline) {
        char byte{};
        const auto peeked = ::recv(socket, &byte, 1, MSG_PEEK);
        if (peeked > 0) {
            saw_response = true;
            break;
        }
        if (peeked == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(saw_response);
    GLYPHA_REQUIRE(::shutdown(socket, SHUT_WR) == 0);

    std::vector<std::byte> received;
    received.reserve(kPayload + 64);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<std::byte, 16U * 1024U> chunk{};
        const auto n = ::recv(socket, chunk.data(), chunk.size(), 0);
        if (n > 0) {
            received.insert(received.end(), chunk.begin(), chunk.begin() + n);
            if (received.size() >= glyphastore::server::kResponseHeaderBytes) {
                const auto decoded = glyphastore::server::decode_response(received);
                if (decoded && decoded->complete) {
                    break;
                }
            }
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(received.size() >= glyphastore::server::kResponseHeaderBytes);
    const auto decoded = glyphastore::server::decode_response(received);
    GLYPHA_REQUIRE(decoded.has_value());
    GLYPHA_REQUIRE(decoded->complete);
    GLYPHA_REQUIRE(decoded->frame.request_id == 59);
    GLYPHA_REQUIRE(decoded->frame.status == glyphastore::server::ResponseStatus::ok);
    GLYPHA_REQUIRE(decoded->frame.value.size() == kPayload);
    GLYPHA_REQUIRE(server.live());
    GLYPHA_REQUIRE(server.healthy());

    static_cast<void>(::close(socket));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

#if defined(GLYPHASTORE_FAULT_INJECTION)
GLYPHA_TEST("input buffer allocation failure drains decided response without daemon fail-stop") {
    // Large PING fills the peer receive window so decided bytes remain queued.
    // A follow-up that fails input-buffer growth must drain that ACK, not escalate
    // to executor fail-stop or hard-close that discards it.
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 4,
        .worker_count = 1,
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto socket = connect_to(server.port());
    GLYPHA_REQUIRE(socket >= 0);
    int rcvbuf = 4 * 1024;
    GLYPHA_REQUIRE(::setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) == 0);
    GLYPHA_REQUIRE(initialize_and_bind(socket, 0, 1));

    constexpr std::size_t kPayload = 64U * 1024U;
    std::vector<std::byte> payload(kPayload, std::byte{0x5b});
    const auto ping = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 56,
        .value = payload,
    });
    GLYPHA_REQUIRE(ping.has_value());
    GLYPHA_REQUIRE(send_all(socket, *ping));
    // Wait until decided PING bytes are visible — response is queued server-side and
    // may still be blocked on the small client receive window.
    bool saw_response = false;
    const auto peek_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < peek_deadline) {
        char byte{};
        const auto peeked = ::recv(socket, &byte, 1, MSG_PEEK);
        if (peeked > 0) {
            saw_response = true;
            break;
        }
        if (peeked == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            continue;
        }
        break;
    }
    GLYPHA_REQUIRE(saw_response);

    glyphastore::fault::reset();
    glyphastore::fault::fail_once(glyphastore::fault::Site::input_buffer);
    const auto follow_up = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 57,
        .value = bytes("follow"),
    });
    GLYPHA_REQUIRE(follow_up.has_value());
    GLYPHA_REQUIRE(send_all(socket, *follow_up));

    std::vector<std::byte> received;
    received.reserve(kPayload + 64);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
        std::array<std::byte, 16U * 1024U> chunk{};
        const auto n = ::recv(socket, chunk.data(), chunk.size(), 0);
        if (n > 0) {
            received.insert(received.end(), chunk.begin(), chunk.begin() + n);
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            continue;
        }
        break;
    }
    glyphastore::fault::reset();
    GLYPHA_REQUIRE(received.size() >= glyphastore::server::kResponseHeaderBytes);
    const auto decoded = glyphastore::server::decode_response(received);
    GLYPHA_REQUIRE(decoded.has_value());
    GLYPHA_REQUIRE(decoded->complete);
    GLYPHA_REQUIRE(decoded->frame.request_id == 56);
    GLYPHA_REQUIRE(decoded->frame.status == glyphastore::server::ResponseStatus::ok);
    GLYPHA_REQUIRE(decoded->frame.value.size() == kPayload);
    GLYPHA_REQUIRE(server.live());
    GLYPHA_REQUIRE(server.healthy());

    const auto probe = connect_to(server.port());
    GLYPHA_REQUIRE(probe >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(probe, 0, 1));
    const auto health = probe_lifecycle(probe, glyphastore::server::RequestOpcode::health, 58);
    GLYPHA_REQUIRE(health.has_value());
    GLYPHA_REQUIRE(health->decoded.frame.status == glyphastore::server::ResponseStatus::ok);

    static_cast<void>(::close(socket));
    static_cast<void>(::close(probe));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}
#endif

GLYPHA_TEST("server accept rate limit drops excess handshakes") {
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 32,
        .worker_count = 1,
        .abuse =
            {
                .max_accepts_per_sec = 1,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto first = connect_to(server.port());
    GLYPHA_REQUIRE(first >= 0);
    // Give the reactor time to accept the first connection against the budget.
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    const auto second = connect_to(server.port());
    GLYPHA_REQUIRE(second >= 0);

    // First peer can still speak; the second accept is dropped so INIT should fail.
    GLYPHA_REQUIRE(initialize_and_bind(first, 0, 1));
    const auto init = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::init,
        .request_id = 1,
    });
    GLYPHA_REQUIRE(init.has_value());
    static_cast<void>(send_all(second, *init));
    const auto frame = receive_response(second);
    GLYPHA_REQUIRE(frame.empty());

    const auto stats = probe_lifecycle(first, glyphastore::server::RequestOpcode::stats, 7);
    GLYPHA_REQUIRE(stats.has_value());
    const auto report = text(stats->decoded.frame.value);
    GLYPHA_REQUIRE(report.find("abuse_accepts_rejected=") != std::string_view::npos);

    static_cast<void>(::close(first));
    static_cast<void>(::close(second));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("client RST hard-close releases reactor connection slot") {
    // Abortive close (SO_LINGER l_linger=0) must free the bounded connection slot
    // so a subsequent peer can INIT+BIND under maximum_connections=1.
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 1,
        .worker_count = 1,
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto first = connect_to(server.port());
    GLYPHA_REQUIRE(first >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(first, 0, 1));

    linger reset_on_close{.l_onoff = 1, .l_linger = 0};
    GLYPHA_REQUIRE(::setsockopt(first, SOL_SOCKET, SO_LINGER, &reset_on_close, sizeof(reset_on_close)) == 0);
    static_cast<void>(::close(first));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (server.active_connections_per_executor()[0] != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    GLYPHA_REQUIRE(server.active_connections_per_executor()[0] == 0);

    const auto second = connect_to(server.port());
    GLYPHA_REQUIRE(second >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(second, 0, 1));
    const auto ping = probe_lifecycle(second, glyphastore::server::RequestOpcode::ping, 7);
    GLYPHA_REQUIRE(ping.has_value());
    GLYPHA_REQUIRE(ping->decoded.frame.status == glyphastore::server::ResponseStatus::ok);

    static_cast<void>(::close(second));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}

GLYPHA_TEST("connection rate limit reconnect after window reset admits again") {
    auto opened = glyphastore::server::Server::create({
        .port = 0,
        .maximum_connections = 8,
        .worker_count = 1,
        .abuse =
            {
                .connection_max_requests_per_sec = 2,
            },
    });
    GLYPHA_REQUIRE(opened.has_value());
    auto& server = **opened;
    GLYPHA_REQUIRE(server.start().has_value());

    const auto first = connect_to(server.port());
    GLYPHA_REQUIRE(first >= 0);
    // INIT + BIND consume the two-request budget; the next PING must be overloaded.
    GLYPHA_REQUIRE(initialize_and_bind(first, 0, 1));
    const auto overloaded = glyphastore::server::encode_request({
        .opcode = glyphastore::server::RequestOpcode::ping,
        .request_id = 99,
        .value = bytes("x"),
    });
    GLYPHA_REQUIRE(overloaded.has_value());
    GLYPHA_REQUIRE(send_all(first, *overloaded));
    const auto overloaded_frame = receive_response(first);
    const auto overloaded_response = glyphastore::server::decode_response(overloaded_frame);
    GLYPHA_REQUIRE(overloaded_response.has_value());
    GLYPHA_REQUIRE(overloaded_response->frame.status == glyphastore::server::ResponseStatus::overloaded);
    static_cast<void>(::close(first));

    std::this_thread::sleep_for(std::chrono::milliseconds{1100});

    const auto second = connect_to(server.port());
    GLYPHA_REQUIRE(second >= 0);
    GLYPHA_REQUIRE(initialize_and_bind(second, 0, 1));
    // INIT+BIND filled the fresh connection window; wait for the next second so PING admits.
    std::this_thread::sleep_for(std::chrono::milliseconds{1100});
    const auto ping = probe_lifecycle(second, glyphastore::server::RequestOpcode::ping, 100);
    GLYPHA_REQUIRE(ping.has_value());
    GLYPHA_REQUIRE(ping->decoded.frame.status == glyphastore::server::ResponseStatus::ok);

    static_cast<void>(::close(second));
    server.request_stop();
    GLYPHA_REQUIRE(server.join().has_value());
}
