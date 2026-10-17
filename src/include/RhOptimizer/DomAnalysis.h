#pragma once

// Kompatibilitas: analisis dominator pindah ke RhIR/IrDomAnalysis.h
// (dipakai juga verifier). Alias namespace rhc::opt dipertahankan
// untuk pass optimizer yang sudah ada.

#include "RhIR/IrDomAnalysis.h"

namespace rhc {
namespace opt {
using rhc::DomInfo;
using rhc::computeDom;
} // namespace opt
} // namespace rhc
