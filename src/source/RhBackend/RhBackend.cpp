// RhBackend.cpp - Orkestrasi emit + invoke assembler.

#include "RhBackend/RhBackend.h"

#include "RhBackend/AsmBuilder.h"
#include "RhBackend/X86_64SysVEmitter.h"

#include "RhIR/IrTargetId.h"

#include <cstdlib>
#include <fstream>

namespace rhc {

RhBackendReport RhBackend::emitAssembly(const FinalIr& finalIr,
                                        const RhBackendOptions& options) const {
    RhBackendReport report;

    // Cek ringan: satu-satunya target yang didukung v1. (Upstream
    // IrProviderManager sudah fail-fast untuk target tak dikenal.)
    if (finalIr.targetId() != IrTargetId::systemVAmd64()) {
        RhBackendError err;
        err.code = RhBackendErrorCode::UnsupportedTarget;
        err.message = "target modul bukan x86_64-linux-sysv";
        report.errors.push_back(std::move(err));
        return report;
    }

    AsmBuilder builder;
    X86_64SysVEmitter emitter;
    if (!emitter.emitModule(finalIr, builder, report.errors)) {
        return report;
    }

    std::ofstream out(options.outputPath);
    if (!out) {
        RhBackendError err;
        err.code = RhBackendErrorCode::EmitFailure;
        err.message = "gagal membuka output: " + options.outputPath;
        report.errors.push_back(std::move(err));
        return report;
    }
    out << builder.str();
    out.close();

    if (options.runAssembler) {
        // <path>.s -> <path>.o (ganti ekstensi di belakang)
        std::string objPath = options.outputPath;
        const std::string::size_type dot = objPath.rfind('.');
        if (dot != std::string::npos) {
            objPath.resize(dot);
        }
        objPath += ".o";
        const std::string cmd = options.assembler + " " + options.outputPath +
                                " -o " + objPath;
        const int rc = std::system(cmd.c_str());
        if (rc != 0) {
            RhBackendError err;
            err.code = RhBackendErrorCode::AssemblerFailed;
            err.message = "assembler gagal (rc=" + std::to_string(rc) + "): " + cmd;
            report.errors.push_back(std::move(err));
            return report;
        }
    }

    report.ok = true;
    return report;
}

}  // namespace rhc
