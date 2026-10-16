#pragma once

#include <ostream>

namespace rhc {

struct IrModule;

// Opsi dump IR text (spesifikasi Bagian 9.2).
// Format S-expression konsisten dengan dump parser.
//   default : ringkas
//   full    : verbose penuh (layout tipe, constant pool, seluruh metadata)
//   noLoc   : strip source location
struct IrDumpOptions {
    bool full = false;
    bool noLoc = false;
};

// Dump deterministik: fungsi mengikuti urutan deklarasi sumber; block/value
// mengikuti urutan insertion (spesifikasi Bagian 9.2).
void dumpIrModule(std::ostream& out, const IrModule& module,
                  const IrDumpOptions& options = {});

} // namespace rhc
