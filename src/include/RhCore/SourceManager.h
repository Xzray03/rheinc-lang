#pragma once

#include "RhCore/SourceLocation.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rhc {

enum class SLocKind {
    File,
    Expansion,
};

struct ExpansionPiece {
    std::string text;
    SourceLocation spellingLoc = 0;
};

class SourceManager {
public:
    SourceLocation makeLocation(FileID file, FileOffset offset) const;
    FileID getFileID(SourceLocation loc) const;
    FileOffset getFileOffset(SourceLocation loc) const;

    FileID addFile(std::string path, std::string content);
    FileID addFileFromDisk(const std::string& path, std::string& errorOut);

    FileID createExpansion(SourceLocation expansionLoc,
                           std::vector<ExpansionPiece> pieces);

    std::string_view getBuffer(FileID file) const;
    const std::string& getPath(FileID file) const;

    std::string_view getText(FileID file, SourceLocation loc, std::uint32_t length) const;

    bool getLineAndColumn(FileID file, SourceLocation loc,
                          unsigned& line, unsigned& column) const;

    bool getSpellingLocation(SourceLocation loc, FileID& file, FileOffset& offset) const;
    FileID getSpellingFileID(SourceLocation loc) const;

private:
    struct Entry {
        SLocKind kind = SLocKind::File;
        std::string path;
        std::string buffer;
        std::vector<FileOffset> lineStarts;

        SourceLocation expansionLoc = 0;
        std::vector<ExpansionPiece> pieces;
    };

    void computeLineStarts(Entry& entry) const;
    const Entry* getEntry(FileID file) const;

    std::vector<Entry> entries_;
};

} // namespace rhc
