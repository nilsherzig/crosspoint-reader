#include "CsvReader.h"

namespace flashcards {

int CsvReader::nextByte() {
  if (pendingByte >= 0) {
    const int value = pendingByte;
    pendingByte = -1;
    return value;
  }
  return readByte(context);
}

bool CsvReader::append(std::string& field, const char value, size_t& rowBytes, std::string& error) const {
  if (field.size() >= MAX_FIELD_BYTES) {
    error = "CSV field exceeds 16384 bytes";
    return false;
  }
  if (rowBytes >= MAX_ROW_BYTES) {
    error = "CSV row exceeds 32768 bytes";
    return false;
  }
  field.push_back(value);
  ++rowBytes;
  return true;
}

CsvReader::Result CsvReader::readRow(std::vector<std::string>& fields, std::string& error) {
  fields.clear();
  error.clear();
  if (fields.capacity() < 4) fields.reserve(4);

  std::string field;
  field.reserve(128);
  bool inQuotes = false;
  bool fieldStarted = false;
  bool sawInput = false;
  size_t rowBytes = 0;

  const auto finishField = [&]() -> bool {
    if (fields.size() >= MAX_COLUMNS) {
      error = "CSV row exceeds 64 columns";
      return false;
    }
    if (firstField) {
      firstField = false;
      if (field.size() >= 3 && static_cast<unsigned char>(field[0]) == 0xEF &&
          static_cast<unsigned char>(field[1]) == 0xBB && static_cast<unsigned char>(field[2]) == 0xBF) {
        field.erase(0, 3);
      }
    }
    fields.push_back(std::move(field));
    field.clear();
    field.reserve(128);
    fieldStarted = false;
    return true;
  };

  while (true) {
    const int value = nextByte();
    if (value < 0) {
      if (inQuotes) {
        error = "Unterminated quoted CSV field";
        return Result::Error;
      }
      if (!sawInput && fields.empty() && field.empty()) return Result::End;
      return finishField() ? Result::Row : Result::Error;
    }

    sawInput = true;
    const char c = static_cast<char>(value);
    if (inQuotes) {
      if (c != '"') {
        if (!append(field, c, rowBytes, error)) return Result::Error;
        continue;
      }

      const int afterQuote = nextByte();
      if (afterQuote == '"') {
        if (!append(field, '"', rowBytes, error)) return Result::Error;
        continue;
      }

      inQuotes = false;
      if (afterQuote < 0) return finishField() ? Result::Row : Result::Error;
      if (afterQuote == ',') {
        if (!finishField()) return Result::Error;
        continue;
      }
      if (afterQuote == '\n') return finishField() ? Result::Row : Result::Error;
      if (afterQuote == '\r') {
        const int afterCr = nextByte();
        if (afterCr >= 0 && afterCr != '\n') putBack(afterCr);
        return finishField() ? Result::Row : Result::Error;
      }
      error = "Unexpected character after quoted CSV field";
      return Result::Error;
    }

    if (c == ',' ) {
      if (!finishField()) return Result::Error;
      continue;
    }
    if (c == '\n') return finishField() ? Result::Row : Result::Error;
    if (c == '\r') {
      const int afterCr = nextByte();
      if (afterCr >= 0 && afterCr != '\n') putBack(afterCr);
      return finishField() ? Result::Row : Result::Error;
    }
    if (c == '"') {
      if (fieldStarted) {
        const bool bomBeforeQuotedFirstField =
            firstField && field.size() == 3 && static_cast<unsigned char>(field[0]) == 0xEF &&
            static_cast<unsigned char>(field[1]) == 0xBB && static_cast<unsigned char>(field[2]) == 0xBF;
        if (!bomBeforeQuotedFirstField) {
          error = "Quote inside unquoted CSV field";
          return Result::Error;
        }
        field.clear();
      }
      inQuotes = true;
      fieldStarted = true;
      continue;
    }

    fieldStarted = true;
    if (!append(field, c, rowBytes, error)) return Result::Error;
  }
}

}  // namespace flashcards
