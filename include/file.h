#include <string>
#ifndef file_h
#define file_h

/*
Represents a file. A map of record numbers to these file objects can be used to reconstruct the full path
*/
class File {
  public:
    File () :
      name(""),
      record_no(0),
      par_record_no(0),
      timestamp(""),
      valid(false) {}
    File(std::string Name, unsigned int RecordNo, unsigned int ParRecordNo, std::string Timestamp) :
      name(Name),
      record_no(RecordNo),
      par_record_no(ParRecordNo),
      timestamp(Timestamp),
      valid(true) {}
    std::string name;
    unsigned int record_no, par_record_no;
    std::string timestamp;
    bool valid;
};
#endif
