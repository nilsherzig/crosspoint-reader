add_executable(flashcards_tests
  FlashcardsTests.cpp
  ${REPO_ROOT}/lib/Flashcards/CsvReader.cpp
  ${REPO_ROOT}/lib/Flashcards/FsrsScheduler.cpp
  ${REPO_ROOT}/lib/Flashcards/StudyQueueBuilder.cpp
)

target_include_directories(flashcards_tests PRIVATE
  ${REPO_ROOT}/lib/Flashcards
)
target_link_libraries(flashcards_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(flashcards_tests)
