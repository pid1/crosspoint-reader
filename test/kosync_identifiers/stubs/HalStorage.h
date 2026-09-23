#pragma once

// An in-memory stand-in for the SD card, enough for the manifest spill file
// ContentOpfParser writes and reads back while resolving spine idrefs.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fake {

inline std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;

inline void reset() { files.clear(); }

}  // namespace fake

class HalFile {
 public:
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t pos = 0;

  explicit operator bool() const { return bool(bytes); }
  void close() { bytes.reset(); }
  size_t fileSize() const { return bytes ? bytes->size() : 0; }
  size_t position() const { return pos; }
  int available() const { return bytes ? static_cast<int>(bytes->size() - std::min(pos, bytes->size())) : 0; }
  bool seek(const size_t offset) {
    pos = offset;
    return true;
  }
  bool seekSet(const size_t offset) { return seek(offset); }
  int read(void* out, size_t size) {
    if (!bytes) return -1;
    size = std::min(size, bytes->size() - std::min(pos, bytes->size()));
    std::memcpy(out, bytes->data() + std::min(pos, bytes->size()), size);
    pos += size;
    return static_cast<int>(size);
  }
  size_t write(const uint8_t* data, const size_t size) {
    if (!bytes) return 0;
    bytes->resize(std::max(bytes->size(), pos + size));
    std::memcpy(bytes->data() + pos, data, size);
    pos += size;
    return size;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool exists(const char* path) const { return fake::files.count(path) != 0; }
  bool remove(const char* path) { return fake::files.erase(path) != 0; }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    const auto found = fake::files.find(path);
    if (found == fake::files.end()) return false;
    file.bytes = found->second;
    file.pos = 0;
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    file.bytes = std::make_shared<std::vector<uint8_t>>();
    file.pos = 0;
    fake::files[path] = file.bytes;
    return true;
  }
};

#define Storage HalStorage::getInstance()
