find_package(EXPAT REQUIRED)

add_executable(KosyncIdentifiersTest
  KosyncIdentifiersTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderIdentifiers.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderStructureDigest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContainerParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/miniz/third_party/miniz.c
)

target_include_directories(KosyncIdentifiersTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/miniz/third_party
)

target_compile_definitions(KosyncIdentifiersTest PRIVATE
  FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

target_link_libraries(KosyncIdentifiersTest PRIVATE
  crosspoint_test_common
  EXPAT::EXPAT
  GTest::gtest_main
)

gtest_discover_tests(KosyncIdentifiersTest)
