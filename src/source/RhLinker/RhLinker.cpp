// RhLinker.cpp - Implementasi linker Rheinc.
//
// Alur:
//   1. Baca simbol UNDEFINED global dari object user (nm -u -g).
//   2. Bangun indeks manifest: simbol DEFINED global -> object library
//      (nm -g --defined-only per object .o di libraryDir).
//   3. Closure: mulai dari simbol rh_* undefined user; tiap object yang
//      mendefinisikan simbol yang dibutuhkan ikut terpilih; simbol undefined
//      object itu (nm -u) menambah kebutuhan. Ulangi sampai fixpoint.
//   4. rh_crt.o selalu termasuk (berisi _start).
//   5. Invoke ld: -o output user.o crt + object terpilih.

#include "RhLinker/RhLinker.h"

#include <cstdio>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace rhc {

namespace {

void fail(std::vector<RhLinkerError>& errors, RhLinkerErrorCode code,
          const std::string& msg) {
    RhLinkerError err;
    err.code = code;
    err.message = msg;
    errors.push_back(std::move(err));
}

// Jalankan perintah, kembalikan stdout-nya.
std::string runCapture(const std::string& cmd, bool* okOut) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), p)) out += buf;
        const int rc = pclose(p);
        if (okOut) *okOut = (rc == 0);
    } else if (okOut) {
        *okOut = false;
    }
    return out;
}

// Simbol dari output nm: token terakhir tiap baris non-kosong.
std::vector<std::string> parseNmSymbols(const std::string& nmOut) {
    std::vector<std::string> syms;
    std::string cur;
    for (char c : nmOut) {
        if (c == '\n') {
            // abaikan baris kosong & baris "no symbols"
            std::string line = cur;
            cur.clear();
            if (line.empty() || line.find("no symbols") != std::string::npos) {
                continue;
            }
            const std::string::size_type pos = line.find_last_not_of(" \t");
            if (pos == std::string::npos) continue;
            const std::string::size_type start = line.find_last_of(" \t", pos);
            const std::string sym =
                line.substr(start == std::string::npos ? 0 : start + 1,
                            pos - (start == std::string::npos ? 0 : start));
            if (!sym.empty() && sym.find(".") != 0) syms.push_back(sym);
        } else {
            cur += c;
        }
    }
    return syms;
}

bool fileExists(const std::string& path) {
    return std::fopen(path.c_str(), "rb") != nullptr;
}

}  // namespace

RhLinkerReport RhLinker::link(const RhLinkerOptions& options) const {
    RhLinkerReport report;

    if (!fileExists(options.inputObject)) {
        fail(report.errors, RhLinkerErrorCode::MissingInput,
             "object input tak ditemukan: " + options.inputObject);
        return report;
    }

    // Daftar object library (*.o di libraryDir).
    std::vector<std::string> libObjects;
    {
        const std::string lsCmd =
            "ls " + options.libraryDir + "/*.o 2>/dev/null";
        bool ok = false;
        const std::string lsOut = runCapture(lsCmd, &ok);
        if (!ok || lsOut.empty()) {
            fail(report.errors, RhLinkerErrorCode::LibraryNotFound,
                 "direktori library RhBuiltin kosong/invalid: " +
                     options.libraryDir);
            return report;
        }
        std::string cur;
        for (char c : lsOut) {
            if (c == '\n') {
                if (!cur.empty()) libObjects.push_back(cur);
                cur.clear();
            } else if (c != '\r') {
                cur += c;
            }
        }
        if (!cur.empty()) libObjects.push_back(cur);
    }

    // Indeks manifest: simbol defined -> object.
    std::unordered_map<std::string, std::string> definedIn;
    // Undefined per object library (untuk closure).
    std::unordered_map<std::string, std::vector<std::string>> undefOf;
    for (const std::string& obj : libObjects) {
        bool ok = false;
        const std::string defOut =
            runCapture("nm -g --defined-only " + obj + " 2>/dev/null", &ok);
        if (!ok) {
            fail(report.errors, RhLinkerErrorCode::SymbolScanFailed,
                 "nm gagal pada " + obj);
            return report;
        }
        for (const std::string& s : parseNmSymbols(defOut)) definedIn[s] = obj;
        const std::string undOut =
            runCapture("nm -u " + obj + " 2>/dev/null", nullptr);
        undefOf[obj] = parseNmSymbols(undOut);
    }

    // Kebutuhan awal: simbol undefined object user.
    std::set<std::string> needed;
    {
        const std::string undOut =
            runCapture("nm -u " + options.inputObject + " 2>/dev/null",
                       nullptr);
        for (const std::string& s : parseNmSymbols(undOut)) needed.insert(s);
    }

    // Closure object terpilih.
    std::set<std::string> selected;
    bool grew = true;
    while (grew) {
        grew = false;
        for (const std::string& sym : needed) {
            const auto it = definedIn.find(sym);
            if (it == definedIn.end()) continue;  // simbol non-runtime
            if (selected.count(it->second)) continue;
            selected.insert(it->second);
            report.selectedObjects.push_back(it->second);
            grew = true;
            for (const std::string& u : undefOf[it->second]) {
                if (!needed.count(u)) {
                    needed.insert(u);
                    grew = true;
                }
            }
        }
    }

    // rh_crt.o selalu dipakai (_start); pastikan terpilih walau tak
    // direferensikan simbol (user.o mereferensikan _start lewat ELF entry).
    for (const std::string& obj : libObjects) {
        if (obj.size() >= 8 &&
            obj.compare(obj.size() - 8, 8, "rh_crt.o") == 0 &&
            !selected.count(obj)) {
            selected.insert(obj);
            report.selectedObjects.push_back(obj);
        }
    }

    // Verifikasi simbol runtime yang tak terselesaikan.
    for (const std::string& sym : needed) {
        if (sym.rfind("rh_", 0) == 0 && !definedIn.count(sym)) {
            fail(report.errors, RhLinkerErrorCode::UnresolvedSymbol,
                 "simbol runtime tak ditemukan di library: " + sym);
        }
    }
    if (!report.errors.empty()) return report;

    // Invoke ld.
    std::string cmd = options.linker + " -o " + options.outputPath + " " +
                      options.inputObject;
    // Urutan: object terpilih (crt dulu agar _start di awal, bukan wajib).
    for (const std::string& obj : report.selectedObjects) cmd += " " + obj;
    bool ldOk = false;
    const std::string ldOut = runCapture(cmd + " 2>&1", &ldOk);
    if (!ldOk) {
        fail(report.errors, RhLinkerErrorCode::LdFailed,
             "ld gagal: " + ldOut);
        return report;
    }

    report.ok = true;
    return report;
}

}  // namespace rhc
