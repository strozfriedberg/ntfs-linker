#include <string>
#ifndef file_h
#define file_h

/*
Represents a file. A map of record numbers to these file objects can be used to reconstruct the full path
*/
class File {
  public:
    File(std::string name, unsigned int record_no, unsigned int par_record_no, unsigned long long time);
    std::string name;
    unsigned int record_no, par_record_no;
    unsigned long long timestamp;
};
#endif
