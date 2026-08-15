#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace mw {

namespace detail {
class MemoryPoolView;
class SampleReleaseContext;
}

class Subscriber;

class SampleView {
  public:
    SampleView() noexcept;
    ~SampleView();

    SampleView(const SampleView&) = delete;
    SampleView& operator=(const SampleView&) = delete;
    SampleView(SampleView&& other) noexcept;
    SampleView& operator=(SampleView&& other) noexcept;

    const void* data() const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t sequence() const noexcept;
    std::uint64_t publishTimestampNs() const noexcept;

    std::uint64_t poolId() const noexcept;
    std::uint32_t chunkIndex() const noexcept;
    std::uint32_t generation() const noexcept;
    std::uint64_t payloadOffset() const noexcept;
    bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }

  private:
    friend class Subscriber;

    SampleView(std::shared_ptr<detail::SampleReleaseContext> release_context,
               std::shared_ptr<detail::MemoryPoolView> pool_view, const void* payload,
               std::size_t size, std::uint64_t sequence, std::uint64_t publish_timestamp_ns,
               std::uint64_t pool_id, std::uint32_t chunk_index, std::uint32_t generation,
               std::uint64_t payload_offset) noexcept;

    void release() noexcept;

    std::shared_ptr<detail::SampleReleaseContext> release_context_;
    std::shared_ptr<detail::MemoryPoolView> pool_view_;
    const void* payload_{nullptr};
    std::size_t size_{0};
    std::uint64_t sequence_{0};
    std::uint64_t publish_timestamp_ns_{0};
    std::uint64_t pool_id_{0};
    std::uint32_t chunk_index_{0};
    std::uint32_t generation_{0};
    std::uint64_t payload_offset_{0};
    bool active_{false};
};

} // namespace mw
