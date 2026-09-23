#include "KOReaderDocumentId.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

#include "KOReaderIdentifiers.h"

namespace {
// Separator between the fields of a composite digest. It cannot occur in a
// normalized title, author or spine href, so no two field lists collide.
constexpr char FIELD_SEPARATOR[] = "\n";

// Extract filename from path (everything after last '/')
std::string getFilename(const std::string& path) {
  const size_t pos = path.rfind('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}
}  // namespace

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  const std::string filename = getFilename(filePath);
  if (filename.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename.c_str());
  md5.calculate();

  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Filename hash: %s (from '%s')", result.c_str(), filename.c_str());
  return result;
}

std::string KOReaderDocumentId::calculateFromMetadata(const std::string& title, const std::string& authors) {
  const std::string normalizedTitle = KOReaderIdentifiers::normalizeMetadataText(title);
  const std::string normalizedAuthors = KOReaderIdentifiers::normalizeMetadataText(authors);
  if (normalizedTitle.empty() && normalizedAuthors.empty()) {
    LOG_DBG("KODoc", "No title or author to hash");
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(normalizedTitle.c_str());
  md5.add(FIELD_SEPARATOR);
  md5.add(normalizedAuthors.c_str());
  md5.calculate();

  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Metadata hash: %s (from '%s' / '%s')", result.c_str(), normalizedTitle.c_str(),
          normalizedAuthors.c_str());
  return result;
}

KOReaderStructureDigest::KOReaderStructureDigest(const std::string& title, const std::string& authors) {
  md5.begin();
  md5.add(KOReaderIdentifiers::normalizeMetadataText(title).c_str());
  md5.add(FIELD_SEPARATOR);
  md5.add(KOReaderIdentifiers::normalizeMetadataText(authors).c_str());
}

void KOReaderStructureDigest::addSpineHref(const std::string& href) {
  const std::string_view key = KOReaderIdentifiers::spineHrefKey(href);
  if (key.empty()) {
    return;
  }
  md5.add(FIELD_SEPARATOR);
  md5.add(reinterpret_cast<const uint8_t*>(key.data()), key.size());
  spineCount++;
}

std::string KOReaderStructureDigest::finish() {
  if (spineCount == 0) {
    return "";
  }
  md5.calculate();
  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Structure hash: %s (from %d spine items)", result.c_str(), spineCount);
  return result;
}

size_t KOReaderDocumentId::getOffset(int i) {
  // Offset = 1024 << (2*i)
  // For i = -1: KOReader uses a value of 0
  // For i >= 0: 1024 << (2*i)
  if (i < 0) {
    return 0;
  }
  return CHUNK_SIZE << (2 * i);
}

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  HalFile file;
  if (!Storage.openFileForRead("KODoc", filePath, file)) {
    LOG_DBG("KODoc", "Failed to open file: %s", filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();
  LOG_DBG("KODoc", "Calculating hash for file: %s (size: %zu)", filePath.c_str(), fileSize);

  // Initialize MD5 builder
  MD5Builder md5;
  md5.begin();

  // Buffer for reading chunks
  uint8_t buffer[CHUNK_SIZE];
  size_t totalBytesRead = 0;

  // Read from each offset (i = -1 to 10)
  for (int i = -1; i < OFFSET_COUNT - 1; i++) {
    const size_t offset = getOffset(i);

    // Skip if offset is beyond file size
    if (offset >= fileSize) {
      continue;
    }

    // Seek to offset
    if (!file.seekSet(offset)) {
      LOG_DBG("KODoc", "Failed to seek to offset %zu", offset);
      continue;
    }

    // Read up to CHUNK_SIZE bytes
    const size_t bytesToRead = std::min(CHUNK_SIZE, fileSize - offset);
    const size_t bytesRead = file.read(buffer, bytesToRead);

    if (bytesRead > 0) {
      md5.add(buffer, bytesRead);
      totalBytesRead += bytesRead;
    }
  }

  // Calculate final hash
  md5.calculate();
  std::string result = md5.toString().c_str();

  LOG_DBG("KODoc", "Hash calculated: %s (from %zu bytes)", result.c_str(), totalBytesRead);

  return result;
}
