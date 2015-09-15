#include <scope/test.h>
#include <cstring>

#include "helper_functions.h"

SCOPE_TEST(testUnpack) {
  SCOPE_ASSERT_EQUAL(16, hex_to_long("\x0010\x0000\x0000\x0000", 4));
  SCOPE_ASSERT_EQUAL(255, hex_to_long("\x00FF\x0000\x0000\x0000", 4));
}

SCOPE_TEST(testFixup) {
  static char buffer[4096];
  memset(buffer, 0, 4096);
  buffer[4] = 0x10;
  buffer[6] = 0x9;
  buffer[0x10] = 0x7F;
  buffer[0x11] = 0x7F;
  for (int i = 0; i < 4096; i += 512) {
    buffer[i+510] = 0x7F;
    buffer[i + 511] = 0x7F;
  }

  SCOPE_ASSERT_EQUAL(false, doFixup(buffer, 4096));

  for (int i = 0x22; i < 4096; i++) {
    SCOPE_ASSERT_EQUAL(0, buffer[i]);
  }
}
