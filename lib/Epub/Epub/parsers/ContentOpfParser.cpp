#include "ContentOpfParser.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Serialization.h>
#include <XmlParserUtils.h>

#include <cctype>
#include <cstring>
#include <string_view>

#include "Epub/BookMetadataCache.h"

namespace {
constexpr char MEDIA_TYPE_NCX[] = "application/x-dtbncx+xml";
constexpr char MEDIA_TYPE_CSS[] = "text/css";
constexpr char MEDIA_TYPE_IMAGE_PREFIX[] = "image/";
constexpr char itemCacheFile[] = "/.items.bin";

bool startsWithImageMediaType(const std::string& mediaType) {
  constexpr size_t prefixLen = sizeof(MEDIA_TYPE_IMAGE_PREFIX) - 1;
  if (mediaType.size() < prefixLen) {
    return false;
  }

  for (size_t i = 0; i < prefixLen; ++i) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(mediaType[i])));
    if (c != MEDIA_TYPE_IMAGE_PREFIX[i]) {
      return false;
    }
  }

  return true;
}

bool isXmlWhitespace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Metadata text comes straight from the (untrusted) OPF; unbounded growth on
// a multi-megabyte title would exhaust the heap. Downstream consumers truncate
// far below this anyway, so overflow is clamped, not fatal.
constexpr size_t MAX_METADATA_TEXT = 512;

// Between two `dc:creator` values in the display string.
constexpr char AUTHOR_SEPARATOR[] = ", ";

void appendMetadataText(std::string& out, const XML_Char* text, const int len, bool& spacePending,
                        bool* separatorPending = nullptr) {
  if (out.size() >= MAX_METADATA_TEXT) return;  // already clamped and logged
  for (int i = 0; i < len; i++) {
    const char c = text[i];
    if (isXmlWhitespace(c)) {
      spacePending = true;
      continue;
    }

    if (out.size() >= MAX_METADATA_TEXT) {
      LOG_DBG("COF", "Metadata text exceeds %u bytes; truncating", static_cast<unsigned>(MAX_METADATA_TEXT));
      return;
    }
    if (separatorPending != nullptr && *separatorPending) {
      out.append(AUTHOR_SEPARATOR);
      *separatorPending = false;
      spacePending = false;
    } else if (spacePending && !out.empty()) {
      out.push_back(' ');
    }
    spacePending = false;
    out.push_back(c);
  }
}

// dc:identifier text reaches the digest with only its ends trimmed, so it is
// accumulated whole rather than through appendMetadataText's collapsing.
void appendIdentifierText(std::string& out, const XML_Char* text, const int len) {
  const size_t room = MAX_METADATA_TEXT - std::min(out.size(), MAX_METADATA_TEXT);
  out.append(text, std::min(static_cast<size_t>(len), room));
}

std::string_view trimmed(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && isXmlWhitespace(text[begin])) begin++;
  while (end > begin && isXmlWhitespace(text[end - 1])) end--;
  return std::string_view{text}.substr(begin, end - begin);
}

// A fragment addresses a place inside the file, not the file, so the recipe
// drops it. What is left is the attribute as the parser yields it: entity
// references expanded, percent escapes intact, no OPF directory in front.
std::string_view hrefWithoutFragment(std::string_view href) {
  const size_t hash = href.find('#');
  return hash == std::string_view::npos ? href : href.substr(0, hash);
}

}  // namespace

