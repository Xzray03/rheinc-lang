#include "RhSemantic/BuiltinRegistry.h"

#include <string>
#include <utility>
#include <vector>

namespace rhc {

void BuiltinRegistry::registerBuiltinTypes(SemanticContext& context) {
    static constexpr BuiltinTypeKind kinds[] = {
        BuiltinTypeKind::Bilangan,
        BuiltinTypeKind::Bilangan8,
        BuiltinTypeKind::Bilangan16,
        BuiltinTypeKind::Bilangan32,
        BuiltinTypeKind::Bilangan64,
        BuiltinTypeKind::BilanganTanpaTanda8,
        BuiltinTypeKind::BilanganTanpaTanda16,
        BuiltinTypeKind::BilanganTanpaTanda32,
        BuiltinTypeKind::BilanganTanpaTanda64,
        BuiltinTypeKind::Desimal,
        BuiltinTypeKind::Logika,
        BuiltinTypeKind::Karakter,
        BuiltinTypeKind::Teks,
        BuiltinTypeKind::Hampa,
    };
    for (BuiltinTypeKind kind : kinds) {
        BuiltinType* type = context.types().getBuiltinType(kind);
        Symbol* symbol = context.createSymbol(SymbolKind::BuiltinType, type->name, type,
                                              nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(symbol);
    }

    Type* hampa = context.types().getBuiltinType(BuiltinTypeKind::Hampa);
    Type* bilangan = context.types().getBuiltinType(BuiltinTypeKind::Bilangan);
    Type* logika = context.types().getBuiltinType(BuiltinTypeKind::Logika);
    Type* teks = context.types().getBuiltinType(BuiltinTypeKind::Teks);
    Type* error = context.types().getErrorType();

    auto functionType = [&](Type* returnType) {
        return static_cast<FunctionType*>(context.types().getFunctionType({}, returnType, true));
    };

    auto addObject = [&](std::string name,
                         std::vector<std::pair<std::string, Type*>> methods,
                         std::vector<std::string> typeParameterNames = {}) {
        BuiltinObjectType* object = context.types().getBuiltinObjectType(name);
        for (const std::string& paramName : typeParameterNames) {
            object->typeParams.push_back(context.types().getTypeVariable(paramName));
        }
        for (auto& [methodName, returnType] : methods) {
            // nullptr = self-type (method mengembalikan object itu sendiri,
            // mis. transpose/adjoin/invers pada matriks).
            object->members.emplace(methodName,
                                    functionType(returnType ? returnType
                                                            : object));
        }
        Symbol* symbol = context.createSymbol(SymbolKind::BuiltinType, object->name, object,
                                              nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(symbol);
    };

    // Opsi / Hasil are built-in generic sum types (constructor patterns + pilih).
    {
        TypeVariableType* t = context.types().getTypeVariable("T");
        TypeVariableType* e = context.types().getTypeVariable("E");
        auto* opsi = context.types().createSumType("Opsi", nullptr, false, {t});
        opsi->cases.push_back(SumType::Case{"ADA", {SumType::Field{"nilai", t}}, nullptr, nullptr});
        opsi->cases.push_back(SumType::Case{"KOSONG", {}, nullptr, nullptr});
        Symbol* opsiSymbol = context.createSymbol(SymbolKind::Enum, "Opsi", opsi,
                                                  nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(opsiSymbol);
        // Case symbols (preseden: jenis_daftar) — klien dapat resolve
        // varian dari nama (mis. RhAST untuk subjek pilih).
        for (const SumType::Case& kase : opsi->cases) {
            Symbol* caseSymbol = context.createSymbol(
                SymbolKind::EnumConstant, kase.name, opsi, nullptr,
                SourceRange{}, &context.globalScope());
            context.globalScope().insert(caseSymbol);
        }
        auto* hasil = context.types().createSumType("Hasil", nullptr, false, {t, e});
        hasil->cases.push_back(SumType::Case{"SUKSES", {SumType::Field{"nilai", t}}, nullptr, nullptr});
        hasil->cases.push_back(SumType::Case{"GAGAL", {SumType::Field{"pesan", e}}, nullptr, nullptr});
        Symbol* hasilSymbol = context.createSymbol(SymbolKind::Enum, "Hasil", hasil,
                                                   nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(hasilSymbol);
    }

    addObject("Tugas", {{"gabung", error}, {"tunggu", error}, {"status", teks}}, {"T"});
    addObject("Pekerja", {{"gabung", error}, {"tunggu", error}, {"status", teks}, {"lepas", hampa}}, {"T"});
    addObject("Mutex", {{"kunci", hampa}, {"buka", hampa}});
    addObject("Atomik", {{"baca", error}, {"set", hampa}, {"tambah", error}, {"kurang", error}, {"tukar", error}});
    addObject("Saluran", {{"kirim", hampa}, {"terima", error}, {"tutup", hampa}}, {"T"});
    addObject("Kondisi", {{"tunggu", hampa}, {"notifikasi", hampa}, {"bangunkan_semua", hampa}});
    addObject("Semaphore", {{"tunggu", hampa}, {"lepas", hampa}});
    addObject("Barrier", {{"tunggu", hampa}});
    addObject("Sinyal", {{"kirim", hampa}, {"tunggu", error}, {"blokir", error}, {"abaikan", hampa}});
    addObject("Pembangkit", {{"masih_ada", logika}, {"berikutnya", error}, {"reset", hampa}}, {"T", "R"});
    addObject("Iterator", {}, {"T"});   // pointer ke state node arena (8B)
    addObject("Daftar", {{"tambah", hampa}, {"hapus", hampa}, {"ukuran", bilangan}, {"kosong", logika}, {"cari", bilangan}, {"ambil", error}, {"atur", hampa}, {"bersihkan", hampa}}, {"T"});
    addObject("Antrian", {{"push", hampa}, {"pop", error}, {"ukuran", bilangan}, {"kosong", logika}, {"depan", error}, {"belakang", error}}, {"T"});
    addObject("Tumpukan", {{"push", hampa}, {"pop", error}, {"ukuran", bilangan}, {"kosong", logika}, {"atas", error}}, {"T"});
    addObject("desimal_tetap", {{"ke_teks", teks}, {"ke_desimal", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}});
    addObject("bilangan_kompleks", {{"ke_teks", teks}, {"magnitudo", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}});
    addObject("bilangan_tanpa_batas", {{"ke_teks", teks}, {"ke_bilangan", bilangan}});
    addObject("matriks", {{"transpose", nullptr}, {"lu", nullptr}, {"qr", nullptr}, {"determinan", context.types().getGenericInstantiation(dynamic_cast<SumType*>(context.globalScope().lookup("Opsi")->type), {{"T", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}})}, {"invers", nullptr}, {"jumlah", context.types().getTypeVariable("T")}, {"rata_rata", context.types().getTypeVariable("T")}, {"minimum", context.types().getTypeVariable("T")}, {"maksimum", context.types().getTypeVariable("T")}, {"ke_teks", teks}, {"det", context.types().getGenericInstantiation(dynamic_cast<SumType*>(context.globalScope().lookup("Opsi")->type), {{"T", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}})}, {"trace", context.types().getGenericInstantiation(dynamic_cast<SumType*>(context.globalScope().lookup("Opsi")->type), {{"T", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}})}, {"rank", context.types().getBuiltinType(BuiltinTypeKind::Bilangan)}, {"lu", error}, {"qr", error}, {"iterasi", error}});
        // invers mengembalikan Opsi<matriks> (gagal jika singular) — bukan
        // matriks langsung (corpus: pilih a.invers() kasus KOSONG).
        {
            BuiltinObjectType* matriksObj =
                context.types().getBuiltinObjectType("matriks");
            Type* opsiSum = nullptr;   // lookup (lokal di registerBuiltinSum)
            if (Symbol* sym = context.globalScope().lookup("Opsi")) {
                opsiSum = sym->type;
            }
            Type* opsiMatriks =
                opsiSum
                    ? context.types().getGenericInstantiation(
                          static_cast<SumType*>(opsiSum),
                          {{"T", matriksObj}})
                    : static_cast<Type*>(matriksObj);  // fallback aman
            // Iterator matriks (corpus H): a.iterasi() -> it.selesai() /
            // it.berikutnya() -> posisi_matriks {baris, kolom, nilai}.
            {
                BuiltinObjectType* posisi =
                    context.types().getBuiltinObjectType("posisi_matriks");
                posisi->members.emplace("baris", bilangan);
                posisi->members.emplace("kolom", bilangan);
                posisi->members.emplace(
                    "nilai", context.types().getTypeVariable("T"));
                Symbol* posisiSymbol = context.createSymbol(
                    SymbolKind::BuiltinType, posisi->name, posisi, nullptr,
                    SourceRange{}, &context.globalScope());
                context.globalScope().insert(posisiSymbol);

                addObject("iterator_matriks",
                          {{"selesai", logika}, {"berikutnya", posisi}});
                BuiltinObjectType* iterMatriks =
                    context.types().getBuiltinObjectType("iterator_matriks");
                matriksObj->members["iterasi"] = static_cast<FunctionType*>(
                    context.types().getFunctionType({}, iterMatriks, true));
            }
            matriksObj->members["lu"] = static_cast<FunctionType*>(
                context.types().getFunctionType({}, opsiMatriks, true));
            matriksObj->members["qr"] = static_cast<FunctionType*>(
                context.types().getFunctionType({}, opsiMatriks, true));
            matriksObj->members["invers"] = static_cast<FunctionType*>(
                context.types().getFunctionType({}, opsiMatriks, true));
        }
}

} // namespace rhc
