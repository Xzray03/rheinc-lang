#include "RhCore/SourceManager.h"

#include "RhCore/Utf8.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace rhc {

SourceLocation SourceManager::makeLocation(FileID file, FileOffset offset) const {
    return makeSourceLocation(file, offset);
}

FileID SourceManager::getFileID(SourceLocation loc) const {
    return getFileIDFromLocation(loc);
}

FileOffset SourceManager::getFileOffset(SourceLocation loc) const {
    return getFileOffsetFromLocation(loc);
}

FileID SourceManager::addFile(std::string path, std::string content) {
    entries_.push_back(Entry{});
    Entry& entry = entries_.back();
    entry.kind = SLocKind::File;
    entry.path = std::move(path);
    entry.buffer = std::move(content);
    entry.lineStarts.push_back(0);
    computeLineStarts(entry);
    return FileID(static_cast<unsigned>(entries_.size() - 1));
}

FileID SourceManager::addFileFromDisk(const std::string& path, std::string& errorOut) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        errorOut = "cannot open file: " + path;
        return FileID();
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.good() && !input.eof()) {
        errorOut = "cannot read file: " + path;
        return FileID();
    }
    return addFile(path, contents.str());
}

FileID SourceManager::createExpansion(SourceLocation expansionLoc,
                                      std::vector<ExpansionPiece> pieces) {
    entries_.push_back(Entry{});
    Entry& entry = entries_.back();
    entry.kind = SLocKind::Expansion;
    entry.path = "<macro expansion>";
    entry.expansionLoc = expansionLoc;
    entry.pieces = std::move(pieces);
    return FileID(static_cast<unsigned>(entries_.size() - 1));
}

std::string_view SourceManager::getBuffer(FileID file) const {
    const Entry* entry = getEntry(file);
    if (!entry || entry->kind != SLocKind::File) return {};
    return entry->buffer;
}

const std::string& SourceManager::getPath(FileID file) const {
    static const std::string empty;
    const Entry* entry = getEntry(file);
    if (!entry) return empty;
    return entry->path;
}

std::string_view SourceManager::getText(FileID file, SourceLocation loc, std::uint32_t length) const {
    FileID locFile = file.valid() ? file : getFileID(loc);
    const Entry* entry = getEntry(locFile);
    if (!entry) return {};

    const FileOffset offset = getFileOffset(loc);
    if (entry->kind == SLocKind::Expansion) {
        if (offset >= entry->pieces.size()) return {};
        // Expansion offsets address pieces. Token length currently mirrors the
        // piece text length, but returning the whole piece is the useful behavior.
        (void)length;
        return entry->pieces[offset].text;
    }

    std::string_view buffer(entry->buffer);
    if (offset > buffer.size()) return {};
    const std::size_t available = buffer.size() - offset;
    const std::size_t clipped = std::min<std::size_t>(available, length);
    return buffer.substr(offset, clipped);
}

bool SourceManager::getLineAndColumn(FileID file, SourceLocation loc,
                                     unsigned& line, unsigned& column) const {
    (void)file; // The location itself determines the spelling file.
    line = 1;
    column = 1;

    FileID spellingFile;
    FileOffset spellingOffset = 0;
    if (!getSpellingLocation(loc, spellingFile, spellingOffset)) {
        return false;
    }
    const Entry* entry = getEntry(spellingFile);
    if (!entry || entry->kind != SLocKind::File) return false;
    std::string_view buffer(entry->buffer);
    if (spellingOffset > buffer.size()) return false;

    const auto& starts = entry->lineStarts;
    auto it = std::upper_bound(starts.begin(), starts.end(), spellingOffset);
    if (it == starts.begin()) return false;
    const std::size_t lineIndex = static_cast<std::size_t>(std::distance(starts.begin(), it)) - 1;
    line = static_cast<unsigned>(lineIndex + 1);

    const std::size_t lineStart = starts[lineIndex];
    std::size_t pos = lineStart;
    unsigned col = 1;
    while (pos < spellingOffset) {
        char32_t cp = 0;
        std::size_t len = 0;
        utf8::decode(buffer.data(), buffer.size(), pos, cp, len);
        if (len == 0) break;
        ++col;
        pos += len;
    }
    column = col;
    return true;
}

bool SourceManager::getSpellingLocation(SourceLocation loc, FileID& file, FileOffset& offset) const {
    FileID currentFile = getFileID(loc);
    FileOffset currentOffset = getFileOffset(loc);
    if (!currentFile.valid() || currentFile.value() >= entries_.size()) {
        return false;
    }

    // Resolve expansion pieces to their spelling locations. This loop is bounded
    // by the number of nested macro expansion entries.
    for (std::size_t depth = 0; depth <= entries_.size(); ++depth) {
        const Entry& entry = entries_[currentFile.value()];
        if (entry.kind == SLocKind::File) {
            file = currentFile;
            offset = currentOffset;
            return true;
        }
        if (currentOffset >= entry.pieces.size()) {
            currentFile = getFileID(entry.expansionLoc);
            currentOffset = getFileOffset(entry.expansionLoc);
            continue;
        }
        const SourceLocation spelling = entry.pieces[currentOffset].spellingLoc;
        currentFile = getFileID(spelling);
        currentOffset = getFileOffset(spelling);
    }
    return false;
}

FileID SourceManager::getSpellingFileID(SourceLocation loc) const {
    FileID file;
    FileOffset offset = 0;
    if (!getSpellingLocation(loc, file, offset)) return FileID();
    return file;
}

void SourceManager::computeLineStarts(Entry& entry) const {
    const std::string_view buffer(entry.buffer);
    std::size_t pos = 0;
    while (pos < buffer.size()) {
        char32_t cp = 0;
        std::size_t len = 0;
        const bool valid = utf8::decode(buffer.data(), buffer.size(), pos, cp, len);
        if (len == 0) break;

        if (valid && utf8::isNewline(cp)) {
            std::size_t next = pos + len;
            if (cp == 0x000Du && next < buffer.size() && buffer[next] == '\n') {
                next += 1;
            }
            entry.lineStarts.push_back(static_cast<FileOffset>(next));
            pos = next;
            continue;
        }

        pos += len;
    }
}

const SourceManager::Entry* SourceManager::getEntry(FileID file) const {
    if (!file.valid() || file.value() >= entries_.size()) return nullptr;
    return &entries_[file.value()];
}

} // namespace rhc
