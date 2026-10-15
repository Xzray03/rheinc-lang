#include "RhSemantic/BuiltinRegistry.h"

#include <string>
#include <utility>
#include <vector>

namespace rhc {

void BuiltinRegistry::registerAll(SemanticContext& context) {
    registerBuiltinTypes(context);
    registerBuiltinFunctions(context);
}

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
            object->members.emplace(methodName, functionType(returnType));
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
    addObject("Iterator", {{"masih_ada", logika}, {"berikutnya", error}, {"reset", hampa}}, {"T"});
    addObject("Daftar", {{"tambah", hampa}, {"hapus", hampa}, {"ukuran", bilangan}, {"kosong", logika}, {"cari", bilangan}, {"ambil", error}, {"atur", hampa}, {"bersihkan", hampa}}, {"T"});
    addObject("Antrian", {{"push", hampa}, {"pop", error}, {"ukuran", bilangan}, {"kosong", logika}, {"depan", error}, {"belakang", error}}, {"T"});
    addObject("Tumpukan", {{"push", hampa}, {"pop", error}, {"ukuran", bilangan}, {"kosong", logika}, {"atas", error}}, {"T"});
    addObject("desimal_tetap", {{"ke_teks", teks}, {"ke_desimal", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}});
    addObject("bilangan_kompleks", {{"ke_teks", teks}, {"magnitudo", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}});
    addObject("bilangan_tanpa_batas", {{"ke_teks", teks}, {"ke_bilangan", bilangan}});
    addObject("matriks", {{"transpose", error}, {"determinan", error}, {"invers", error}, {"jumlah", error}, {"rata_rata", error}, {"minimum", error}, {"maksimum", error}, {"ke_teks", teks}, {"det", context.types().getGenericInstantiation(dynamic_cast<SumType*>(context.globalScope().lookup("Opsi")->type), {{"T", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}})}, {"trace", context.types().getGenericInstantiation(dynamic_cast<SumType*>(context.globalScope().lookup("Opsi")->type), {{"T", context.types().getBuiltinType(BuiltinTypeKind::Desimal)}})}, {"rank", context.types().getBuiltinType(BuiltinTypeKind::Bilangan)}, {"lu", error}, {"qr", error}, {"iterasi", error}});
}

