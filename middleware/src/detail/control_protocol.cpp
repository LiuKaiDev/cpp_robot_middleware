#include "detail/control_protocol.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace mw::detail {
namespace {

template <typename Integer> void appendBigEndian(std::vector<std::uint8_t>& output, Integer value) {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        const auto shift = static_cast<unsigned>((sizeof(Integer) - index - 1U) * 8U);
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

template <typename Integer> Integer decodeBigEndian(const std::uint8_t* input) noexcept {
    Integer value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value = static_cast<Integer>((value << 8U) | input[index]);
    }
    return value;
}

} // namespace

void PayloadWriter::writeU16(std::uint16_t value) { appendBigEndian(data_, value); }
void PayloadWriter::writeU32(std::uint32_t value) { appendBigEndian(data_, value); }
void PayloadWriter::writeU64(std::uint64_t value) { appendBigEndian(data_, value); }

void PayloadWriter::writeString(const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("control protocol string is too large");
    }
    writeU32(static_cast<std::uint32_t>(value.size()));
    data_.insert(data_.end(), value.begin(), value.end());
}

void PayloadWriter::writeBytes(const std::vector<std::uint8_t>& value) {
    data_.insert(data_.end(), value.begin(), value.end());
}

bool PayloadReader::readU16(std::uint16_t& value) noexcept {
    if (remaining() < sizeof(value)) {
        return false;
    }
    value = decodeBigEndian<std::uint16_t>(data_.data() + offset_);
    offset_ += sizeof(value);
    return true;
}

bool PayloadReader::readU32(std::uint32_t& value) noexcept {
    if (remaining() < sizeof(value)) {
        return false;
    }
    value = decodeBigEndian<std::uint32_t>(data_.data() + offset_);
    offset_ += sizeof(value);
    return true;
}

bool PayloadReader::readU64(std::uint64_t& value) noexcept {
    if (remaining() < sizeof(value)) {
        return false;
    }
    value = decodeBigEndian<std::uint64_t>(data_.data() + offset_);
    offset_ += sizeof(value);
    return true;
}

bool PayloadReader::readString(std::string& value) {
    std::uint32_t size = 0;
    if (!readU32(size) || size > remaining()) {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(data_.data() + offset_), size);
    offset_ += size;
    return true;
}

bool PayloadReader::readRemaining(std::vector<std::uint8_t>& value) {
    value.assign(data_.begin() + static_cast<std::ptrdiff_t>(offset_), data_.end());
    offset_ = data_.size();
    return true;
}

std::vector<std::uint8_t> encodeControlHeader(const ControlHeader& header) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(kControlHeaderSize);
    appendBigEndian(encoded, header.magic);
    appendBigEndian(encoded, header.version);
    appendBigEndian(encoded, static_cast<std::uint16_t>(header.opcode));
    appendBigEndian(encoded, header.request_id);
    appendBigEndian(encoded, header.payload_size);
    return encoded;
}

std::optional<ControlHeader> decodeControlHeader(const std::uint8_t* data,
                                                 std::size_t size) noexcept {
    if (data == nullptr || size != kControlHeaderSize) {
        return std::nullopt;
    }
    return ControlHeader{
        decodeBigEndian<std::uint32_t>(data),
        decodeBigEndian<std::uint16_t>(data + 4U),
        static_cast<Opcode>(decodeBigEndian<std::uint16_t>(data + 6U)),
        decodeBigEndian<std::uint32_t>(data + 8U),
        decodeBigEndian<std::uint32_t>(data + 12U),
    };
}

ControlHeaderValidation validateControlHeader(const ControlHeader& header) noexcept {
    if (header.magic != kControlMagic) {
        return ControlHeaderValidation::BadMagic;
    }
    if (header.version != kControlVersion) {
        return ControlHeaderValidation::UnsupportedVersion;
    }
    if (header.payload_size > kMaxControlPayloadSize) {
        return ControlHeaderValidation::PayloadTooLarge;
    }
    return ControlHeaderValidation::Valid;
}

std::vector<std::uint8_t> encodeControlFrame(Opcode opcode, std::uint32_t request_id,
                                             const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxControlPayloadSize) {
        throw std::length_error("control payload is too large");
    }
    const ControlHeader header{kControlMagic, kControlVersion, opcode, request_id,
                               static_cast<std::uint32_t>(payload.size())};
    std::vector<std::uint8_t> frame = encodeControlHeader(header);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

bool isKnownRequestOpcode(Opcode opcode) noexcept {
    switch (opcode) {
    case Opcode::RegisterNode:
    case Opcode::UnregisterNode:
    case Opcode::AdvertiseTopic:
    case Opcode::UnadvertiseTopic:
    case Opcode::SubscribeTopic:
    case Opcode::UnsubscribeTopic:
    case Opcode::ResolveEndpoint:
    case Opcode::ListNodes:
    case Opcode::ListTopics:
    case Opcode::QueryTopic:
    case Opcode::AttachHeartbeat:
    case Opcode::Heartbeat:
        return true;
    case Opcode::Response:
        return false;
    }
    return false;
}

std::vector<std::uint8_t> encodeResponsePayload(ErrorCode error, const std::string& message,
                                                const std::vector<std::uint8_t>& body) {
    PayloadWriter writer;
    writer.writeU16(static_cast<std::uint16_t>(error));
    writer.writeString(message);
    writer.writeBytes(body);
    return writer.take();
}

std::optional<ResponseEnvelope> decodeResponsePayload(const std::vector<std::uint8_t>& payload) {
    PayloadReader reader{payload};
    std::uint16_t error = 0;
    ResponseEnvelope envelope;
    if (!reader.readU16(error) || !reader.readString(envelope.message) ||
        !reader.readRemaining(envelope.body)) {
        return std::nullopt;
    }
    envelope.error = static_cast<ErrorCode>(error);
    return envelope;
}

} // namespace mw::detail
