#include "AstToIrTranslatorImpl.h"

namespace rhc {

// Encoding string literal ke constant pool (spesifikasi Bagian 6.2):
// bytes UTF-8 + length eksplisit. Dedup aktif dijamin oleh
// IrBuilder::internConstant — string yang identik berbagi ConstId.
ConstId AstToIrTranslator::Impl::encodeStringLiteral(
        const std::string& text, TypeId stringType, const AstNode* node) {
    (void)node;  // lokasi tercatat di instruksi ConstString pemakai
    IrConstant constant;
    constant.kind = IrConstantKind::String;
    constant.type = stringType;
    constant.bytes.assign(text.begin(), text.end());
    constant.length = text.size();
    return builder.internConstant(constant);
}

} // namespace rhc
