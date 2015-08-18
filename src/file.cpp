#include "file.h"
#include <string>
file::file(std::string s, unsigned int a, unsigned int b, unsigned long long time) {
  name = s;
  record_no = a;
  par_record_no = b;
  timestamp = time;
}
