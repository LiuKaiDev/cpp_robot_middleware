#pragma once

#include <mw/result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mw::detail {

inline constexpr std::uint32_t kControlMagic = 0x4D574332U;
inline constexpr std::uint16_t kControlVersion = 2U;
inline constexpr std::size_t kControlHeaderSize = 16U;
inline constexpr std::size_t kMaxControlPayloadSize = 64U * 1024U;

enum class Opcode : std::uint16_t {
    RegisterNode = 1,
    UnregisterNode = 2,
    AdvertiseTopic = 3,
    UnadvertiseTopic = 4,
    SubscribeTopic = 5,
    UnsubscribeTopic = 6,
    ResolveEndpoint = 7,
    ListNodes = 8,
    ListTopics = 9,
    QueryTopic = 10,
    Response = 100,
};

struct ControlHeader {
    std::uint32_t magic{kControlMagic};
    std::uint16_t version{kControlVersion};
    Opcode opcode{Opcode::Response};
    std::uint32_t request_id{0};
    std::uint32_t payload_size{0};
};

struct ControlFrame {
    ControlHeader header;
    std::vector<std::uint8_t> payload;
};

enum class ControlHeaderValidation {
    Valid,
    BadMagic,
    UnsupportedVersion,
    PayloadTooLarge,
};

class PayloadWriter {
  public:
    void writeU16(std::uint16_t value);
    void writeU32(std::uint32_t value);
    void writeU64(std::uint64_t value);
    void writeString(const std::string& value);
    void writeBytes(const std::vector<std::uint8_t>& value);

    const std::vector<std::uint8_t>& data() const noexcept { return data_; }
    std::vector<std::uint8_t> take() noexcept { return std::move(data_); }

  private:
    std::vector<std::uint8_t> data_;
};

class PayloadReader {
  public:
    explicit PayloadReader(const std::vector<std::uint8_t>& data) noexcept : data_(data) {}

    bool readU16(std::uint16_t& value) noexcept;
    bool readU32(std::uint32_t& value) noexcept;
    bool readU64(std::uint64_t& value) noexcept;
    bool readString(std::string& value);
    bool readRemaining(std::vector<std::uint8_t>& value);

    bool empty() const noexcept { return offset_ == data_.size(); }
    std::size_t remaining() const noexcept { return data_.size() - offset_; }

  private:
    const std::vector<std::uint8_t>& data_;
    std::size_t offset_{0};
};

struct ResponseEnvelope {
    ErrorCode error{ErrorCode::Ok};
    std::string message;
    std::vector<std::uint8_t> body;
};

std::vector<std::uint8_t> encodeControlHeader(const ControlHeader& header);
std::optional<ControlHeader> decodeControlHeader(const std::uint8_t* data,
                                                 std::size_t size) noexcept;
ControlHeaderValidation validateControlHeader(const ControlHeader& header) noexcept;
std::vector<std::uint8_t> encodeControlFrame(Opcode opcode, std::uint32_t request_id,
                                             const std::vector<std::uint8_t>& payload);
bool isKnownRequestOpcode(Opcode opcode) noexcept;

std::vector<std::uint8_t> encodeResponsePayload(ErrorCode error, const std::string& message,
                                                const std::vector<std::uint8_t>& body = {});
std::optional<ResponseEnvelope> decodeResponsePayload(const std::vector<std::uint8_t>& payload);

} // namespace mw::detail
