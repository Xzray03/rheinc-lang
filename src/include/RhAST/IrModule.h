#pragma once

#include "RhAST/IrConstant.h"
#include "RhAST/IrFunction.h"
#include "RhAST/IrIds.h"
#include "RhAST/IrType.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhc {

// Pemetaan semantic TypeId -> IrTypeId (spesifikasi Bagian 5.1 & 5.5).
// Kunci adalah pointer Type milik SemanticContext (non-owning; hidup
// selama caller menjaga SemanticAnalyzer). Gagal memetakan tipe
// -> Error ast.type_mapping_failed.
class TypeMapping {
public:
    // Mendaftarkan pemetaan. Mengembalikan false jika kunci sudah terpetakan
    // ke nilai BERBEDA (indikasi bug translator — pemetaan harus stabil).
    // Definisi inline: layout modul tidak mencantumkan IrModule.cpp.
    bool map(const void* semanticType, TypeId irType) {
        const auto [it, inserted] =
            entries_.emplace(semanticType, irType);
        (void)it;
        return inserted || it->second == irType;
    }
    TypeId lookup(const void* semanticType) const {  // InvalidType bila tidak ada
        const auto it = entries_.find(semanticType);
        return it != entries_.end() ? it->second : InvalidType;
    }
    bool contains(const void* semanticType) const {
        return entries_.find(semanticType) != entries_.end();
    }
    std::size_t size() const { return entries_.size(); }

private:
    std::unordered_map<const void*, TypeId> entries_;
};

// Pemetaan symbol semantic -> IR (spesifikasi Bagian 2.2).
// Fungsi -> FuncId; variabel global -> ValueId milik fungsi
// inisialisasi global (detail lowering ditentukan saat implementasi
// fase lowering).
class SymbolMapping {
public:
    void mapFunction(const void* symbol, FuncId func) {
        functions_[symbol] = func;
    }
    void mapGlobal(const void* symbol, ValueId value) {
        globals_[symbol] = value;
    }
    FuncId lookupFunction(const void* symbol) const {  // InvalidFunc bila tidak ada
        const auto it = functions_.find(symbol);
        return it != functions_.end() ? it->second : InvalidFunc;
    }
    ValueId lookupGlobal(const void* symbol) const {   // InvalidValue bila tidak ada
        const auto it = globals_.find(symbol);
        return it != globals_.end() ? it->second : InvalidValue;
    }

    std::size_t functionCount() const { return functions_.size(); }
    std::size_t globalCount() const { return globals_.size(); }

private:
    std::unordered_map<const void*, FuncId> functions_;
    std::unordered_map<const void*, ValueId> globals_;
};

// Storage variabel global mutable (irVersion 3): read = Load(@Gi),
// write = Store(@Gi, v). Inisialisasi non-literal dilakukan fungsi
// implisit .__global_init yang membangun nilai dan Store ke @Gi.
struct IrGlobal {
    std::string name;
    TypeId type = InvalidType;   // tipe value (bukan pointer)
    SourceLocation loc = 0;
};

// Modul IR hasil kerja RhAST (spesifikasi Bagian 2.2).
// IR self-contained: pass lain dapat memproses IR tanpa membawa object
// semantic; TypeMapping/SymbolMapping hanyalah metadata translasi.
struct IrModule {
    std::uint32_t irVersion = 1;        // lihat IrVersion.h / spesifikasi Bagian 12
    std::string sourcePath;
    std::vector<IrFunction> functions;
    std::vector<IrType> types;          // type table per module
    std::vector<IrConstant> constants;  // constant pool per module
    std::vector<IrGlobal> globalVars;               // storage global mutable
    std::unordered_map<std::string, std::uint32_t> globalVarIndex;  // name -> idx
    std::unordered_map<std::string, FuncId> globals;  // symbol table global
    TypeMapping typeMapping;            // semantic TypeId -> IrTypeId
    SymbolMapping symbolMapping;        // symbol semantic -> IR
};

} // namespace rhc