bool ContentOpfParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_DBG("COF", "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

ContentOpfParser::~ContentOpfParser() {
  destroyXmlParser(parser);
  if (metadataOnly || !wantsManifestItems()) {
    return;
  }
  if (tempItemStore) {
    tempItemStore.close();
  }
  const auto itemCachePath = cachePath + itemCacheFile;
  if (Storage.exists(itemCachePath.c_str())) {
    Storage.remove(itemCachePath.c_str());
  }
}

size_t ContentOpfParser::write(const uint8_t data) { return write(&data, 1); }

size_t ContentOpfParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);

    if (!buf) {
      LOG_ERR("COF", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      LOG_DBG("COF", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;

    if (metadataOnly && metadataComplete) {
      const size_t processed = size - remainingInBuffer;
      return processed < size ? processed : size - 1;
    }
  }

  return size;
}

void XMLCALL ContentOpfParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)atts;

  if (self->metadataOnly && self->metadataComplete) {
    return;
  }
  if (self->metadataOnly && (xmlLocalNameEquals(name, "manifest") || xmlLocalNameEquals(name, "spine") ||
                             xmlLocalNameEquals(name, "guide"))) {
    self->metadataComplete = true;
    return;
  }

  if (self->state == START && xmlLocalNameEquals(name, "package")) {
    self->state = IN_PACKAGE;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "unique-identifier") == 0) {
        self->uniqueIdentifierRef = atts[i + 1];
      }
    }
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "metadata")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "title")) {
    // Only capture the first title element; subsequent ones are subtitles
    if (self->title.empty()) {
      self->state = IN_BOOK_TITLE;
      self->metadataSpacePending = false;
    }
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "creator")) {
    self->state = IN_BOOK_AUTHOR;
    self->metadataSpacePending = false;
    self->authorSeparatorPending = !self->author.empty();
    self->creatorStart = self->author.size();
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "language")) {
    self->state = IN_BOOK_LANGUAGE;
    self->metadataSpacePending = false;
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "identifier")) {
    self->state = IN_BOOK_IDENTIFIER;
    self->identifierElementId.clear();
    self->identifierText.clear();
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0) {
        self->identifierElementId = atts[i + 1];
      }
    }
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "manifest")) {
    self->state = IN_MANIFEST;
    if (self->wantsManifestItems() &&
        !Storage.openFileForWrite("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open temp items file for writing. This is probably going to be a fatal error.");
    }
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "spine")) {
    self->state = IN_SPINE;
    if (self->wantsManifestItems() &&
        !Storage.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
    }
    self->emitPackageIdentifier();

    // Sort the (unconditionally-built) item index so every idref lookup uses binary
    // search. Without this, small/medium manifests fell back to an O(spine × manifest)
    // linear rescan of .items.bin per itemref (up to ~200ms/item at large scale).
    if (!self->itemIndex.empty()) {
      std::sort(self->itemIndex.begin(), self->itemIndex.end(), [](const ItemIndexEntry& a, const ItemIndexEntry& b) {
        return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
      });
      self->useItemIndex = true;
      LOG_DBG("COF", "Using fast index for %zu manifest items", self->itemIndex.size());
    }
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "guide")) {
    self->state = IN_GUIDE;
    // TODO Remove print
    LOG_DBG("COF", "Entering guide state.");
    if (self->cache && !Storage.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
    }
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "meta")) {
    bool isCover = false;
    std::string coverItemId;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0 && strcmp(atts[i + 1], "cover") == 0) {
        isCover = true;
      } else if (strcmp(atts[i], "content") == 0) {
        coverItemId = atts[i + 1];
      }
    }

    if (isCover) {
      self->coverItemId = coverItemId;
    }
    return;
  }

  if (self->state == IN_MANIFEST && xmlLocalNameEquals(name, "item")) {
    std::string itemId;
    std::string href;
    std::string mediaType;
    std::string properties;

    // The href the structure recipe hashes. The resolved one beside it is
    // percent-decoded and carries the OPF directory, all of which the recipe
    // forbids, so the two cannot be the same string.
    std::string structureHref;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0) {
        itemId = atts[i + 1];
      } else if (strcmp(atts[i], "href") == 0) {
        structureHref = hrefWithoutFragment(atts[i + 1]);
        href = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->baseContentPath + atts[i + 1]));
      } else if (strcmp(atts[i], "media-type") == 0) {
        mediaType = atts[i + 1];
      } else if (strcmp(atts[i], "properties") == 0) {
        properties = atts[i + 1];
      }
    }

    // Record index entry for fast lookup later
    if (self->tempItemStore) {
      ItemIndexEntry entry;
      entry.idHash = fnvHash(itemId);
      entry.idLen = static_cast<uint16_t>(itemId.size());
      entry.fileOffset = static_cast<uint32_t>(self->tempItemStore.position());
      self->itemIndex.push_back(entry);
    }

    if (self->tempItemStore) {
      serialization::writeString(self->tempItemStore, itemId);
      serialization::writeString(self->tempItemStore, href);
      if (self->structureSink) {
        serialization::writeString(self->tempItemStore, structureHref);
      }
    }

    if (itemId == self->coverItemId) {
      // Some EPUBs set meta name="cover" to an XHTML wrapper item.
      // Only treat it as a cover image when the manifest media-type is image/*.
      if (startsWithImageMediaType(mediaType)) {
        self->coverItemHref = href;
      } else {
        LOG_DBG("COF", "Ignoring meta cover item '%s' with non-image media type: %s", itemId.c_str(),
                mediaType.c_str());
      }
    }

    if (mediaType == MEDIA_TYPE_NCX) {
      if (self->tocNcxPath.empty()) {
        self->tocNcxPath = href;
      } else {
        LOG_DBG("COF", "Warning: Multiple NCX files found in manifest. Ignoring duplicate: %s", href.c_str());
      }
    }

    // Collect CSS files
    if (mediaType == MEDIA_TYPE_CSS) {
      self->cssFiles.push_back(href);
    }

    // EPUB 3: Check for nav document (properties contains "nav")
    if (!properties.empty() && self->tocNavPath.empty()) {
      // Properties is space-separated, check if "nav" is present as a word
      if (properties == "nav" || properties.find("nav ") == 0 || properties.find(" nav") != std::string::npos) {
        self->tocNavPath = href;
        LOG_DBG("COF", "Found EPUB 3 nav document: %s", href.c_str());
      }
    }

    // EPUB 3: Check for cover image (properties contains "cover-image")
    if (!properties.empty() && self->coverItemHref.empty()) {
      if (properties == "cover-image" || properties.find("cover-image ") == 0 ||
          properties.find(" cover-image") != std::string::npos) {
        self->coverItemHref = href;
      }
    }
    return;
  }

  // NOTE: This relies on spine appearing after item manifest (which is pretty safe as it's part of the EPUB spec)
  // Only resolve idrefs when something consumes them
  if (self->wantsManifestItems()) {
    if (self->state == IN_SPINE && xmlLocalNameEquals(name, "itemref")) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "idref") == 0) {
          const std::string idref = atts[i + 1];
          std::string href;
          std::string structureHref;
          bool found = false;

          if (self->useItemIndex) {
            // Fast path: binary search
            uint32_t targetHash = fnvHash(idref);
            uint16_t targetLen = static_cast<uint16_t>(idref.size());

            auto it = std::lower_bound(self->itemIndex.begin(), self->itemIndex.end(),
                                       ItemIndexEntry{targetHash, targetLen, 0},
                                       [](const ItemIndexEntry& a, const ItemIndexEntry& b) {
                                         return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
                                       });

            // Check for match (may need to check a few due to hash collisions)
            while (it != self->itemIndex.end() && it->idHash == targetHash) {
              self->tempItemStore.seek(it->fileOffset);
              std::string itemId;
              serialization::readString(self->tempItemStore, itemId);
              if (itemId == idref) {
                serialization::readString(self->tempItemStore, href);
                if (self->structureSink) {
                  serialization::readString(self->tempItemStore, structureHref);
                }
                found = true;
                break;
              }
              ++it;
            }
          } else {
            // Fallback linear scan, only reached when the index is empty (no manifest
            // items). The fast binary-search path above is used for all real manifests.
            self->tempItemStore.seek(0);
            std::string itemId;
            while (self->tempItemStore.available()) {
              serialization::readString(self->tempItemStore, itemId);
              serialization::readString(self->tempItemStore, href);
              if (self->structureSink) {
                serialization::readString(self->tempItemStore, structureHref);
              }
              if (itemId == idref) {
                found = true;
                break;
              }
            }
          }

          if (found && self->cache) {
            self->cache->createSpineEntry(href);
          }
          // An item carrying no href describes no file, so it contributes no line.
          if (found && self->structureSink && !structureHref.empty()) {
            self->structureSink->addSpineHref(structureHref.data(), structureHref.size());
          }
        }
      }
      return;
    }
  }
  // parse the guide
  if (self->state == IN_GUIDE && xmlLocalNameEquals(name, "reference")) {
    std::string type;
    std::string guideHref;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "type") == 0) {
        type = atts[i + 1];
      } else if (strcmp(atts[i], "href") == 0) {
        guideHref = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->baseContentPath + atts[i + 1]));
      }
    }
    if (!guideHref.empty()) {
      // EPUB 2 guides often mark every content file as "text", so that type
      // does not identify a reliable first-reading location. Only use the
      // explicit "start" semantic; otherwise the reader opens at spine index 0.
      if (type == "start" && !self->hasExplicitStartReference) {
        LOG_DBG("COF", "Found %s reference in guide: %s", type.c_str(), guideHref.c_str());
        self->textReferenceHref = guideHref;
        self->hasExplicitStartReference = type == "start";
      } else if ((type == "cover" || type == "cover-page") && self->guideCoverPageHref.empty()) {
        LOG_DBG("COF", "Found cover reference in guide: %s", guideHref.c_str());
        self->guideCoverPageHref = guideHref;
      }
    }
    return;
  }
}

