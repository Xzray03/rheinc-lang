#pragma once

#include "RhCore/Utf8.h"

namespace rhc {

// Klasifikasi karakter lexer (stateless). Dulu member Lexer, diangkat jadi
// free function inline agar tetap dapat di-inline lintas TU di jalur
// per-karakter panas (scanner identifier/numerik/string/char).
inline bool isAsciiIdentifierStart(char32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

inline bool isIdentifierStart(char32_t cp) {
    if (isAsciiIdentifierStart(cp)) return true;
    // U3: broad Unicode identifiers. Semantic narrowing can be added later.
    return cp >= 0x80u && cp != utf8::Replacement && cp != utf8::SentinelEOF;
}

inline bool isIdentifierContinue(char32_t cp) {
    return isIdentifierStart(cp) || (cp >= '0' && cp <= '9');
}

inline bool isHorizontalWhitespace(char32_t cp) {
    return cp == ' ' || cp == '\t';
}

inline bool isValidEscape(char32_t cp) {
    switch (cp) {
    case 'n':
    case 'r':
    case 't':
    case '\\':
    case '"':
    case '\'':
    case '0':
        return true;
    default:
        return false;
    }
}

} // namespace rhc
