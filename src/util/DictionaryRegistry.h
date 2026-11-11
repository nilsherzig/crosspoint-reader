#pragma once

#include <cstddef>
#include <string>
#include <vector>

// One StarDict dictionary found under /dictionaries or /.dictionaries: a
// subfolder holding <stem>.idx plus <stem>.dict or <stem>.dict.dz.
struct DictionaryEntry {
  std::string name;  // subfolder name (shown to the user, stored in settings)
  std::string stem;  // index basename without .idx
};

namespace DictionaryRegistry {

// Scan /dictionaries/*/ and /.dictionaries/*/ for dictionaries. Folders with
// multiple index stems are ambiguous and skipped; a folder name found in both
// roots is listed once. Result is sorted
// case-insensitively by name.
void discover(std::vector<DictionaryEntry>& out);

// Number of usable dictionaries, counting no further than limit, so callers
// that only need "none / one / several" stop the SD scan early.
size_t count(size_t limit);

// Resolve a folder name to its extensionless base path
// ("/dictionaries/<folder>/<stem>" or "/.dictionaries/<folder>/<stem>").
// Returns false if the folder holds no usable dictionary in either root.
bool resolveBasePath(const char* folderName, std::string& basePathOut);

// Per-book dictionary choice: the folder name, stored in the book's cache
// directory. It follows the cache when the book is moved and is deleted with it.
// False when nothing is stored or the stored name does not fit in out.
bool loadBookChoice(const std::string& bookCachePath, char* out, size_t outSize);
bool saveBookChoice(const std::string& bookCachePath, const char* folderName);

}  // namespace DictionaryRegistry