void BuiltinRegistry::registerBuiltinFunctions(SemanticContext& context) {
    Type* hampa = context.types().getBuiltinType(BuiltinTypeKind::Hampa);
    Type* bilangan = context.types().getBuiltinType(BuiltinTypeKind::Bilangan);
    Type* logika = context.types().getBuiltinType(BuiltinTypeKind::Logika);
    Type* teks = context.types().getBuiltinType(BuiltinTypeKind::Teks);
    Type* karakter = context.types().getBuiltinType(BuiltinTypeKind::Karakter);
    Type* desimal = context.types().getBuiltinType(BuiltinTypeKind::Desimal);
    Type* error = context.types().getErrorType();

    auto registerVariadic = [&](const char* name, Type* returnType) {
        auto* type = context.types().getFunctionType({}, returnType, true);
        Symbol* symbol = context.createSymbol(SymbolKind::BuiltinFunction, name, type,
                                              nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(symbol);
    };

    registerVariadic("tampilkan", hampa);
    registerVariadic("panjang", bilangan);
    registerVariadic("ukuran", bilangan);
    registerVariadic("baca", teks);
    registerVariadic("ke_teks", teks);
    registerVariadic("ke_bilangan", bilangan);
    registerVariadic("ke_desimal", desimal);
    registerVariadic("ke_logika", logika);
    registerVariadic("ke_karakter", karakter);
    registerVariadic("alokasi", error);
    registerVariadic("alokasi_otomatis", error);
    registerVariadic("bebaskan", hampa);
    registerVariadic("realokasi", error);
    registerVariadic("salin", error);
    registerVariadic("isi", error);
    registerVariadic("isi_memori", hampa);
    registerVariadic("sistem", teks);
    registerVariadic("potong", teks);
    registerVariadic("gabung", error);
    registerVariadic("urutkan", hampa);
    registerVariadic("tidur", hampa);

    // Precise generic signatures for sum-type producing builtins.
    TypeVariableType* tvT = context.types().getTypeVariable("T");
    TypeVariableType* tvR = context.types().getTypeVariable("R");
    auto lookupType = [&](const char* name) -> Type* {
        Symbol* symbol = context.globalScope().lookup(name);
        return symbol ? symbol->type : nullptr;
    };
    auto* opsiType = dynamic_cast<SumType*>(lookupType("Opsi"));
    auto* saluranObject = dynamic_cast<BuiltinObjectType*>(lookupType("Saluran"));
    auto* pembangkitObject = dynamic_cast<BuiltinObjectType*>(lookupType("Pembangkit"));
    auto registerPrecise = [&](const char* name, std::vector<FunctionParamType> params,
                               Type* returnType, std::vector<TypeVariableType*> typeParams) {
        auto* type = context.types().getFunctionType(std::move(params), returnType, false,
                                                     std::move(typeParams));
        Symbol* symbol = context.createSymbol(SymbolKind::BuiltinFunction, name, type,
                                              nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(symbol);
    };
    if (opsiType && saluranObject) {
        auto* saluranT = context.types().getGenericInstantiation(saluranObject, {{"T", tvT}});
        auto* opsiT = context.types().getGenericInstantiation(opsiType, {{"T", tvT}});
        registerPrecise("saluran_terima", {{"ch", saluranT}}, opsiT, {tvT});
        registerPrecise("ambil_ada", {{"x", opsiT}}, tvT, {tvT});
        auto* pekerjaObject = dynamic_cast<BuiltinObjectType*>(lookupType("Pekerja"));
        if (pekerjaObject) {
            auto* pekerjaT = context.types().getGenericInstantiation(pekerjaObject, {{"T", tvT}});
            registerPrecise("pekerja_tunggu", {{"p", pekerjaT}}, opsiT, {tvT});
        }
        registerVariadic("saluran_ukuran", bilangan);
        // Collection query/removal produce Opsi values (not-found is KOSONG).
        auto* antrianObject = dynamic_cast<BuiltinObjectType*>(lookupType("Antrian"));
        auto* tumpukanObject = dynamic_cast<BuiltinObjectType*>(lookupType("Tumpukan"));
        auto* daftarObject = dynamic_cast<BuiltinObjectType*>(lookupType("Daftar"));
        if (antrianObject && tumpukanObject && daftarObject) {
            auto* antrianT = context.types().getGenericInstantiation(antrianObject, {{"T", tvT}});
            auto* tumpukanT = context.types().getGenericInstantiation(tumpukanObject, {{"T", tvT}});
            auto* daftarT = context.types().getGenericInstantiation(daftarObject, {{"T", tvT}});
            auto* opsiBilangan = context.types().getGenericInstantiation(opsiType, {{"T", bilangan}});
            registerPrecise("antrian_cari", {{"q", antrianT}, {"nilai", tvT}}, opsiBilangan, {tvT});
            registerPrecise("tumpukan_cari", {{"t", tumpukanT}, {"nilai", tvT}}, opsiBilangan, {tvT});
            registerPrecise("daftar_cari", {{"d", daftarT}, {"nilai", tvT}}, opsiBilangan, {tvT});
            registerPrecise("daftar_hapus_depan", {{"d", daftarT}}, opsiT, {tvT});
            registerPrecise("daftar_hapus_belakang", {{"d", daftarT}}, opsiT, {tvT});
            auto* larikT = context.types().getArrayType(tvT, std::nullopt);
            registerPrecise("antrian_ke_larik", {{"q", antrianT}}, larikT, {tvT});
            registerPrecise("tumpukan_ke_larik", {{"t", tumpukanT}}, larikT, {tvT});
        }
        // Complex number helpers (kx_*) and fixed-point alias dt.
        for (const char* name : {"kx_modulus", "kx_konjugat", "kx_argumen", "kx_polar",
                                 "kx_akar", "kx_pangkat", "kx_sin", "kx_cos", "kx_tan",
                                 "kx_exp", "kx_log"}) {
            registerVariadic(name, error);
        }
        if (auto* desimalTetap = lookupType("desimal_tetap")) {
            Symbol* dtAlias = context.createSymbol(SymbolKind::BuiltinType, "dt", desimalTetap,
                                                   nullptr, SourceRange{}, &context.globalScope());
            context.globalScope().insert(dtAlias);
        }
    }
    if (opsiType && pembangkitObject) {
        auto* pembangkitTR = context.types().getGenericInstantiation(
            pembangkitObject, {{"T", tvT}, {"R", tvR}});
        auto* opsiT2 = context.types().getGenericInstantiation(opsiType, {{"T", tvT}});
        registerPrecise("pembangkit_berikutnya", {{"g", pembangkitTR}}, opsiT2, {tvT});
        registerPrecise("pembangkit_selesai", {{"g", pembangkitTR}}, logika, {tvT, tvR});
    }
    registerVariadic("pembangkit_kirim", hampa);
    registerVariadic("pembangkit_reset", hampa);

    // ==== Aliases restored after block surgery ====
    registerVariadic("buka_file_async", error);
    registerVariadic("baca_isi_async", teks);
    registerVariadic("tulis_file_async", hampa);
    registerVariadic("tutup_file_async", hampa);
    registerVariadic("saluran_async", error);
    registerVariadic("http_get", error);
    registerVariadic("http_post", error);

    registerVariadic("atomik_baca", logika);

    registerVariadic("tulis_file", logika);
    registerVariadic("baca_file", logika);
    registerVariadic("hapus_file", logika);
    registerVariadic("ganti_nama_file", hampa);
    registerVariadic("ada_file", logika);
    registerVariadic("tutup_file", hampa);

    registerVariadic("daftar_tambah", hampa);
    registerVariadic("daftar_tambah_belakang", hampa);
    registerVariadic("daftar_tambah_depan", hampa);
    registerVariadic("daftar_ukuran", bilangan);
    registerVariadic("daftar_ke", error);
    registerVariadic("daftar_bersihkan", hampa);
    registerVariadic("daftar_kosong", logika);
    registerVariadic("daftar_dari_larik", error);
    registerVariadic("antrian_tambah", hampa);
    registerVariadic("antrian_ukuran", bilangan);
    registerVariadic("antrian_ambil", error);
    registerVariadic("antrian_bersihkan", hampa);
    registerVariadic("antrian_kosong", logika);
    registerVariadic("antrian_dari_larik", error);
    registerVariadic("tumpukan_tambah", hampa);
    registerVariadic("tumpukan_ukuran", bilangan);
    registerVariadic("tumpukan_ambil", error);
    registerVariadic("tumpukan_atas", error);
    registerVariadic("tumpukan_bersihkan", hampa);
    registerVariadic("tumpukan_kosong", logika);
    registerVariadic("tumpukan_dari_larik", error);

    registerVariadic("iterator_selesai", logika);
    registerVariadic("iterator_sebelumnya", error);

    registerVariadic("pekerja_nama", teks);
    registerVariadic("pekerja_id", bilangan);
    registerVariadic("pekerja_aktif", logika);
    registerVariadic("pekerja_lepas", hampa);
    registerVariadic("pekerja_gabung", hampa);
    registerVariadic("pekerja_prioritas", bilangan);
    registerVariadic("berhentikan_pekerja", hampa);
    registerVariadic("pekerja_tutup", hampa);
    registerVariadic("pekerja_status", teks);
    registerVariadic("kondisi_sinyal", logika);
    registerVariadic("daftar_ke_larik", error);
    registerVariadic("antrian_ke", error);
    registerVariadic("tumpukan_ke", error);
    for (const char* name : {"kecil ke besar", "besar ke kecil"}) {
        Symbol* direction = context.createSymbol(SymbolKind::GlobalVar, name, bilangan, nullptr,
                                                 SourceRange{}, &context.globalScope());
        context.globalScope().insert(direction);
    }

    registerVariadic("tangkap_sinyal", hampa);
    registerVariadic("abaikan_sinyal", hampa);
    registerVariadic("kembalikan_sinyal", hampa);
    registerVariadic("blokir_sinyal", hampa);
    registerVariadic("buka_blokir_sinyal", hampa);
    registerVariadic("kirim_sinyal", hampa);
    registerVariadic("pid_sendiri", bilangan);
    registerVariadic("tunggu_sinyal", bilangan);
    registerVariadic("reset_sinyal", hampa);


    if (auto* dtT = lookupType("desimal_tetap")) {
        for (const char* name : {"dt_abs", "dt_akar", "dt_bulat", "dt_pangkat", "dt_potong",
                                 "dt_skala", "dt_bagi", "dt_tambah", "dt_kurang", "dt_kali"}) {
            registerVariadic(name, dtT);
        }
        registerVariadic("ke_desimal_tetap", dtT);
        Symbol* larikDt = context.createSymbol(SymbolKind::BuiltinType, "larik_dt", dtT,
                                               nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(larikDt);
    }
    if (auto* kxT2 = lookupType("bilangan_kompleks")) {
        Symbol* kxAlias = context.createSymbol(SymbolKind::BuiltinType, "kx", kxT2, nullptr,
                                               SourceRange{}, &context.globalScope());
        context.globalScope().insert(kxAlias);
        registerVariadic("ke_kompleks", kxT2);
        for (const char* name : {"larik_kd", "larik_kdt"}) {
            Symbol* alias = context.createSymbol(SymbolKind::BuiltinType, name, kxT2, nullptr,
                                                 SourceRange{}, &context.globalScope());
            context.globalScope().insert(alias);
        }
    }
    if (auto* btbT2 = lookupType("bilangan_tanpa_batas")) {
        registerVariadic("ke_btb", btbT2);
        for (const char* name : {"btb_akar", "btb_faktorial", "btb_gcd", "btb_lcm",
                                 "btb_pangkat_mod"}) {
            registerVariadic(name, btbT2);
        }
        registerVariadic("btb_prima", logika);
        registerVariadic("btb_bit_length", bilangan);
        registerVariadic("btb_ke_larik_btb", context.types().getArrayType(btbT2, std::nullopt));
        Symbol* larikBtb = context.createSymbol(SymbolKind::BuiltinType, "larik_btb", btbT2,
                                                nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(larikBtb);
        registerVariadic("larik_btb_ke_btb", error);
        registerVariadic("btb_dari_larik_btb", error);
    }
    if (auto* matriksT = lookupType("matriks")) {
        Symbol* larikMatriks = context.createSymbol(SymbolKind::BuiltinType, "larik_matriks",
                                                    matriksT, nullptr, SourceRange{},
                                                    &context.globalScope());
        context.globalScope().insert(larikMatriks);
        registerVariadic("larik_matriks_ke_matriks", error);
    }

    // Async & misc API aliases.
    registerVariadic("balapan", error);
    registerVariadic("proses", error);
    registerVariadic("mulai_tugas", error);
    registerVariadic("batalkan", hampa);
    registerVariadic("ambil_data", teks);
    registerVariadic("ambil_data_aman", error);
    registerVariadic("pekerja_lepas", hampa);
    registerVariadic("berhentikan_pekerja", hampa);
    registerVariadic("antrian_bersihkan", hampa);
    registerVariadic("antrian_kosong", logika);
    registerVariadic("iterator_akhir", error);

    if (auto* btbT = lookupType("bilangan_tanpa_batas")) {
        registerVariadic("btb_akar", btbT);
        registerVariadic("btb_faktorial", btbT);
        registerVariadic("btb_gcd", btbT);
        registerVariadic("btb_lcm", btbT);
        registerVariadic("btb_pangkat_mod", btbT);
        registerVariadic("btb_prima", logika);
        registerVariadic("btb_bit_length", bilangan);
        registerVariadic("btb_ke_larik_btb", context.types().getArrayType(btbT, std::nullopt));
        // larik_btb[5] == larik[5] dari bilangan_tanpa_batas.
        Symbol* larikBtb = context.createSymbol(SymbolKind::BuiltinType, "larik_btb", btbT,
                                                nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(larikBtb);
    }

    // Legacy free-function collection API (modern form: methods on Daftar/Antrian/Tumpukan).
    registerVariadic("daftar_tambah", hampa);
    registerVariadic("antrian_tambah", hampa);
    registerVariadic("tumpukan_tambah", hampa);
    registerVariadic("iterator_selesai", logika);
    registerVariadic("pekerja_gabung", hampa);

    // Legacy signal API aliases (modern form: sinyal_* functions / Sinyal methods).
    registerVariadic("tangkap_sinyal", hampa);
    registerVariadic("abaikan_sinyal", hampa);
    registerVariadic("kembalikan_sinyal", hampa);
    registerVariadic("blokir_sinyal", hampa);
    registerVariadic("buka_blokir_sinyal", hampa);
    registerVariadic("kirim_sinyal", hampa);
    registerVariadic("pid_sendiri", bilangan);
    registerVariadic("tunggu_sinyal", bilangan);
    registerVariadic("reset_sinyal", hampa);

    // Signal number constants (typed as Sinyal; handlers take `sinyal: Sinyal`).
    Type* sinyalType = lookupType("Sinyal");
    if (!sinyalType) sinyalType = bilangan;
    for (const char* name : {"SIGINT", "SIGTERM", "SIGKILL", "SIGUSR1", "SIGUSR2",
                             "SIGALRM", "SIGHUP", "SIGQUIT", "SIGCHLD", "SIGSEGV", "SIGPIPE", "SIGCONT"}) {
        Symbol* symbol = context.createSymbol(SymbolKind::GlobalVar, name, sinyalType, nullptr,
                                              SourceRange{}, &context.globalScope());
        context.globalScope().insert(symbol);
    }

    // jenis_daftar: values used as generic type arguments (jenis = tunggal/ganda).
    auto* jenisDaftar = context.types().createSumType("jenis_daftar", nullptr, true, {});
    jenisDaftar->cases.push_back(SumType::Case{"tunggal", {}, nullptr, nullptr});
    jenisDaftar->cases.push_back(SumType::Case{"ganda", {}, nullptr, nullptr});
    Symbol* jenisSymbol = context.createSymbol(SymbolKind::Enum, "jenis_daftar", jenisDaftar,
                                               nullptr, SourceRange{}, &context.globalScope());
    context.globalScope().insert(jenisSymbol);
    for (const char* name : {"tunggal", "ganda"}) {
        Symbol* constant = context.createSymbol(SymbolKind::EnumConstant, name, jenisDaftar,
                                                nullptr, SourceRange{}, &context.globalScope());
        context.globalScope().insert(constant);
    }

    for (const char* name : {
        "buat_pekerja", "buat_mutex", "buat_atomik", "buat_saluran", "buat_kondisi",
        "buat_semaphore", "buat_barrier", "buat_sinyal", "buat_pembangkit", "buat_iterator",
        "buat_daftar", "buat_antrian", "buat_tumpukan", "buat_tugas", "buat_opsi",
        "buat_hasil", "mutex", "semaphore", "barrier", "kondisi", "sinyal", "pekerja",
        "saluran", "atomik", "desimal_tetap", "bilangan_kompleks", "bilangan_tanpa_batas",
        "matriks", "btb", "dt", "kx", "daftar", "antrian", "tumpukan", "hasil", "opsi",
        "iterator_mulai", "iterator_masih_ada", "iterator_berikutnya", "iterator_reset",
        "http_get", "mutex_kunci", "mutex_buka", "saluran_kirim", "saluran_terima",
        "saluran_tutup", "atomik_set", "atomik_tambah", "atomik_kurang",
        "atomik_tukar", "kondisi_tunggu", "kondisi_notifikasi", "semaphore_tunggu",
        "semaphore_lepas", "barrier_tunggu", "sinyal_tangkap", "sinyal_abaikan",
        "pembangkit_mulai", "pembangkit_berikutnya", "pembangkit_selesai"
    }) {
        registerVariadic(name, error);
    }
}

} // namespace rhc
