#include "FlashcardBackupUploader.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr char MODULE[] = "FLASHBK";
constexpr size_t MAX_FILES = 128;

struct SourceFile {
  std::string path;
  std::string destination;
};

bool endsWithIgnoreCase(const char* name, const char* suffix) {
  const size_t length = strlen(name);
  const size_t suffixLength = strlen(suffix);
  if (length <= suffixLength) return false;
  for (size_t i = 0; i < suffixLength; ++i) {
    if (std::tolower(static_cast<unsigned char>(name[length - suffixLength + i])) != suffix[i]) return false;
  }
  return true;
}

bool collectFiles(const char* directory, const char* suffix, const char* destination, std::vector<SourceFile>& files,
                  std::string& error) {
  if (!Storage.exists(directory)) return true;
  HalFile folder;
  if (!Storage.openFileForRead(MODULE, directory, folder)) {
    error = "Cannot read flashcard directory";
    return false;
  }
  for (HalFile entry = folder.openNextFile(); entry; entry = folder.openNextFile()) {
    if (entry.isDirectory()) continue;
    char name[192]{};
    if (entry.getName(name, sizeof(name)) == 0) {
      error = "Flashcard filename is too long";
      return false;
    }
    if (!endsWithIgnoreCase(name, suffix)) continue;
    if (files.size() == MAX_FILES) {
      error = "Too many flashcard backup files";
      return false;
    }
    files.push_back({std::string(directory) + "/" + name, std::string(destination) + "/" + name});
  }
  return true;
}

class BackupHttpClient {
  esp_http_client_handle_t client = nullptr;
  std::string origin;

 public:
  explicit BackupHttpClient(const flashcards::Config& config) : origin(config.backupServerUrl) {
    esp_http_client_config_t options = {};
    options.url = origin.c_str();
    options.crt_bundle_attach = esp_crt_bundle_attach;
    options.timeout_ms = 30000;
    options.keep_alive_enable = true;
    options.buffer_size = 1024;
    client = esp_http_client_init(&options);
    if (client) {
      esp_http_client_set_header(client, "User-Agent", "CrossPoint-FlashcardBackup");
      if (!config.backupPassword.empty()) esp_http_client_set_header(client, "PW", config.backupPassword.c_str());
    }
  }
  ~BackupHttpClient() {
    if (client) esp_http_client_cleanup(client);
  }
  bool ready() const { return client != nullptr; }

  bool request(const std::string& path, esp_http_client_method_t method, HalFile* file, int& status) {
    if (!client || esp_http_client_set_url(client, (origin + path).c_str()) != ESP_OK ||
        esp_http_client_set_method(client, method) != ESP_OK)
      return false;
    const uint64_t size = file ? file->fileSize64() : 0;
    if (size > INT32_MAX || esp_http_client_open(client, static_cast<int>(size)) != ESP_OK) return false;
    if (file) {
      uint8_t data[192];
      uint64_t remaining = size;
      while (remaining) {
        const size_t count = std::min<uint64_t>(remaining, sizeof(data));
        const int read = file->read(data, count);
        if (read != static_cast<int>(count)) {
          esp_http_client_close(client);
          return false;
        }
        int offset = 0;
        while (offset < read) {
          const int written =
              esp_http_client_write(client, reinterpret_cast<const char*>(data + offset), read - offset);
          if (written <= 0) {
            esp_http_client_close(client);
            return false;
          }
          offset += written;
        }
        remaining -= count;
      }
    }
    if (esp_http_client_fetch_headers(client) < 0) {
      esp_http_client_close(client);
      return false;
    }
    status = esp_http_client_get_status_code(client);
    int discarded = 0;
    if (esp_http_client_flush_response(client, &discarded) != ESP_OK) {
      esp_http_client_close(client);
      return false;
    }
    return true;
  }
};

bool ensureDirectory(BackupHttpClient& client, const std::string& directory, std::string& error) {
  size_t start = 1;
  while (start < directory.size()) {
    const size_t end = directory.find('/', start);
    const std::string prefix = directory.substr(0, end);
    int status = 0;
    if (!client.request(prefix + "/", HTTP_METHOD_MKCOL, nullptr, status) ||
        (status != 200 && status != 201 && status != 204 && status != 405)) {
      LOG_ERR(MODULE, "MKCOL failed: %d (%s)", status, prefix.c_str());
      error = "Could not create backup directory";
      return false;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}
}  // namespace

bool FlashcardBackupUploader::upload(const flashcards::Config& config, const int64_t timestamp,
                                     const ProgressCallback progress, void* ctx, std::string& error) {
  error.clear();
  if (timestamp <= 0 || config.backupServerUrl.rfind("https://", 0) != 0 || config.backupPassword.empty()) {
    error = "Backup server or password is not configured";
    return false;
  }
  std::vector<SourceFile> files;
  files.reserve(MAX_FILES);  // Bounded file list; keep the transfer stream buffer on the stack.
  if (!collectFiles(flashcards::FlashcardStore::SOURCE_DIRECTORY, ".csv", "decks", files, error) ||
      !collectFiles("/.crosspoint/flashcards", ".history", "history", files, error))
    return false;
  if (files.empty()) {
    error = "No flashcard files to back up";
    return false;
  }
  const time_t seconds = static_cast<time_t>(timestamp);
  tm utc{};
  if (!gmtime_r(&seconds, &utc)) {
    error = "Invalid backup timestamp";
    return false;
  }
  char stamp[24];
  if (strftime(stamp, sizeof(stamp), "%Y%m%dT%H%M%SZ", &utc) == 0) {
    error = "Invalid backup timestamp";
    return false;
  }
  std::string root = config.backupDirectory;
  while (root.size() > 1 && root.back() == '/') root.pop_back();
  if (root == "/") root.clear();
  root += "/";
  root += stamp;
  BackupHttpClient client(config);
  if (!client.ready() || !ensureDirectory(client, config.backupDirectory, error)) {
    if (error.empty()) error = "Could not connect to backup server";
    return false;
  }
  int timestampStatus = 0;
  if (!client.request(root + "/", HTTP_METHOD_MKCOL, nullptr, timestampStatus) || timestampStatus != 201) {
    LOG_ERR(MODULE, "Could not create fresh timestamp directory (%d)", timestampStatus);
    error = "Backup timestamp directory already exists or cannot be created";
    return false;
  }
  if (!ensureDirectory(client, root + "/decks", error) || !ensureDirectory(client, root + "/history", error))
    return false;
  if (progress) progress(ctx, 0, files.size());
  for (size_t i = 0; i < files.size(); ++i) {
    HalFile file;
    if (!Storage.openFileForRead(MODULE, files[i].path, file)) {
      error = "Could not read backup source";
      return false;
    }
    int status = 0;
    if (!client.request(root + "/" + files[i].destination, HTTP_METHOD_PUT, &file, status) || status < 200 ||
        status >= 300) {
      LOG_ERR(MODULE, "Upload failed: %s (%d)", files[i].path.c_str(), status);
      error = "Backup upload failed";
      return false;
    }
    if (progress) progress(ctx, i + 1, files.size());
  }
  return true;
}
