#pragma once

#include "glifistore/core/worker_routing.hpp"
#include "glifistore/persistence/filesystem.hpp"
#include "glifistore/store/config.hpp"

#include <cstddef>
#include <optional>

namespace glifistore {

[[nodiscard]] auto prepare_durable_store(DataDirectory& directory, DurableOpenMode mode,
                                         std::size_t creation_worker_count,
                                         std::optional<std::size_t> required_worker_count,
                                         const DurableResourceLimits& limits = {},
                                         const WorkerRoutingConfig& creation_routing = {}) -> Status;

} // namespace glifistore
