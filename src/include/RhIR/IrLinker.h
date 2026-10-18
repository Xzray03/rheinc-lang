#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace rhc {

class IrModule;

// ============ IrLinkVerifier: verifikasi simbol antar modul ============
//
// Impor Rheinc bersifat TEKSTUAL (#tambahkan di preprocessor), sehingga
// satu unit translasi sudah self-contained dan tidak butuh merging IR.
// Yang tetap perlu di-link-time adalah VERIFIKASI SIMBOL lintas modul
// yang dikompilasi bersamaan oleh driver:
//
//   - DuplicateDefinition: nama fungsi didefinisikan (Normal/Entry) di
//     lebih dari satu modul — akan bentrok bila modul digabung/dilink
//     oleh backend. GlobalInit (.__global_init per modul) dikecualikan:
//     global init per-modul digabung backend lewat urutan eksekusi.
//   - UnresolvedExternal: modul EXECUTABLE punya deklarasi External
//     yang tidak terdefinisi di modul manapun dalam sesi link. Modul
//     declaration (.rhh) dikecualikan dari aturan ini — memang tugasnya
//     menyediakan deklarasi (mis. untuk backend object/archive).
//
// Fase berikutnya bila bahasa menambah impor simbolis: merging modul
// dengan remap FuncId (perlu perluasan IrEditor melewati batas fungsi).

enum class IrLinkErrorCode : std::uint8_t {
    DuplicateDefinition,
    UnresolvedExternal,
};

struct IrLinkError {
    IrLinkErrorCode code;
    std::string moduleName;  // module->sourcePath modul pelapor
    std::string symbol;
    std::string message;
};

struct IrLinkReport {
    bool ok = false;
    std::vector<IrLinkError> errors;
};

class IrLinkVerifier {
public:
    // Setiap pointer modul harus valid dan outlive pemanggilan.
    IrLinkReport verify(const std::vector<const IrModule*>& modules) const;

    // Sama, tetapi simbol yang didefinisikan oleh object prebuilt (.o)
    // ikut dianggap sebagai definisi eksternal. Dipakai RhMain saat
    // --link mencampur file .rhc dan object .o.
    IrLinkReport verify(
        const std::vector<const IrModule*>& modules,
        const std::unordered_set<std::string>& externalDefinedSymbols) const;
};

} // namespace rhc
