add_executable(KosyncIdentifiersTest
  KosyncIdentifiersTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderIdentifiers.cpp
)

target_include_directories(KosyncIdentifiersTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(KosyncIdentifiersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KosyncIdentifiersTest)
