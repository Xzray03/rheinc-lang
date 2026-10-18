#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Pattern.h"
#include "RhSemantic/Symbol.h"
#include "RhParser/Stmt.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace rhc {

// Lowering pilih (spesifikasi Bagian 4.4): subjek integer ->
// terminator SwitchInt di block saat ini. Satu block per kasus —
// kasus tanpa body (fall-through) jadi block kosong yang Br ke kasus
// berikutnya, membuat fall-through EKSPLISIT di IR. Body yang berakhir
// normal Br ke join; variabel yang berbeda antar jalur digabung phi.
// "berhenti" di dalam pilih keluar ke join (konteks break khusus pilih
// dengan header InvalidBlock — "lanjut" di dalam pilih error).
bool AstToIrTranslator::Impl::lowerPilihStatement(const PilihStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;

    const NameValueMap preEnv = env.back().vars;

    // Subjek & nilai kasus dievaluasi di block saat ini.
    const ValueId subject = lowerExpr(stmt->subject);
    if (subject == InvalidValue) return false;

    struct CaseTarget {
        std::uint64_t tag;      // nilai konstanta kasus (int/tag sum, 64-bit)
        const CaseClause* clause;
    };
    std::vector<CaseTarget> targets;
    const CaseClause* defaultClause = nullptr;
    bool anyConstructor = false;
    for (const CaseClause* clause : stmt->clauses) {
        for (const PatternNode* pattern : clause->patterns) {
            if (pattern->kind == AstKind::ConstructorPattern) {
                anyConstructor = true;
            }
        }
        if (clause->isDefault) {
            if (defaultClause) {
                return error(AstIrErrorCode::MalformedShape, clause,
                             "pilih dengan dua kasus bawaan");
            }
            defaultClause = clause;
        }
    }

    TypeId subjectTypeId =
        builder.function(currentFunc).values[subject].type;
    bool subjectIsSum =
        subjectTypeId != InvalidType &&
        module.types[subjectTypeId].kind == IrTypeKind::Sum;
    // Subjek hasil inferensi-gagal (temuan RhSemantic): resolve tipe
    // sum BUILTIN dari nama varian — case builtin (ADA/KOSONG) tidak
    // terdaftar sebagai symbol, tapi sum pembentuknya (Opsi<T>,
    // Hasil<T,E>) ADA di global scope (SymbolKind::Enum). Iterasi
    // daftar nama sum builtin; varian cocok = kandidat. Payload
    // TypeVariable terkonkretisasi default (DEVIATIONS D-2).
    if (!subjectIsSum && anyConstructor) {
        std::string ctorName;
        for (const CaseClause* clause : stmt->clauses) {
            for (const PatternNode* pattern : clause->patterns) {
                if (pattern->kind == AstKind::ConstructorPattern) {
                    ctorName = static_cast<const ConstructorPattern*>(
                                   pattern)->constructorName;
                    break;
                }
            }
            if (!ctorName.empty()) break;
        }
        static const char* kBuiltinSums[] = {"Opsi", "Hasil"};
        for (const char* sumName : kBuiltinSums) {
            const Symbol* symbol = lookupGlobalSymbol(sumName);
            if (!symbol || !symbol->type ||
                symbol->type->kind() != TypeKind::Sum) {
                continue;
            }
            const auto* sum = static_cast<const SumType*>(symbol->type);
            const bool hasVariant =
                std::any_of(sum->cases.begin(), sum->cases.end(),
                            [&](const SumType::Case& kase) {
                                return kase.name == ctorName;
                            });
            if (!hasVariant) continue;
            subjectTypeId = mapType(sum, stmt->subject);
            if (failed) return false;
            subjectIsSum = subjectTypeId != InvalidType &&
                           module.types[subjectTypeId].kind ==
                               IrTypeKind::Sum;
            break;
        }
    }
    // Scope fungsi (dipakai pembangunan target & binding kasus).
    // BY VALUE: referensi ke module.types akan dangling bila mapType/
    // internType mem-push tipe baru di tengah lowering (vector realloc).
    const IrType sumTypeValue =
        subjectIsSum ? module.types[subjectTypeId] : IrType();
    const IrType* sumTypePtr = subjectIsSum ? &sumTypeValue : nullptr;

    if (anyConstructor && !subjectIsSum) {
        return error(AstIrErrorCode::MatchNonSum, stmt->subject,
                     "pattern konstruktor pada subjek non-sum-type");
    }
    if (anyConstructor) {
        // ===== MatchTag: pasangan (tag, block) per varian =====
        const IrType& sumType = *sumTypePtr;  // referensi ke copy lokal
        for (const CaseClause* clause : stmt->clauses) {
            if (clause->isDefault) continue;
            for (const PatternNode* pattern : clause->patterns) {
                const auto* ctor =
                    static_cast<const ConstructorPattern*>(pattern);
                int variantIndex = -1;
                for (std::size_t v = 0; v < sumType.variants.size(); ++v) {
                    if (sumType.variants[v].name == ctor->constructorName) {
                        variantIndex = static_cast<int>(v);
                        break;
                    }
                }
                if (variantIndex < 0) {
                    return error(AstIrErrorCode::MalformedShape, pattern,
                                 "varian '" + ctor->constructorName +
                                 "' tidak ada pada " + sumType.name);
                }
                targets.push_back(
                    {static_cast<std::uint64_t>(
                         sumType.variants[variantIndex].tagValue),
                     clause});
            }
        }
    } else {
        // ===== SwitchInt: konstanta integer per pattern ekspresi =====
        for (const CaseClause* clause : stmt->clauses) {
            if (clause->isDefault) continue;
            for (const PatternNode* pattern : clause->patterns) {
                if (pattern->kind != AstKind::ExpressionPattern) {
                    return error(AstIrErrorCode::UnsupportedNode, pattern,
                                 "hanya pattern ekspresi konstanta yang "
                                 "didukung pada SwitchInt");
                }
                const auto* exprPattern =
                    static_cast<const ExpressionPattern*>(pattern);
                const ValueId value = lowerExpr(exprPattern->expression);
                if (value == InvalidValue) return false;
                const IrFunction& fn = builder.function(currentFunc);
                const IrInstr& instr = fn.instrs[fn.values[value].payload];
                std::uint64_t tagValue = 0;
                if (instr.op != IrOpcode::ConstInt ||
                    !decodeConstInt(instr, tagValue)) {
                    return error(AstIrErrorCode::MalformedShape, pattern,
                                 "nilai kasus pilih harus konstanta integer");
                }
                targets.push_back({tagValue, clause});
            }
        }
    }

    // Block per kasus (berurutan sumber) + join. Bawaan = kasus biasa.
    std::unordered_map<const CaseClause*, BlockId> clauseBlocks;
    for (const CaseClause* clause : stmt->clauses) {
        clauseBlocks[clause] = builder.createBlock(currentFunc, "kasus");
    }
    const BlockId joinB = builder.createBlock(currentFunc, "pilih.join");
    const BlockId defaultBlock = defaultClause
        ? clauseBlocks[defaultClause]
        : joinB;   // tanpa bawaan: tidak cocok -> jatuh ke join

    // Terminator SwitchInt di block subjek.
    std::vector<IrOperand> operands;
    operands.push_back(IrOperand::value(subject));
    const IrOpcode switchOp = anyConstructor ? IrOpcode::MatchTag
                                             : IrOpcode::SwitchInt;
    for (const CaseTarget& target : targets) {
        operands.push_back(IrOperand::value(
            builder.emit(IrOpcode::ConstInt, subjectTypeId,
                         encodeConstIntOperands(target.tag),
                         stmt->range.begin)));
        operands.push_back(IrOperand::block(clauseBlocks[target.clause]));
    }
    operands.push_back(IrOperand::block(defaultBlock));
    builder.setTerminator(currentFunc, currentBlock, switchOp,
                          std::move(operands), stmt->range.begin);

    // Konteks break untuk "berhenti" di dalam kasus.
    loopStack.push_back({InvalidBlock, joinB, {}});

    // Lower tiap kasus berurutan. Pred join = kasus yang Br-nya
    // mengarah ke join (kasus terakhir dari run fall-through / bawaan).
    std::vector<std::pair<BlockId, NameValueMap>> joinPreds;
    for (std::size_t i = 0; i < stmt->clauses.size(); ++i) {
        const CaseClause* clause = stmt->clauses[i];
        const BlockId block = clauseBlocks[clause];
        env.back().vars = preEnv;   // tiap kasus mulai dari env sama
        setCurrent(block);
        // MatchTag: bind payload varian ke nama pada ConstructorPattern.
        if (anyConstructor) {
            for (const PatternNode* pattern : clause->patterns) {
                const auto* ctor =
                    static_cast<const ConstructorPattern*>(pattern);
                for (std::size_t v = 0; v < sumTypePtr->variants.size(); ++v) {
                    if (sumTypePtr->variants[v].name != ctor->constructorName) {
                        continue;
                    }
                    for (const PatternBinding* binding : ctor->bindings) {
                        if (binding->nestedPattern) {
                            error(AstIrErrorCode::UnsupportedNode, pattern,
                                  "nested pattern pada binding belum "
                                  "didukung");
                            loopStack.pop_back();
                            return false;
                        }
                        const TypeId payloadType =
                            sumTypePtr->variants[v].payloadType;
                        if (payloadType == InvalidType) {
                            error(AstIrErrorCode::MalformedShape, pattern,
                                  "binding pada varian tanpa payload");
                            loopStack.pop_back();
                            return false;
                        }
                        const ValueId payload = builder.emit(
                            IrOpcode::Extract, payloadType,
                            {IrOperand::value(subject), IrOperand::imm(0)},
                            pattern->range.begin, binding->name);
                        env.back().vars[binding->name] = payload;
                    }
                }
            }
        }
        if (clause->body && !lowerStmt(clause->body)) {
            loopStack.pop_back();
            return false;
        }
        if (blockOpen()) {
            // Badan KOSONG -> fallthrough ke kasus berikutnya (makna
            // `kasus 10: kasus 9:`). Badan BERISI -> setelah statement
            // lompat ke join (semantik pilih Rheinc: hanya kasus cocok
            // yang dieksekusi). Sebelumnya badan berisi juga fallthrough,
            // sehingga semua kasus setelah yang cocok ikut tereksekusi.
            const bool emptyBody = (clause->body == nullptr);
            const BlockId next =
                (emptyBody && i + 1 < stmt->clauses.size())
                    ? clauseBlocks[stmt->clauses[i + 1]]
                    : joinB;
            builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                                  {IrOperand::block(next)},
                                  clause->range.begin);
            if (next == joinB) {
                joinPreds.emplace_back(currentBlock, env.back().vars);
            }
        }
        // Body berakhir terminator (kembalikan/berhenti): tidak mengalir.
    }
    loopStack.pop_back();

    if (joinPreds.empty()) {
        builder.setTerminator(currentFunc, joinB, IrOpcode::Unreachable, {},
                              stmt->range.begin);
        setCurrent(InvalidBlock);
        return !builder.broken();
    }
    std::vector<std::pair<BlockId, const NameValueMap*>> predRefs;
    for (const auto& [block, snapshot] : joinPreds) {
        predRefs.emplace_back(block, &snapshot);
    }
    env.back().vars = mergeAtJoin(joinB, predRefs, preEnv);
    setCurrent(joinB);
    return !builder.broken();
}

} // namespace rhc
