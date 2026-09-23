#include "KOReaderStructureDigest.h"

#include <Logging.h>

namespace {
constexpr char LINE_SEPARATOR = '\n';
}  // namespace

KOReaderStructureDigest::KOReaderStructureDigest() { md5.begin(); }

void KOReaderStructureDigest::addLine(const char* data, const size_t length) {
  if (needsSeparator) {
    md5.add(reinterpret_cast<const uint8_t*>(&LINE_SEPARATOR), 1);
  }
  md5.add(reinterpret_cast<const uint8_t*>(data), length);
  needsSeparator = true;
}

void KOReaderStructureDigest::setPackageIdentifier(const char* data, const size_t length) {
  if (length == 0) {
    return;
  }
  addLine(data, length);
}

void KOReaderStructureDigest::addSpineHref(const char* data, const size_t length) {
  addLine(data, length);
  spineCount++;
}

std::string KOReaderStructureDigest::finish() {
  // A container with no spine has no structure digest: the identifier alone
  // describes an edition, not a layout, and is offered as a guess otherwise.
  if (spineCount == 0) {
    return "";
  }
  md5.calculate();
  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Structure hash: %s (from %d spine items)", result.c_str(), spineCount);
  return result;
}
