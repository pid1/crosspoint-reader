#pragma once
#include <MD5Builder.h>

#include <string>

#include "Epub/parsers/OpfStructureSink.h"

/**
 * The kosync `structure` identifier: md5 over the package identifier and the
 * spine, as SPEC.md §5.8 pins the recipe.
 *
 * The lines are the `dc:identifier` named by `<package unique-identifier>`
 * when it is non-empty, then every spine item's manifest href exactly as the
 * OPF writes it, in spine order. They are joined with `\n`, with no trailing
 * newline, and the digest is the md5 of the UTF-8 bytes.
 *
 * No file contents reach it. A repack, an image re-encode and a stylesheet
 * injected into every chapter all rewrite entry bytes and leave the spine
 * alone, so the digest holds where the partial MD5 of §8 moves. The spine href
 * list is also what a KOReader xpointer counts — `/body/DocFragment[N]` is the
 * Nth entry — so two copies agreeing here agree on where a stored position
 * points.
 */
class KOReaderStructureDigest final : public OpfStructureSink {
 public:
  KOReaderStructureDigest();

  void setPackageIdentifier(const char* data, size_t length) override;
  void addSpineHref(const char* data, size_t length) override;

  /** @return 32-character lowercase hex string, or empty when the spine was empty */
  std::string finish();

 private:
  void addLine(const char* data, size_t length);

  MD5Builder md5;
  bool needsSeparator = false;
  int spineCount = 0;
};
