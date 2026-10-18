// X86_64SysVEmitter.h - Emitter assembly x86-64 System V (satu-satunya
// implementasi AsmEmitter pada v1; target x86_64-linux-sysv).

#pragma once

#include "RhBackend/AsmEmitter.h"

namespace rhc {

class X86_64SysVEmitter final : public AsmEmitter {
public:
    bool emitModule(const FinalIr& finalIr, AsmBuilder& out,
                    std::vector<RhBackendError>& errors,
                    bool emitCrtGlue) override;
};

}  // namespace rhc
