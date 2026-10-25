#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

// Owning FILE handle: closed when it goes out of scope.
struct FileCloser
{
    void operator()(std::FILE *f) const { std::fclose(f); }
};
using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

// File names are std::filesystem::path at this boundary only: they are opened as they are written (the
// narrow name, byte for byte on POSIX). Parsing a name (directory, extension, either slash) stays string
// code, see the path helpers in cmdlib.h.

// Opens a file; null if it cannot be opened (errno says why).
UniqueFile OpenFile(const std::filesystem::path &filename, const char *const mode);

// Opens a file or exits with "Error opening <name>: <reason>".
UniqueFile SafeOpen(const std::filesystem::path &filename, const char *const mode);

// The whole file. Exits on open or read errors.
std::vector<char> ReadFile(const std::filesystem::path &filename);

int q_filelength(std::FILE *f);
// compat: whether the file can be opened for reading (not fs::exists: that is also true for a file
// that cannot be read, and the searches for a wad go on to the next candidate in that case).
bool q_exists(const std::filesystem::path &filename);
// Deletes a file if it is there; errors (no such file, no permission) are ignored, as with the
// unlink() calls this replaces. A directory, even an empty one, is left alone.
void RemoveFile(const std::filesystem::path &filename);

void SafeRead(std::FILE *f, void *buffer, int count);
void SafeWrite(std::FILE *f, const void *const buffer, int count);

// Raw-pointer versions for the tools that still manage FILE* themselves.
std::FILE *SafeOpenWrite(const std::filesystem::path &filename);
