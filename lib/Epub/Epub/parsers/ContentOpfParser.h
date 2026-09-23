#pragma once
#include <Print.h>

#include <algorithm>
#include <deque>
#include <vector>

#include "Epub.h"
#include "OpfStructureSink.h"
#include "expat.h"

class BookMetadataCache;

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_LANGUAGE,
    IN_BOOK_IDENTIFIER,
    IN_MANIFEST,
    IN_SPINE,
    IN_GUIDE,
  };

  const std::string& cachePath;
  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  const bool metadataOnly;
  OpfStructureSink* structureSink;
  bool metadataComplete = false;
  HalFile tempItemStore;
  std::string coverItemId;
  bool hasExplicitStartReference = false;
  // XML character data is allowed to arrive in several callbacks for one text
  // node (notably around character references). Keep whitespace and creator
  // separation as element state rather than inferring either from callbacks.
  bool metadataSpacePending = false;
  bool authorSeparatorPending = false;

  // The `<package unique-identifier>` attribute and the `dc:identifier`
  // elements it selects between. Held verbatim: the structure recipe trims the
  // chosen one and takes it as written, so neither is folded or collapsed.
  std::string uniqueIdentifierRef;
  std::string identifierElementId;
  std::string identifierText;
  std::string packageIdentifier;
  std::string firstIdentifier;
  bool hasPackageIdentifier = false;
  bool hasFirstIdentifier = false;
  bool packageIdentifierEmitted = false;

  // expat resolves `&amp;` in an attribute value; the recipe wants the five
  // characters the OPF wrote. XML_DefaultCurrent replays the current element's
  // markup verbatim into the default handler, which lands here.
  std::string rawMarkup;
  bool capturingRawMarkup = false;

  // Index for fast idref→href lookup (binary search over .items.bin)
  struct ItemIndexEntry {
    uint32_t idHash;      // FNV-1a hash of itemId
    uint16_t idLen;       // length for collision reduction
    uint32_t fileOffset;  // offset in .items.bin
  };
  std::deque<ItemIndexEntry> itemIndex;
  bool useItemIndex = false;

  // FNV-1a hash function
  static uint32_t fnvHash(const std::string& s) {
    uint32_t hash = 2166136261u;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 16777619u;
    }
    return hash;
  }

  // The manifest is spilled to `.items.bin` whenever a consumer needs an
  // idref resolved: the spine cache, the structure digest, or both.
  bool wantsManifestItems() const { return cache != nullptr || structureSink != nullptr; }
  void emitPackageIdentifier();
  std::string rawHrefOfCurrentElement();

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);
  static void defaultHandler(void* userData, const XML_Char* s, int len);

 public:
  std::string title;
  std::string author;
  std::string language;
  std::string tocNcxPath;
  std::string tocNavPath;  // EPUB 3 nav document path
  std::string coverItemHref;
  std::string guideCoverPageHref;  // Guide reference with type="cover" or "cover-page" (points to XHTML wrapper)
  std::string textReferenceHref;
  std::vector<std::string> cssFiles;  // CSS stylesheet paths

  explicit ContentOpfParser(const std::string& cachePath, const std::string& baseContentPath, const size_t xmlSize,
                            BookMetadataCache* cache, const bool metadataOnly = false,
                            OpfStructureSink* structureSink = nullptr)
      : cachePath(cachePath),
        baseContentPath(baseContentPath),
        remainingSize(xmlSize),
        cache(cache),
        metadataOnly(metadataOnly),
        structureSink(structureSink) {}
  ~ContentOpfParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
