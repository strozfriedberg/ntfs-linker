#include <scope/test.h>

#include "usn.h"
#include <sstream>

void advanceStream(bool runSparse, bool isSparse) {
  std::stringstream ss;
  static char buffer[USN_BUFFER_SIZE];

  for(unsigned int i = 0; i < 10 * USN_BUFFER_SIZE && isSparse; i++) {
    ss << "\x0000";
  }
  for(unsigned int i = 0; i < 13; i++) {
    ss << "\x00FF";
  }
  advanceStream(ss, buffer, runSparse);
  SCOPE_ASSERT(ss.good());
  SCOPE_ASSERT(!ss.fail());
  SCOPE_ASSERT(ss.tellg() <= 10 * USN_BUFFER_SIZE);
}

SCOPE_TEST(testAdvancing) {
  for(int i = 0; i < 4; i++)
    advanceStream(i&1, i&2);
}
