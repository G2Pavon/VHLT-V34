#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Owning FILE handle: closed when it goes out of scope.
struct FileCloser
{
    void operator()(std::FILE *f) const { std::fclose(f); }
};
using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

// Opens a file or exits with "Error opening <name>: <reason>".
UniqueFile SafeOpen(const char *const filename, const char *const mode);

// The whole file. Exits on open or read errors.
std::vector<char> ReadFile(const char *const filename);

int q_filelength(std::FILE *f);
bool q_exists(const char *const filename);
// Deletes a file if it is there; errors (no such file, no permission) are ignored, as with the
// unlink() calls this replaces. A directory, even an empty one, is left alone.
void RemoveFile(const std::string &filename);

void SafeRead(std::FILE *f, void *buffer, int count);
void SafeWrite(std::FILE *f, const void *const buffer, int count);

// Raw-pointer versions for the tools that still manage FILE* themselves.
std::FILE *SafeOpenWrite(const char *const filename);
std::FILE *SafeOpenRead(const char *const filename);
