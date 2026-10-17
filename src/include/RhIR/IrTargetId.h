#pragma once

#include <cstdint>
#include <string>

namespace rhc {

enum class IrArchitecture : std::uint8_t {
    X86_64,
};

enum class IrOperatingSystem : std::uint8_t {
    Linux,
};

enum class IrAbi : std::uint8_t {
    SystemV,
};

enum class IrEndianness : std::uint8_t {
    Little,
};

struct IrTargetId {
    IrArchitecture architecture;
    IrOperatingSystem os;
    IrAbi abi;
    IrEndianness endianness;

    constexpr bool operator==(const IrTargetId&) const = default;

    static constexpr IrTargetId systemVAmd64() noexcept {
        return IrTargetId{
            IrArchitecture::X86_64,
            IrOperatingSystem::Linux,
            IrAbi::SystemV,
            IrEndianness::Little,
        };
    }

    std::string toString() const;
};

}  // namespace rhc
