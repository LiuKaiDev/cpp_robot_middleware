#pragma once

#include <mw/result.hpp>

#include "detail/pool_protocol.hpp"

#include <cstddef>

namespace mw::detail {

class LoanedSampleOwner {
  public:
    virtual ~LoanedSampleOwner() = default;

    virtual PublishResult publishLoanedSample(const ChunkHandle& handle,
                                              std::size_t payload_size) = 0;
    virtual ErrorCode cancelLoanedSample(const ChunkHandle& handle) noexcept = 0;
};

} // namespace mw::detail
