
#include "RhAST/IrBuilder.h"
#include "RhCore/DiagnosticEngine.h"
#include "RhCore/SourceManager.h"

#include <cassert>
#include <iostream>

using namespace rhc;

int main() {
    SourceManager sm;
    DiagnosticEngine diag(sm);
    IrModule module;
    AstToIrOptions opts;
    opts.verifyAfterEachMutation = true;  // mode debug penuh
    IrBuilder builder(module, diag, opts);

    // --- tipe ---
    IrType i32;
    i32.kind = IrTypeKind::Int;
    i32.sizeBytes = 4;
    i32.alignBytes = 4;
    i32.isSigned = true;
    const TypeId tI32 = builder.internType(i32);
    assert(builder.internType(i32) == tI32);  // dedup struktural
    IrType named;
    named.kind = IrTypeKind::Struct;
    named.name = "Titik";
    const TypeId tTitik1 = builder.internType(named);
    const TypeId tTitik2 = builder.internType(named);
    assert(tTitik1 != tTitik2);  // named type tidak di-dedup

    // --- fungsi: if (c) x=1 else x=2; return x ---
    const FuncId f = builder.createFunction("uji", InvalidType);
    const BlockId entry = builder.createBlock(f, "entry");
    const BlockId thenB = builder.createBlock(f, "then");
    const BlockId elseB = builder.createBlock(f, "else");
    const BlockId merge = builder.createBlock(f, "merge");

    builder.setInsertPoint(f, entry);
    const ValueId c = builder.emit(IrOpcode::ConstBool, tI32,
                                   {IrOperand::imm(1)}, 0, "c");
    builder.setTerminator(f, entry, IrOpcode::CondBr,
                          {IrOperand::value(c), IrOperand::block(thenB),
                           IrOperand::block(elseB)});

    builder.setInsertPoint(f, thenB);
    const ValueId x1 = builder.emit(IrOpcode::ConstInt, tI32,
                                    {IrOperand::imm(1)}, 0, "x");
    builder.setTerminator(f, thenB, IrOpcode::Br, {IrOperand::block(merge)});

    builder.setInsertPoint(f, elseB);
    const ValueId x2 = builder.emit(IrOpcode::ConstInt, tI32,
                                    {IrOperand::imm(2)}, 0, "x");
    builder.setTerminator(f, elseB, IrOpcode::Br, {IrOperand::block(merge)});

    builder.setInsertPoint(f, merge);
    const ValueId px = builder.createPhi(tI32, f, merge, 0, "x");
    builder.addPhiIncoming(f, px, thenB, x1);
    builder.addPhiIncoming(f, px, elseB, x2);
    const ValueId dead = builder.emit(IrOpcode::Add, tI32,
                                      {IrOperand::value(px), IrOperand::value(px)},
                                      0, "mati");
    builder.setTerminator(f, merge, IrOpcode::Ret, {IrOperand::value(px)});

    // invariant cek manual
    assert(module.functions[f].blocks[merge].preds.size() == 2);
    assert(builder.usesOf(f, x1).size() == 1);
    assert(builder.usesOf(f, x2).size() == 1);
    assert(builder.usesOf(f, c).size() == 1);
    // px dipakai: Add(2 slot) + Ret(1 slot) = 3
    assert(builder.usesOf(f, px).size() == 3);

    std::string err;
    assert(builder.verifyModule(&err));
    assert(!builder.broken());

    // --- replace terminator (edge lama harus hilang) ---
    builder.setTerminator(f, thenB, IrOpcode::Br, {IrOperand::block(elseB)},
                          0, /*replaceTerminator=*/true);
    assert(!builder.verify(f, &err));  // CFG phi kini rusak -> expected
    builder.setTerminator(f, thenB, IrOpcode::Br, {IrOperand::block(merge)},
                          0, true);
    assert(builder.verifyModule(&err));

    // --- tombstone + compact ---
    const InstrId deadInstr = module.functions[f].values[dead].payload;
    builder.markDead(f, deadInstr);
    assert(builder.usesOf(f, px).size() == 1);  // use Add hilang
    builder.compact(f);
    assert(builder.verifyModule(&err));
    assert(!builder.broken());

    // --- invariant violation harus -> broken ---
    DiagnosticEngine diag2(sm);
    IrModule m2;
    IrBuilder b2(m2, diag2, opts);
    const FuncId f2 = b2.createFunction("rusak", InvalidType);
    const BlockId bEntry = b2.createBlock(f2, "entry");
    b2.setTerminator(f2, bEntry, IrOpcode::RetVoid, {});
    b2.setTerminator(f2, bEntry, IrOpcode::RetVoid, {});  // ganda!
    assert(b2.broken());
    assert(diag2.hasErrors());
    assert(b2.emit(IrOpcode::Add, InvalidType, {}, 0, "x") ==
           InvalidValue);  // no-op aman

    // --- compact safety: value Dead ber-use harus menolak compact ---
    DiagnosticEngine diag3(sm);
    IrModule m3;
    IrBuilder b3(m3, diag3, opts);
    IrType i32Local;
    i32Local.kind = IrTypeKind::Int;
    i32Local.sizeBytes = 4;
    i32Local.isSigned = true;
    const TypeId tI32Local = b3.internType(i32Local);  // milik module m3!
    const FuncId f3 = b3.createFunction("aman", InvalidType);
    const BlockId e3 = b3.createBlock(f3, "entry");
    b3.setInsertPoint(f3, e3);
    const ValueId a = b3.emit(IrOpcode::ConstInt, tI32Local,
                              {IrOperand::imm(7)}, 0, "a");
    const ValueId s = b3.emit(IrOpcode::Add, tI32Local,
        {IrOperand::value(a), IrOperand::value(a)}, 0, "s");
    b3.setTerminator(f3, e3, IrOpcode::Ret, {IrOperand::value(s)});
    const InstrId sInstr = m3.functions[f3].values[s].payload;
    b3.markDead(f3, sInstr);
    b3.compact(f3);  // harus FATAL ditolak: 's' masih dipakai oleh Ret
    assert(b3.broken());

    std::cout << "SEMUA SMOKE TEST IrBuilder LULUS\n";
    return 0;
}
