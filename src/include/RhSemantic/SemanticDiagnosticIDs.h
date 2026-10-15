#pragma once

namespace rhc {

enum class SemanticDiagnosticID {
    UnknownIdentifier,
    UnknownType,
    Redefinition,
    UnsupportedFeature,
    TypeMismatch,
    WrongArgumentCount,
    ReturnTypeMismatch,
    ExpectedConditionLogika,
    ExpectedLValue,
};

} // namespace rhc
