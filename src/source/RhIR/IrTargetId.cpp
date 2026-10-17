#include "RhIR/IrTargetId.h"

namespace rhc {

std::string IrTargetId::toString() const {
    if (*this == IrTargetId::systemVAmd64()) {
        return "x86_64-linux-sysv";
    }
    return "unknown-target";
}

}  // namespace rhc
