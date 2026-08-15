#include <mw/loaned_sample.hpp>

#include "detail/loaned_sample_owner.hpp"

#include <utility>

namespace mw {

LoanedSample::LoanedSample() noexcept = default;

LoanedSample::LoanedSample(std::shared_ptr<detail::LoanedSampleOwner> owner, void* payload,
                           std::size_t size, std::size_t capacity, std::uint64_t pool_id,
                           std::uint32_t chunk_index, std::uint32_t generation,
                           std::uint64_t payload_offset) noexcept
    : owner_(std::move(owner)), payload_(payload), size_(size), capacity_(capacity),
      pool_id_(pool_id), chunk_index_(chunk_index), generation_(generation),
      payload_offset_(payload_offset), error_(ErrorCode::Ok), active_(true) {}

LoanedSample::LoanedSample(ErrorCode error) noexcept : error_(error) {}

LoanedSample::~LoanedSample() { cancel(); }

LoanedSample::LoanedSample(LoanedSample&& other) noexcept
    : owner_(std::move(other.owner_)), payload_(std::exchange(other.payload_, nullptr)),
      size_(std::exchange(other.size_, 0U)), capacity_(std::exchange(other.capacity_, 0U)),
      pool_id_(std::exchange(other.pool_id_, 0U)),
      chunk_index_(std::exchange(other.chunk_index_, 0U)),
      generation_(std::exchange(other.generation_, 0U)),
      payload_offset_(std::exchange(other.payload_offset_, 0U)),
      error_(std::exchange(other.error_, ErrorCode::InvalidState)),
      active_(std::exchange(other.active_, false)) {}

LoanedSample& LoanedSample::operator=(LoanedSample&& other) noexcept {
    if (this != &other) {
        cancel();
        owner_ = std::move(other.owner_);
        payload_ = std::exchange(other.payload_, nullptr);
        size_ = std::exchange(other.size_, 0U);
        capacity_ = std::exchange(other.capacity_, 0U);
        pool_id_ = std::exchange(other.pool_id_, 0U);
        chunk_index_ = std::exchange(other.chunk_index_, 0U);
        generation_ = std::exchange(other.generation_, 0U);
        payload_offset_ = std::exchange(other.payload_offset_, 0U);
        error_ = std::exchange(other.error_, ErrorCode::InvalidState);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

void* LoanedSample::data() noexcept { return active_ ? payload_ : nullptr; }
const void* LoanedSample::data() const noexcept { return active_ ? payload_ : nullptr; }
std::size_t LoanedSample::size() const noexcept { return size_; }
std::size_t LoanedSample::capacity() const noexcept { return capacity_; }
ErrorCode LoanedSample::error() const noexcept { return error_; }
bool LoanedSample::valid() const noexcept { return active_ && owner_ && error_ == ErrorCode::Ok; }
std::uint64_t LoanedSample::poolId() const noexcept { return pool_id_; }
std::uint32_t LoanedSample::chunkIndex() const noexcept { return chunk_index_; }
std::uint32_t LoanedSample::generation() const noexcept { return generation_; }
std::uint64_t LoanedSample::payloadOffset() const noexcept { return payload_offset_; }

PublishResult LoanedSample::publish() {
    if (!valid()) {
        return {ErrorCode::InvalidState, 0U, 0U};
    }
    const detail::ChunkHandle handle{pool_id_, chunk_index_, generation_, payload_offset_};
    PublishResult result = owner_->publishLoanedSample(handle, size_);
    active_ = false;
    payload_ = nullptr;
    error_ = result.error;
    owner_.reset();
    return result;
}

void LoanedSample::cancel() noexcept {
    if (active_ && owner_) {
        error_ = owner_->cancelLoanedSample(
            {pool_id_, chunk_index_, generation_, payload_offset_});
    }
    active_ = false;
    payload_ = nullptr;
    owner_.reset();
}

} // namespace mw
