#pragma once
#include <MD5Builder.h>

#include <string>

/**
 * Digest of what a book is and how it is laid out: its title, its author and
 * the file name of every spine item in reading order.
 *
 * None of that is a property of the archive's bytes, so a copy repacked at
 * another compression level — or run through the EPUB Optimizer, which
 * re-encodes images and rewrites the OPF manifest but leaves the spine alone —
 * keeps this digest while its partial MD5 changes. The spine list is also what
 * a KOReader xpointer counts: `/body/DocFragment[N]` is the Nth entry of it, so
 * two copies agreeing here agree on where a stored position points.
 *
 * Hrefs are fed in reading order; the digest is order-sensitive because the
 * xpointer is.
 */
class KOReaderStructureDigest {
 public:
  KOReaderStructureDigest(const std::string& title, const std::string& authors);
  void addSpineHref(const std::string& href);

  /** @return 32-character lowercase hex string, or empty if no spine item was added */
  std::string finish();

 private:
  MD5Builder md5;
  int spineCount = 0;
};

/**
 * Calculate KOReader document ID (partial MD5 hash).
 *
 * KOReader identifies documents using a partial MD5 hash of the file content.
 * The algorithm reads 1024 bytes at specific offsets and computes the MD5 hash
 * of the concatenated data.
 *
 * Offsets are calculated as: 1024 << (2*i) for i = -1 to 10
 * Producing: 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304,
 *            16777216, 67108864, 268435456, 1073741824 bytes
 *
 * If an offset is beyond the file size, it is skipped.
 */
class KOReaderDocumentId {
 public:
  /**
   * Calculate the KOReader document hash for a file (binary/content-based).
   *
   * @param filePath Path to the file (typically an EPUB)
   * @return 32-character lowercase hex string, or empty string on failure
   */
  static std::string calculate(const std::string& filePath);

  /**
   * Calculate document hash from filename only (filename-based sync mode).
   * This is simpler and works when files have the same name across devices.
   *
   * @param filePath Path to the file (only the filename portion is used)
   * @return 32-character lowercase hex MD5 of the filename
   */
  static std::string calculateFromFilename(const std::string& filePath);

  /**
   * Calculate the digest that names the work rather than the file: its title
   * and author, normalized. Another edition of the same book matches here and
   * nowhere stronger.
   *
   * @return 32-character lowercase hex string, or empty when both are empty
   */
  static std::string calculateFromMetadata(const std::string& title, const std::string& authors);

 private:
  // Size of each chunk to read at each offset
  static constexpr size_t CHUNK_SIZE = 1024;

  // Number of offsets to try (i = -1 to 10, so 12 offsets)
  static constexpr int OFFSET_COUNT = 12;

  // Calculate offset for index i: 1024 << (2*i)
  static size_t getOffset(int i);
};
