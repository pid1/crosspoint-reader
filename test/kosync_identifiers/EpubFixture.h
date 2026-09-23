#pragma once

// Builds an EPUB in memory around a fixture's real container.xml and OPF, and
// reads one back the way the firmware does: ContainerParser finds the package
// document, ContentOpfParser walks it. The archive itself is miniz, standing in
// for ZipFile, which is an archive reader and nothing more.

#include <miniz.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ContainerParser.h"
#include "ContentOpfParser.h"
#include "KOReaderStructureDigest.h"

namespace fixture {

inline std::string read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("fixture not readable: " + path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// Ordered so a rebuild at another compression level keeps entry order, which is
// what makes the repack case a repack rather than a rewrite.
struct Epub {
  std::vector<std::pair<std::string, std::string>> entries;

  std::string* find(const std::string& name) {
    for (auto& entry : entries) {
      if (entry.first == name) return &entry.second;
    }
    return nullptr;
  }

  void put(const std::string& name, const std::string& bytes) {
    if (std::string* existing = find(name)) {
      *existing = bytes;
      return;
    }
    entries.emplace_back(name, bytes);
  }

  void erase(const std::string& name) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->first == name) {
        entries.erase(it);
        return;
      }
    }
  }

  std::string pack(const int level = MZ_DEFAULT_LEVEL) const {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) throw std::runtime_error("zip writer init failed");
    for (const auto& [name, bytes] : entries) {
      const bool stored = name == "mimetype";
      if (!mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(),
                                 stored ? MZ_NO_COMPRESSION : static_cast<mz_uint>(level))) {
        mz_zip_writer_end(&zip);
        throw std::runtime_error("zip add failed: " + name);
      }
    }
    void* buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
      mz_zip_writer_end(&zip);
      throw std::runtime_error("zip finalize failed");
    }
    std::string packed(static_cast<const char*>(buffer), size);
    mz_zip_writer_end(&zip);
    return packed;
  }
};

// Every href the OPF names, resolved against the OPF's own directory.
inline std::vector<std::string> manifestMembers(const std::string& opf, const std::string& opfPath) {
  const std::string directory = opfPath.substr(0, opfPath.find_last_of('/') + 1);
  std::vector<std::string> members;
  for (size_t at = opf.find("href=\""); at != std::string::npos; at = opf.find("href=\"", at + 1)) {
    const size_t begin = at + 6;
    const size_t end = opf.find('"', begin);
    if (end == std::string::npos) break;
    std::string href = opf.substr(begin, end - begin);
    const size_t fragment = href.find('#');
    if (fragment != std::string::npos) href.resize(fragment);
    if (!href.empty() && href.find("://") == std::string::npos) members.push_back(directory + href);
  }
  return members;
}

// mimetype, the fixture's own container.xml and OPF, and one placeholder member
// per manifest href. The digest reads none of the placeholder bytes, which is
// the property the repack and injection cases put under test.
inline Epub build(const std::string& fixtureDir, const std::string& opfPath) {
  const std::string opf = read(fixtureDir + "/" + opfPath);
  Epub epub;
  epub.put("mimetype", "application/epub+zip");
  epub.put("META-INF/container.xml", read(fixtureDir + "/META-INF/container.xml"));
  epub.put(opfPath, opf);
  for (const std::string& member : manifestMembers(opf, opfPath)) {
    epub.put(member, "<html><body><p>" + member + "</p></body></html>");
  }
  return epub;
}

inline std::string member(const std::string& archive, const std::string& name) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0)) throw std::runtime_error("zip open failed");
  size_t size = 0;
  void* bytes = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &size, 0);
  mz_zip_reader_end(&zip);
  if (!bytes) return {};
  std::string extracted(static_cast<const char*>(bytes), size);
  mz_free(bytes);
  return extracted;
}

// The firmware's own two passes: container.xml for the package document, then
// the OPF for the package identifier and the spine.
inline std::string structureDigest(const std::string& archive) {
  const std::string container = member(archive, "META-INF/container.xml");
  if (container.empty()) return {};

  ContainerParser containerParser(container.size());
  if (!containerParser.setup()) throw std::runtime_error("container parser setup failed");
  containerParser.write(reinterpret_cast<const uint8_t*>(container.data()), container.size());
  if (containerParser.fullPath.empty()) return {};

  const std::string opf = member(archive, containerParser.fullPath);
  if (opf.empty()) return {};

  KOReaderStructureDigest digest;
  const std::string cachePath = "/cache";
  const std::string basePath;
  ContentOpfParser opfParser(cachePath, basePath, opf.size(), nullptr, false, &digest);
  if (!opfParser.setup()) throw std::runtime_error("opf parser setup failed");
  opfParser.write(reinterpret_cast<const uint8_t*>(opf.data()), opf.size());
  return digest.finish();
}

}  // namespace fixture
