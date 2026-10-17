#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// CommonSubexpressionElimination (CSE) global berbasis dominator tree.
//
// Untuk setiap instruksi MURNI (irOpIsPureForDce) yang menghasilkan SSA
// value, pass membangun kunci ekspresi (opcode + resultType + operand).
// Bila kunci yang sama sudah tersedia pada value dari block yang
// mendominasi (availability dijaga oleh traversal preorder pohon
// dominator dengan hash map bercakup: tambah saat masuk block, tarik
// kembali saat keluar), seluruh USE instruksi redundan dialihkan ke
// value definisi dominan, lalu instruksi mati disapu oleh
// IrEditor::removeDeadInstrs di akhir fungsi.
//
// Kanonisasi komutatif: Add/Mul/BitAnd/BitOr/BitXor/CmpEq/CmpNe dengan
// dua operand ValueV yang sama (id terurut) menghasilkan satu kunci.
// Instruksi non-murni, Phi, dan terminator tidak disentuh.
//
// Aman untuk SSA: def dominan mengalir ke semua use yang sebelumnya
// di-dom-inasi def redundan -> use baru pasti di-dom-inasi juga.
class CommonSubexpressionElimination final : public IrPass {
public:
    std::string_view name() const override { return "cse"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
