add_executable(flashcards_tests
  CardSchedulerBehaviorTests.cpp
  FlashcardsTests.cpp
  FlashcardBackupCounterTests.cpp
  FlashcardTextLayoutTests.cpp
  ReviewCountTests.cpp
  ReviewForecastTests.cpp
  ReviewUndoTests.cpp
  StudyQueueBuilderBehaviorTests.cpp
  ${REPO_ROOT}/lib/Flashcards/CardScheduler.cpp
  ${REPO_ROOT}/lib/Flashcards/CsvReader.cpp
  ${REPO_ROOT}/lib/Flashcards/FlashcardConfigParser.cpp
  ${REPO_ROOT}/lib/Flashcards/FsrsScheduler.cpp
  ${REPO_ROOT}/lib/Flashcards/SchedulingFuzzer.cpp
  ${REPO_ROOT}/lib/Flashcards/StudyQueueBuilder.cpp
)

target_include_directories(flashcards_tests PRIVATE
  ${REPO_ROOT}/lib/Flashcards
)
target_link_libraries(flashcards_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(flashcards_tests)

add_executable(flashcard_snapshot_unit_tests SnapshotIntegrityTests.cpp)
target_include_directories(flashcard_snapshot_unit_tests PRIVATE ${REPO_ROOT}/lib/Flashcards)
target_link_libraries(flashcard_snapshot_unit_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(flashcard_snapshot_unit_tests)

add_executable(flashcard_store_integration_tests
  FlashcardStoreIntegrationTests.cpp
  ${REPO_ROOT}/lib/Flashcards/CardScheduler.cpp
  ${REPO_ROOT}/lib/Flashcards/CsvReader.cpp
  ${REPO_ROOT}/lib/Flashcards/FlashcardConfigParser.cpp
  ${REPO_ROOT}/lib/Flashcards/FlashcardStore.cpp
  ${REPO_ROOT}/lib/Flashcards/FsrsScheduler.cpp
  ${REPO_ROOT}/lib/Flashcards/SchedulingFuzzer.cpp
  ${REPO_ROOT}/lib/Flashcards/StudyQueueBuilder.cpp
)
target_include_directories(flashcard_store_integration_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Flashcards
  ${REPO_ROOT}/lib/Serialization
)
target_link_libraries(flashcard_store_integration_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(flashcard_store_integration_tests)
