#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace flashcards {

class CsvReader {
 public:
  using ReadByte = int (*)(void* context);
  enum class Result { Row, End, Error };

  CsvReader(ReadByte readByte, void* context) : readByte(readByte), context(context) {}

  Result readRow(std::vector<std::string>& fields, std::string& error);

 private:
  static constexpr size_t MAX_COLUMNS = 64;
  static constexpr size_t MAX_FIELD_BYTES = 16384;
  static constexpr size_t MAX_ROW_BYTES = 32768;

  int nextByte();
  void putBack(int value) { pendingByte = value; }
  bool append(std::string& field, char value, size_t& rowBytes, std::string& error) const;

  ReadByte readByte;
  void* context;
  int pendingByte = -1;
  bool firstField = true;
};

}  // namespace flashcards
