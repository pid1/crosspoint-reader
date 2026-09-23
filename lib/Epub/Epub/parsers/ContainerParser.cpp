#include "ContainerParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

namespace {
constexpr char MEDIA_TYPE_PACKAGE[] = "application/oebps-package+xml";
}  // namespace

bool ContainerParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_ERR("CTR", "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  return true;
}

ContainerParser::~ContainerParser() { destroyXmlParser(parser); }

size_t ContainerParser::write(const uint8_t data) { return write(&data, 1); }

size_t ContainerParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      LOG_DBG("CTR", "Couldn't allocate buffer");
      destroyXmlParser(parser);
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      LOG_ERR("CTR", "Parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }
  return size;
}

void XMLCALL ContainerParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ContainerParser*>(userData);

  // Simple state tracking to ensure we are looking at the valid schema structure
  if (self->state == START && xmlLocalNameEquals(name, "container")) {
    self->state = IN_CONTAINER;
    return;
  }

  if (self->state == IN_CONTAINER && xmlLocalNameEquals(name, "rootfiles")) {
    self->state = IN_ROOTFILES;
    return;
  }

  // The first rootfile declaring the package media-type, and the first of any
  // type when none declares it. EPUB 3 makes the first declared one the default
  // rendition, which is what kosync's structure identifier is taken over
  // (SPEC.md §5.8).
  if (self->state == IN_ROOTFILES && xmlLocalNameEquals(name, "rootfile") && !self->hasPackageRootfile) {
    const char* mediaType = nullptr;
    const char* path = nullptr;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "media-type") == 0) {
        mediaType = atts[i + 1];
      } else if (strcmp(atts[i], "full-path") == 0) {
        path = atts[i + 1];
      }
    }

    if (!path) {
      return;
    }
    if (mediaType && strcmp(mediaType, MEDIA_TYPE_PACKAGE) == 0) {
      self->fullPath = path;
      self->hasPackageRootfile = true;
    } else if (self->fullPath.empty()) {
      self->fullPath = path;
    }
  }
}

void XMLCALL ContainerParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ContainerParser*>(userData);

  if (self->state == IN_ROOTFILES && xmlLocalNameEquals(name, "rootfiles")) {
    self->state = IN_CONTAINER;
  } else if (self->state == IN_CONTAINER && xmlLocalNameEquals(name, "container")) {
    self->state = START;
  }
}
