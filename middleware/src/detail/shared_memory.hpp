#pragma once

#include "detail/unique_fd.hpp"

#include <cstddef>
#include <string>

namespace mw::detail {

class SharedMemoryRegion {
  public:
    static SharedMemoryRegion create(const std::string& name, std::size_t size);
    static SharedMemoryRegion openReadOnly(const std::string& name, std::size_t expected_size);
    static SharedMemoryRegion openReadWrite(const std::string& name, std::size_t expected_size);

    ~SharedMemoryRegion();

    SharedMemoryRegion(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion(SharedMemoryRegion&& other) noexcept;
    SharedMemoryRegion& operator=(SharedMemoryRegion&& other) noexcept;

    void* data() noexcept { return address_; }
    const void* data() const noexcept { return address_; }
    std::size_t size() const noexcept { return size_; }
    const std::string& name() const noexcept { return name_; }
    bool ownsName() const noexcept { return owns_name_; }

    bool unlinkName() noexcept;

  private:
    SharedMemoryRegion(UniqueFd fd, void* address, std::size_t size, std::string name,
                       bool owns_name) noexcept;
    void cleanup() noexcept;

    UniqueFd fd_;
    void* address_{nullptr};
    std::size_t size_{0};
    std::string name_;
    bool owns_name_{false};
};

bool isValidSharedMemoryName(const std::string& name) noexcept;

} // namespace mw::detail
