#pragma once

#include "detail/pool_protocol.hpp"

namespace mw::detail {

class SampleReleaseContext {
  public:
    virtual ~SampleReleaseContext() = default;
    virtual void release(const ChunkHandle& handle) noexcept = 0;
};

} // namespace mw::detail
