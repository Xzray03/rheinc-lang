// RhBackend.h - Entry point backend: FinalIr -> assembly -> object.
//
// v1: target tunggal x86_64-linux-sysv (cek ringan; IrProviderManager
// sudah fail-fast di upstream). Link executable adalah tugas RhLinker
// (nanti) — RhBackend berhenti di object file hasil 'as'.

#pragma once

#include "RhBackend/RhBackendError.h"

#include "RhIR/FinalIr.h"

#include <string>

namespace rhc {

struct RhBackendOptions {
    std::string outputPath;      // path .s yang ditulis
    bool runAssembler = true;    // invoke 'as' -> .o (path sama, ekstensi .o)
    std::string assembler = "as";
    // true untuk object utama executable: emit __rheinc_stub + __global_init.
    // false untuk object dependency agar simbol CRT tidak duplikat saat link.
    bool emitCrtGlue = true;
};

class RhBackend {
public:
    // Emit modul ke assembly tekstual, lalu (default) assemble ke
    // object ELF via 'as'. errors terisi rinci bila report.ok == false.
    RhBackendReport emitAssembly(const FinalIr& finalIr,
                                 const RhBackendOptions& options) const;
};

}  // namespace rhc
