#pragma once
#include <cstddef>

/**
 * Receives the two OPF fields a structure identifier is built from, in the
 * order the recipe lays them out: the package identifier first, then every
 * spine item's manifest href in spine order.
 *
 * The sink is handed pointers into the parser's own buffers, so an
 * implementation consumes them before returning.
 */
class OpfStructureSink {
 public:
  virtual ~OpfStructureSink() = default;

  /**
   * The `dc:identifier` named by `<package unique-identifier>`, or the first
   * `dc:identifier` when that name resolves to nothing. Trimmed, and called
   * once, before any spine href. An empty value contributes no line.
   */
  virtual void setPackageIdentifier(const char* data, size_t length) = 0;

  /**
   * One spine item's manifest `href`, as the attribute writes it: not
   * percent-decoded, not resolved against the OPF directory, not reduced to a
   * basename. Any `#fragment` is already stripped.
   */
  virtual void addSpineHref(const char* data, size_t length) = 0;
};
