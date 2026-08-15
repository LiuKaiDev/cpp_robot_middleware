#include "detail/shared_memory.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace mw::detail {
namespace {

void validateArguments(const std::string& name, std::size_t size) {
    if (!isValidSharedMemoryName(name)) {
        throw std::invalid_argument("invalid POSIX shared memory name");
    }
    if (size == 0U || size > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        throw std::invalid_argument("invalid shared memory size");
    }
}

UniqueFd openObject(const std::string& name, int flags, mode_t mode = 0) {
    const int descriptor = ::shm_open(name.c_str(), flags | O_CLOEXEC, mode);
    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(), "shm_open(" + name + ")");
    }
    return UniqueFd{descriptor};
}

} // namespace

bool isValidSharedMemoryName(const std::string& name) noexcept {
    return name.size() >= 2U && name.size() <= 192U && name.front() == '/' &&
           name.find('/', 1U) == std::string::npos && name.find('\0') == std::string::npos;
}

SharedMemoryRegion SharedMemoryRegion::create(const std::string& name, std::size_t size) {
    validateArguments(name, size);
    UniqueFd fd = openObject(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    bool owns_name = true;
    try {
        if (::ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
            throw std::system_error(errno, std::generic_category(), "ftruncate(" + name + ")");
        }
        void* address = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
        if (address == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "mmap(" + name + ")");
        }
        owns_name = false;
        return SharedMemoryRegion{std::move(fd), address, size, name, true};
    } catch (...) {
        if (owns_name) {
            (void)::shm_unlink(name.c_str());
        }
        throw;
    }
}

SharedMemoryRegion SharedMemoryRegion::openReadOnly(const std::string& name,
                                                    std::size_t expected_size) {
    validateArguments(name, expected_size);
    UniqueFd fd = openObject(name, O_RDONLY);
    struct stat status {};
    if (::fstat(fd.get(), &status) != 0) {
        throw std::system_error(errno, std::generic_category(), "fstat(" + name + ")");
    }
    if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) != expected_size) {
        throw std::system_error(EINVAL, std::generic_category(),
                                "shared memory object has unexpected size");
    }
    void* address = ::mmap(nullptr, expected_size, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (address == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "mmap(" + name + ")");
    }
    return SharedMemoryRegion{std::move(fd), address, expected_size, name, false};
}

SharedMemoryRegion SharedMemoryRegion::openReadWrite(const std::string& name,
                                                     std::size_t expected_size) {
    validateArguments(name, expected_size);
    UniqueFd fd = openObject(name, O_RDWR);
    struct stat status {};
    if (::fstat(fd.get(), &status) != 0) {
        throw std::system_error(errno, std::generic_category(), "fstat(" + name + ")");
    }
    if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) != expected_size) {
        throw std::system_error(EINVAL, std::generic_category(),
                                "shared memory object has unexpected size");
    }
    void* address = ::mmap(nullptr, expected_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (address == MAP_FAILED) {
        throw std::system_error(errno, std::generic_category(), "mmap(" + name + ")");
    }
    return SharedMemoryRegion{std::move(fd), address, expected_size, name, false};
}

SharedMemoryRegion::SharedMemoryRegion(UniqueFd fd, void* address, std::size_t size,
                                       std::string name, bool owns_name) noexcept
    : fd_(std::move(fd)), address_(address), size_(size), name_(std::move(name)),
      owns_name_(owns_name) {}

SharedMemoryRegion::~SharedMemoryRegion() { cleanup(); }

SharedMemoryRegion::SharedMemoryRegion(SharedMemoryRegion&& other) noexcept
    : fd_(std::move(other.fd_)), address_(std::exchange(other.address_, nullptr)),
      size_(std::exchange(other.size_, 0U)), name_(std::move(other.name_)),
      owns_name_(std::exchange(other.owns_name_, false)) {}

SharedMemoryRegion& SharedMemoryRegion::operator=(SharedMemoryRegion&& other) noexcept {
    if (this != &other) {
        cleanup();
        fd_ = std::move(other.fd_);
        address_ = std::exchange(other.address_, nullptr);
        size_ = std::exchange(other.size_, 0U);
        name_ = std::move(other.name_);
        owns_name_ = std::exchange(other.owns_name_, false);
    }
    return *this;
}

bool SharedMemoryRegion::unlinkName() noexcept {
    if (!owns_name_) {
        return true;
    }
    if (::shm_unlink(name_.c_str()) != 0 && errno != ENOENT) {
        return false;
    }
    owns_name_ = false;
    return true;
}

void SharedMemoryRegion::cleanup() noexcept {
    if (address_ != nullptr) {
        (void)::munmap(address_, size_);
        address_ = nullptr;
        size_ = 0U;
    }
    fd_.reset();
    (void)unlinkName();
}

} // namespace mw::detail
