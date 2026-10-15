#pragma once

#include <cstdint>

namespace rhc {

using FileOffset = std::uint32_t;
using SourceLocation = std::uint64_t;

class FileID {
public:
    FileID() = default;
    explicit FileID(unsigned value) : value_(value) {}
    unsigned value() const { return value_; }
    bool valid() const { return value_ != invalidValue; }

    friend bool operator==(FileID lhs, FileID rhs) { return lhs.value_ == rhs.value_; }
    friend bool operator!=(FileID lhs, FileID rhs) { return !(lhs == rhs); }

    static constexpr unsigned invalidValue = 0xFFFFFFFFu;

private:
    unsigned value_ = invalidValue;
};

inline SourceLocation makeSourceLocation(FileID file, FileOffset offset) {
    return (static_cast<SourceLocation>(file.value()) << 32u) |
           static_cast<SourceLocation>(offset);
}

inline FileID getFileIDFromLocation(SourceLocation loc) {
    return FileID(static_cast<unsigned>(loc >> 32u));
}

inline FileOffset getFileOffsetFromLocation(SourceLocation loc) {
    return static_cast<FileOffset>(loc & 0xFFFFFFFFull);
}

struct SourceRange {
    SourceLocation begin = 0;
    std::uint32_t length = 0;
};

} // namespace rhc
