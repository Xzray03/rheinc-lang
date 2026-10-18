// RegAlloc.h - Antarmuka register allocator (pola seam desain awal).
//
// GraphColoringRegAlloc (v1): Chaitin-style — liveness dataflow, graf
// intererensi, simplify + optimistic spill + select. Kelas register: 5
// register callee-saved (rbx, r12-r15) — aman dipakai nilai yang hidup
// menyeberang call. Nilai yang tak ter-color -> spill ke slot (naive).
//
// Nilai ELIGIBLE: skalar <=8 byte ber-kind Int/Bool/Pointer/FnPtr.
// Float/agregat tetap di memori (slot) pada v1.

#pragma once

#include "RhBackend/RhBackendError.h"

#include "RhIR/IrFunction.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhc {

// Home sebuah SSA value hasil alokasi.
struct RegLoc {
    bool inReg = false;          // true = register, false = slot memori
    std::string reg;             // nama register (mis. "rbx") bila inReg
    std::int32_t slotOff = 0;    // offset slot bila !inReg (diisi emitter)
};

struct RegAllocResult {
    bool ok = false;
    // Home per ValueId (hanya value eligible; lainnya tetap slot naive).
    std::unordered_map<ValueId, RegLoc> homes;
    // Register callee-saved yang TERPAKAI (untuk prolog/epilog).
    std::vector<std::string> usedCalleeSaved;
};

class RegAlloc {
public:
    virtual ~RegAlloc() = default;

    // Alokasikan register untuk tiap value eligible pada fn.
    // Hasil: homes berisi assignment register; value tak ter-color
    // tidak ada di homes (emitter memperlakukannya sebagai slot).
    virtual RegAllocResult allocate(const IrModule& module,
                                    const IrFunction& fn,
                                    std::vector<RhBackendError>& errors) = 0;
};

// Factory: graph coloring (Chaitin) — k = 5 callee-saved GP.
RegAlloc* createGraphColoringRegAlloc();

}  // namespace rhc
