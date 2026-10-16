#pragma once

#include <cstdint>

namespace rhc {

// Versi format IR. Selalu tercetak di dump sebagai
// (module ... ir-version=N ...) — spesifikasi Bagian 12.
// Bump manual setiap perubahan breaking IR (hapus/ubah opcode,
// ubah layout encoding, ubah struktur dump).
inline constexpr std::uint32_t IrFormatVersion = 5;

} // namespace rhc