void XMLCALL ContentOpfParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->metadataOnly && self->metadataComplete) {
    return;
  }

  if (self->state == IN_BOOK_TITLE) {
    appendMetadataText(self->title, s, len, self->metadataSpacePending);
    return;
  }

  if (self->state == IN_BOOK_AUTHOR) {
    appendMetadataText(self->author, s, len, self->metadataSpacePending, &self->authorSeparatorPending);
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE) {
    appendMetadataText(self->language, s, len, self->metadataSpacePending);
    return;
  }

  if (self->state == IN_BOOK_IDENTIFIER) {
    appendIdentifierText(self->identifierText, s, len);
    return;
  }
}

void ContentOpfParser::emitPackageIdentifier() {
  if (!structureSink || packageIdentifierEmitted) {
    return;
  }
  packageIdentifierEmitted = true;
  // The named identifier when it has a value, and the first identifier that
  // has one when it does not: an empty match is no answer.
  const std::string& chosen = packageIdentifier.empty() ? firstNonEmptyIdentifier : packageIdentifier;
  structureSink->setPackageIdentifier(chosen.data(), chosen.size());
}

void XMLCALL ContentOpfParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)name;

  if (self->metadataOnly && self->metadataComplete) {
    return;
  }

  if (self->state == IN_SPINE && xmlLocalNameEquals(name, "spine")) {
    self->state = IN_PACKAGE;
    if (self->tempItemStore) self->tempItemStore.close();
    return;
  }

  if (self->state == IN_GUIDE && xmlLocalNameEquals(name, "guide")) {
    self->state = IN_PACKAGE;
    if (self->tempItemStore) self->tempItemStore.close();
    return;
  }

  if (self->state == IN_MANIFEST && xmlLocalNameEquals(name, "manifest")) {
    self->state = IN_PACKAGE;
    if (self->tempItemStore) self->tempItemStore.close();
    return;
  }

  if (self->state == IN_BOOK_TITLE && xmlLocalNameEquals(name, "title")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_AUTHOR && xmlLocalNameEquals(name, "creator")) {
    self->state = IN_METADATA;
    std::string creator = self->author.substr(self->creatorStart);
    if (creator.rfind(AUTHOR_SEPARATOR, 0) == 0) {
      creator.erase(0, std::char_traits<char>::length(AUTHOR_SEPARATOR));
    }
    if (!creator.empty()) {
      self->creators.push_back(std::move(creator));
    }
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE && xmlLocalNameEquals(name, "language")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_IDENTIFIER && xmlLocalNameEquals(name, "identifier")) {
    self->state = IN_METADATA;
    const std::string_view value = trimmed(self->identifierText);
    if (self->firstNonEmptyIdentifier.empty()) {
      self->firstNonEmptyIdentifier = value;
    }
    // `unique-identifier` names an id, so an element without one cannot be it.
    // Several elements may carry that id; the first of them answers.
    if (!self->hasPackageIdentifier && !self->uniqueIdentifierRef.empty() &&
        self->identifierElementId == self->uniqueIdentifierRef) {
      self->hasPackageIdentifier = true;
      self->packageIdentifier = value;
    }
    self->identifierText.clear();
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "metadata")) {
    self->state = IN_PACKAGE;
    self->metadataComplete = true;
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "package")) {
    self->state = START;
    return;
  }
}
