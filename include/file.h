#include <string>
#pragma once

/*
Represents a file. A map of record numbers to these file objects can be used to reconstruct the full path
*/
class File {
  public:
    File () :
      Name(""),
      Record(0),
      Parent(0),
      Timestamp(""),
      Valid(false) {}
    File(std::string name, unsigned int record, unsigned int parent, std::string timestamp) :
      Name(name),
      Record(record),
      Parent(parent),
      Timestamp(timestamp),
      Valid(true) {}
    std::string Name;
    unsigned int Record, Parent;
    std::string Timestamp;
    bool Valid;
};
