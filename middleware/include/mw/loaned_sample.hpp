#pragma once

#include <mw/result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace mw {

namespace detail {
class LoanedSampleOwner;
}

class Publisher;

class LoanedSample {
  public:
    LoanedSample() noexcept;
    ~LoanedSample();

    LoanedSample(const LoanedSample&) = delete;
    LoanedSample& operator=(const LoanedSample&) = delete;
    LoanedSample(LoanedSample&& other) noexcept;
    LoanedSample& operator=(LoanedSample&& other) noexcept;

    void* data() noexcept;
    const void* data() const noexcept;
    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;
    ErrorCode error() const noexcept;
    bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }

    std::uint64_t poolId() const noexcept;
    std::uint32_t chunkIndex() const noexcept;
    std::uint32_t generation() const noexcept;
    std::uint64_t payloadOffset() const noexcept;

    PublishResult publish();

  private:
    friend class Publisher;

    LoanedSample(std::shared_ptr<detail::LoanedSampleOwner> owner, void* payload,
                 std::size_t size, std::size_t capacity, std::uint64_t pool_id,
                 std::uint32_t chunk_index, std::uint32_t generation,
                 std::uint64_t payload_offset) noexcept;
    explicit LoanedSample(ErrorCode error) noexcept;

    void cancel() noexcept;

    std::shared_ptr<detail::LoanedSampleOwner> owner_;
    void* payload_{nullptr};
    std::size_t size_{0};
    std::size_t capacity_{0};
    std::uint64_t pool_id_{0};
    std::uint32_t chunk_index_{0};
    std::uint32_t generation_{0};
    std::uint64_t payload_offset_{0};
    ErrorCode error_{ErrorCode::InvalidState};
    bool active_{false};
};

} // namespace mw
