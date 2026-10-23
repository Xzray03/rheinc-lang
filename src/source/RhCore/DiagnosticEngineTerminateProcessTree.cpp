#include "RhCore/DiagnosticEngine.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace rhc {

// Scan /proc, kumpulkan seluruh keturunan (descendants) dari proses ini,
// SIGKILL satu per satu, lalu SIGKILL diri sendiri. SIGKILL tidak dapat
// ditangkap/diblokir sehingga tidak ada jalan kembali ke caller.
[[noreturn]] void DiagnosticEngine::terminateProcessTree() {
    const pid_t self = getpid();

    // Peta pid -> ppid dari /proc/<pid>/stat. Field setelah ')':
    // state ppid ... (comm boleh mengandung spasi/parenthes).
    std::map<pid_t, pid_t> parentOf;
    if (DIR* proc = opendir("/proc")) {
        while (dirent* entry = readdir(proc)) {
            const std::string name = entry->d_name;
            if (name.empty() ||
                !std::all_of(name.begin(), name.end(),
                             [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                continue;
            }
            std::ifstream stat("/proc/" + name + "/stat");
            std::string content((std::istreambuf_iterator<char>(stat)),
                                std::istreambuf_iterator<char>());
            const std::size_t rparen = content.rfind(')');
            if (rparen == std::string::npos || rparen + 1 >= content.size()) continue;
            std::istringstream rest(content.substr(rparen + 1));
            char state = 0;
            long ppid = 0;
            rest >> state >> ppid;
            if (rest && ppid > 0) {
                parentOf[static_cast<pid_t>(std::strtol(name.c_str(), nullptr, 10))] =
                    static_cast<pid_t>(ppid);
            }
        }
        closedir(proc);
    }

    // Kumpulkan keturunan self secara BFS, bunuh paling dalam lebih dulu
    // agar tidak ada yang lolos saat parent-nya dimatikan lebih awal.
    std::vector<pid_t> descendants;
    std::vector<pid_t> frontier{self};
    while (!frontier.empty()) {
        std::vector<pid_t> next;
        for (const auto& [pid, ppid] : parentOf) {
            if (std::find(frontier.begin(), frontier.end(), ppid) != frontier.end() &&
                std::find(descendants.begin(), descendants.end(), pid) == descendants.end()) {
                descendants.push_back(pid);
                next.push_back(pid);
            }
        }
        frontier = std::move(next);
    }
    for (auto it = descendants.rbegin(); it != descendants.rend(); ++it) {
        kill(*it, SIGKILL);
    }

    kill(self, SIGKILL);
    _exit(137);  // fallback teoretis; SIGKILL di atas tidak pernah kembali
}

} // namespace rhc
