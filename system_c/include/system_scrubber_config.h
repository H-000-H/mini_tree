#ifndef SYSTEM_SCRUBBER_CONFIG_H
#define SYSTEM_SCRUBBER_CONFIG_H

/* Flash bit-rot scrubber policy (not DTS-derived).
 * CRC baseline comes from build-generated system_scrubber_crc_gen.h. */

#define SYSTEM_SCRUBBER_CHUNK_BYTES    32
#define SYSTEM_SCRUBBER_INTERVAL_MS    200

#if defined __has_include
#  if __has_include("system_scrubber_crc_gen.h")
#    include "system_scrubber_crc_gen.h"
#  else
#    define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000U
#  endif
#else
#  include "system_scrubber_crc_gen.h"
#endif

#endif /* SYSTEM_SCRUBBER_CONFIG_H */
