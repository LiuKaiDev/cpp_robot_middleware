#include <mw/sample_view.hpp>

#include "detail/memory_pool.hpp"
#include "detail/sample_release_context.hpp"

#include <utility>

namespace mw {

SampleView::SampleView() noexcept = default;

SampleView::SampleView(std::shared_ptr<detail::SampleReleaseContext> release_context,
                       std::shared_ptr<detail::MemoryPoolView> pool_view, const void* payload,
                       std::size_t size, std::uint64_t sequence,
                       std::uint64_t publish_timestamp_ns, std::uint64_t pool_id,
                       std::uint32_t chunk_index, std::uint32_t generation,
                       std::uint64_t payload_offset) noexcept
    : release_context_(std::move(release_context)), pool_view_(std::move(pool_view)),
      payload_(payload), size_(size), sequence_(sequence),
      publish_timestamp_ns_(publish_timestamp_ns), pool_id_(pool_id), chunk_index_(chunk_index),
      generation_(generation), payload_offset_(payload_offset), active_(true) {}

SampleView::~SampleView() { release(); }

SampleView::SampleView(SampleView&& other) noexcept
    : release_context_(std::move(other.release_context_)), pool_view_(std::move(other.pool_view_)),
      payload_(std::exchange(other.payload_, nullptr)), size_(std::exchange(other.size_, 0U)),
      sequence_(std::exchange(other.sequence_, 0U)),
      publish_timestamp_ns_(std::exchange(other.publish_timestamp_ns_, 0U)),
      pool_id_(std::exchange(other.pool_id_, 0U)),
      chunk_index_(std::exchange(other.chunk_index_, 0U)),
      generation_(std::exchange(other.generation_, 0U)),
      payload_offset_(std::exchange(other.payload_offset_, 0U)),
      active_(std::exchange(other.active_, false)) {}

SampleView& SampleView::operator=(SampleView&& other) noexcept {
    if (this != &other) {
        release();
        release_context_ = std::move(other.release_context_);
        pool_view_ = std::move(other.pool_view_);
        payload_ = std::exchange(other.payload_, nullptr);
        size_ = std::exchange(other.size_, 0U);
        sequence_ = std::exchange(other.sequence_, 0U);
        publish_timestamp_ns_ = std::exchange(other.publish_timestamp_ns_, 0U);
        pool_id_ = std::exchange(other.pool_id_, 0U);
        chunk_index_ = std::exchange(other.chunk_index_, 0U);
        generation_ = std::exchange(other.generation_, 0U);
        payload_offset_ = std::exchange(other.payload_offset_, 0U);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

const void* SampleView::data() const noexcept { return active_ ? payload_ : nullptr; }
std::size_t SampleView::size() const noexcept { return size_; }
std::uint64_t SampleView::sequence() const noexcept { return sequence_; }
std::uint64_t SampleView::publishTimestampNs() const noexcept { return publish_timestamp_ns_; }
std::uint64_t SampleView::poolId() const noexcept { return pool_id_; }
std::uint32_t SampleView::chunkIndex() const noexcept { return chunk_index_; }
std::uint32_t SampleView::generation() const noexcept { return generation_; }
std::uint64_t SampleView::payloadOffset() const noexcept { return payload_offset_; }
bool SampleView::valid() const noexcept {
    return active_ && release_context_ && pool_view_ && payload_ != nullptr;
}

void SampleView::release() noexcept {
    if (active_ && release_context_) {
        release_context_->release({pool_id_, chunk_index_, generation_, payload_offset_});
    }
    active_ = false;
    payload_ = nullptr;
    release_context_.reset();
    pool_view_.reset();
}

} // namespace mw
