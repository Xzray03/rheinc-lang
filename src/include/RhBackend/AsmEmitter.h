// AsmEmitter.h - Antarmuka seam per target (pola IrTargetProvider).
//
// v1: satu implementasi, X86_64SysVEmitter. Inti RhBackend target-
// agnostic tipis berkomunikasi lewat antarmuka ini agar multi-target
// (nanti) tidak menulis ulang inti.

#pragma once

#include "RhBackend/RhBackendError.h"

#include "RhIR/FinalIr.h"

namespace rhc {

class AsmBuilder;

class AsmEmitter {
public:
    virtual ~AsmEmitter() = default;

    // Emit seluruh isi modul (fungsi + konstanta) ke AsmBuilder.
    // Mengisi errors dan mengembalikan false bila ada opcode/tipe
    // yang belum didukung.
    virtual bool emitModule(const FinalIr& finalIr, AsmBuilder& out,
                            std::vector<RhBackendError>& errors) = 0;
};

}  // namespace rhc
