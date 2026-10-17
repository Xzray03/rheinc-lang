#pragma once

#include "RhIR/IrIds.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace rhc {

class IrModule;
struct IrInstr;

// ============ Optimizer Manager & pass registry (RhIR) ============
//
// Pipeline optimasi IR: manager memegang registry pass BERPESANAN
// (insertion order = urutan eksekusi). Setiap pass beroperasi lewat
// IrEditor (index-preserving), sehingga invariant modul terjaga tanpa
// renumber. Opsi verifyEachPass menjalankan Structural verify setelah
// tiap pass — bug pass terisolasi sebelum pass berikutnya menyentuh
// modul. Fase berikutnya: pass-level dependency & invalidasi epoch.

struct IrPassResult {
    bool ok = true;      // pass menolak melanjutkan pipeline bila false
    bool changed = false;
    // Statistik bebas bentuk untuk pass (jumlah fold, dsb).
    std::uint32_t folded = 0;
};

class IrPass {
public:
    virtual ~IrPass() = default;
    IrPass(const IrPass&) = delete;
    IrPass& operator=(const IrPass&) = delete;

    virtual std::string_view name() const = 0;
    virtual IrPassResult run(IrModule& module) = 0;

protected:
    IrPass() = default;
};

struct OptimizerReport {
    bool ok = false;
    // true bila SELURUH pass sukses; pass yang gagal menghentikan pipeline.
    std::vector<std::pair<std::string, IrPassResult>> results;
    std::string errorMessage;
};

struct OptimizerOptions {
    // Jalankan Structural verify setelah tiap pass (debug pass pipeline).
    bool verifyEachPass = false;
    // Ulangi rantai pass sampai satu putaran penuh tidak mengubah
    // apa pun (konvergen), dengan batas pengaman maxIterations.
    // Rantai pass saat ini konvergen dalam satu putaran; opsi ini
    // menjamin terminasi & deteksi non-konvergensi untuk pass masa
    // depan yang saling mengaktifkan (mis. pass yang membuka peluang
    // fold/DCE baru lintas block).
    bool iterateToFixpoint = false;
    std::uint32_t maxIterations = 8;
};

class OptimizerManager {
public:
    OptimizerManager();
    explicit OptimizerManager(OptimizerOptions options);
    ~OptimizerManager();
    OptimizerManager(const OptimizerManager&) = delete;
    OptimizerManager& operator=(const OptimizerManager&) = delete;

    // Registry pass. Pass dimiliki manager (unique_ptr).
    // Duplikat nama ditolak -> false.
    bool registerPass(std::unique_ptr<IrPass> pass);
    bool hasPass(std::string_view name) const;
    std::vector<std::string> passNames() const;

    // Jalankan satu pass terdaftar; modul tersentuh HANYA bila pass ada.
    // verifyEachPass (opsi manager) dijalankan setelah pass.
    OptimizerReport runPass(std::string_view name, IrModule& module);

    // Jalankan seluruh pass berurutan; pass yang gagal (report.ok=false
    // atau verify gagal) menghentikan pipeline (fail-fast).
    OptimizerReport runAll(IrModule& module);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhc
