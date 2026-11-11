#include "DictionaryRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "StringUtils.h"

namespace DictionaryRegistry {
namespace {

// Dictionaries are looked up in both roots, in order. The hidden variant
// lets users keep the folder out of the file browser (hidden by default,
// see FileBrowserActivity's showHiddenFiles check).
constexpr const char* DICT_ROOTS[] = {"/dictionaries", "/.dictionaries"};

constexpr const char* BOOK_CHOICE_FILE = "/dictionary.txt";

// Find the single .idx stem inside one dictionary folder. Returns false when
// the folder holds no .idx or more than one distinct stem (ambiguous).
bool findStem(const char* folderPath, std::string& stemOut) {
  auto dir = Storage.open(folderPath);
  if (!dir || !dir.isDirectory()) return false;

  dir.rewindDirectory();
  char name[128];
  char foundStem[128];
  foundStem[0] = '\0';
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    // Skip macOS metadata files (AppleDouble resource forks)
    if (entry.isDirectory() || strncmp(name, "._", 2) == 0) continue;

    const size_t len = strlen(name);
    if (len <= 4 || strcmp(name + len - 4, ".idx") != 0) continue;

    name[len - 4] = '\0';
    if (foundStem[0] != '\0' && strcmp(foundStem, name) != 0) {
      LOG_DBG("DREG", "Skipping %s: multiple index stems found", folderPath);
      return false;
    }
    strncpy(foundStem, name, sizeof(foundStem) - 1);
    foundStem[sizeof(foundStem) - 1] = '\0';
  }

  if (foundStem[0] == '\0') return false;

  // Require dictionary data next to the index, so folders holding only an
  // .idx never surface as selectable dictionaries that fail at lookup time.
  const std::string base = std::string(folderPath) + "/" + foundStem;
  if (!Storage.exists((base + ".dict").c_str()) && !Storage.exists((base + ".dict.dz").c_str())) {
    LOG_DBG("DREG", "Skipping %s: no .dict or .dict.dz", folderPath);
    return false;
  }

  stemOut = foundStem;
  return true;
}

// Calls visit(name, stem) for each usable dictionary folder, in directory
// order, until visit returns false.
template <typename Visit>
void forEachDictionary(Visit&& visit) {
  for (const char* dictRoot : DICT_ROOTS) {
    auto rootDir = Storage.open(dictRoot);
    if (!rootDir || !rootDir.isDirectory()) {
      LOG_DBG("DREG", "No %s directory on SD card", dictRoot);
      continue;
    }

    rootDir.rewindDirectory();
    char name[128];
    for (auto entry = rootDir.openNextFile(); entry; entry = rootDir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (!entry.isDirectory() || name[0] == '.') continue;

      std::string folderPath = std::string(dictRoot) + "/" + name;
      std::string stem;
      if (!findStem(folderPath.c_str(), stem)) continue;
      if (!visit(name, std::move(stem))) return;
    }
  }
}

}  // namespace

void discover(std::vector<DictionaryEntry>& out) {
  out.clear();
  out.reserve(8);

  forEachDictionary([&out](const char* name, std::string&& stem) {
    DictionaryEntry e;
    e.name = name;
    e.stem = std::move(stem);
    out.push_back(std::move(e));
    LOG_DBG("DREG", "Found dictionary: %s", name);
    return true;
  });

  // Case-insensitive sort by folder name (matches FileBrowserActivity ordering).
  // Stable, so a name found in both roots keeps the first root's entry, the one
  // resolveBasePath() opens; the copy in the second root is dropped.
  std::stable_sort(out.begin(), out.end(), [](const DictionaryEntry& a, const DictionaryEntry& b) {
    return StringUtils::asciiCaseCmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  out.erase(std::unique(out.begin(), out.end(),
                        [](const DictionaryEntry& a, const DictionaryEntry& b) { return a.name == b.name; }),
            out.end());
}

size_t count(const size_t limit) {
  if (limit == 0) return 0;
  // A name found in both roots is one dictionary, as in discover().
  std::vector<std::string> seen;
  forEachDictionary([&seen, limit](const char* name, std::string&&) {
    if (std::find(seen.begin(), seen.end(), name) == seen.end()) seen.emplace_back(name);
    return seen.size() < limit;
  });
  return seen.size();
}

bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  if (!folderName || folderName[0] == '\0') return false;
  // folderName is read back from the SD card: reject separators and dot
  // prefixes so a crafted value cannot escape the dictionary roots.
  if (folderName[0] == '.' || strpbrk(folderName, "/\\") != nullptr) return false;

  for (const char* dictRoot : DICT_ROOTS) {
    std::string folderPath = std::string(dictRoot) + "/" + folderName;
    std::string stem;
    if (!findStem(folderPath.c_str(), stem)) continue;
    basePathOut = folderPath + "/" + stem;
    return true;
  }
  return false;
}

bool loadBookChoice(const std::string& bookCachePath, char* out, const size_t outSize) {
  if (outSize == 0) return false;
  out[0] = '\0';
  HalFile file;
  if (!Storage.openFileForRead("DREG", bookCachePath + BOOK_CHOICE_FILE, file)) return false;
  // A read that fills the whole buffer means the name is too long: reject it rather than truncate.
  const int len = file.read(out, outSize);
  if (len <= 0 || static_cast<size_t>(len) >= outSize) {
    out[0] = '\0';
    return false;
  }
  out[len] = '\0';
  return true;
}

bool saveBookChoice(const std::string& bookCachePath, const char* folderName) {
  HalFile file;
  if (!Storage.openFileForWrite("DREG", bookCachePath + BOOK_CHOICE_FILE, file)) return false;
  const size_t len = strlen(folderName);
  if (file.write(folderName, len) != len) {
    LOG_ERR("DREG", "Failed to save book dictionary choice");
    return false;
  }
  return true;
}

}  // namespace DictionaryRegistry
