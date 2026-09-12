#include <CsvReader.h>
#include <FsrsScheduler.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

struct StringInput {
  std::string value;
  size_t offset = 0;
};

int readByte(void* context) {
  auto& input = *static_cast<StringInput*>(context);
  if (input.offset >= input.value.size()) return -1;
  return static_cast<unsigned char>(input.value[input.offset++]);
}

TEST(CsvReader, ParsesRfc4180QuotingAndBom) {
  StringInput input{"\xEF\xBB\xBF"
                    "front,back,ignored\r\n\"hello, world\",\"line 1\r\nline \"\"2\"\"\",x\r\n"};
  flashcards::CsvReader reader(readByte, &input);
  std::vector<std::string> fields;
  std::string error;

  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  ASSERT_EQ(fields, (std::vector<std::string>{"front", "back", "ignored"}));
  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  ASSERT_EQ(fields, (std::vector<std::string>{"hello, world", "line 1\r\nline \"2\"", "x"}));
  EXPECT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::End);
}

TEST(CsvReader, AcceptsBomBeforeQuotedHeader) {
  StringInput input{"\xEF\xBB\xBF"
                    "\"front\",\"back\"\n"};
  flashcards::CsvReader reader(readByte, &input);
  std::vector<std::string> fields;
  std::string error;

  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  EXPECT_EQ(fields, (std::vector<std::string>{"front", "back"}));
}

TEST(CsvReader, RejectsMalformedQuote) {
  StringInput input{"front,back\nokay,bad\"quote\n"};
  flashcards::CsvReader reader(readByte, &input);
  std::vector<std::string> fields;
  std::string error;

  ASSERT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Row);
  EXPECT_EQ(reader.readRow(fields, error), flashcards::CsvReader::Result::Error);
  EXPECT_EQ(error, "Quote inside unquoted CSV field");
}

TEST(FsrsScheduler, UsesFsrs6DefaultParametersForNewCards) {
  flashcards::SchedulingResult again;
  flashcards::SchedulingResult good;
  ASSERT_TRUE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Again, 0.9f, 36500, again));
  ASSERT_TRUE(flashcards::FsrsScheduler::next(nullptr, 0, flashcards::Rating::Good, 0.9f, 36500, good));

  EXPECT_NEAR(again.memory.stability, 0.2172f, 0.0001f);
  EXPECT_NEAR(again.memory.difficulty, 7.0114f, 0.0001f);
  EXPECT_EQ(again.intervalDays, 1U);
  EXPECT_NEAR(good.memory.stability, 3.2602f, 0.0001f);
  EXPECT_NEAR(good.memory.difficulty, 4.88463f, 0.0001f);
  EXPECT_EQ(good.intervalDays, 3U);
}

TEST(FsrsScheduler, UpdatesExistingMemoryForGoodAndAgain) {
  const flashcards::MemoryState current{3.2602f, 4.8846316f};
  flashcards::SchedulingResult good;
  flashcards::SchedulingResult again;
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&current, 10, flashcards::Rating::Good, 0.9f, 36500, good));
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&current, 10, flashcards::Rating::Again, 0.9f, 36500, again));

  EXPECT_NEAR(good.memory.stability, 21.79030f, 0.001f);
  EXPECT_NEAR(good.memory.difficulty, 4.86806f, 0.001f);
  EXPECT_EQ(good.intervalDays, 22U);
  EXPECT_NEAR(again.memory.stability, 1.41380f, 0.001f);
  EXPECT_NEAR(again.memory.difficulty, 7.23492f, 0.001f);
}

TEST(FsrsScheduler, AppliesShortTermStepAtZeroElapsedDays) {
  const flashcards::MemoryState current{0.2172f, 7.0114f};
  flashcards::SchedulingResult result;
  ASSERT_TRUE(flashcards::FsrsScheduler::next(&current, 0, flashcards::Rating::Good, 0.9f, 36500, result));
  EXPECT_GT(result.memory.stability, current.stability);
  EXPECT_GE(result.intervalDays, 1U);
}

}  // namespace
