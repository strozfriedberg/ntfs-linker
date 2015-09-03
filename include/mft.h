#include "file.h"
#include "sqlite_helper.h"

#include <sqlite3.h>
#include <iostream>
#include <string>
#include <map>

#pragma once

/*
Returns the column headers used in the MFT csv file
*/
std::string getMFTColumnHeaders();

/*
Parses all the MFT records
*/
void parseMFT(std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output, const bool initRecords);

class SIAttribute {
public:
  SIAttribute() : Created(0), Modified(0), MFTModified(0), Accessed(0),
                  Usn(0), Valid(false) {}
  SIAttribute(char* buffer);

  uint64_t Created, Modified, MFTModified, Accessed;
  uint64_t Usn;
private:
  bool Valid;
};

class FNAttribute {
public:
  FNAttribute() : Parent(0), Created(0), Modified(0), MFTModified(0), Accessed(0),
                  LogicalSize(0), PhysicalSize(0), Name(""), Valid(false) {}
  FNAttribute(char* buffer);
  friend bool operator<(FNAttribute a, FNAttribute b);
  unsigned int Parent;
  uint64_t Created, Modified, MFTModified, Accessed;
  uint64_t LogicalSize, PhysicalSize;
  std::string Name;
  unsigned int countAscii();
  bool Valid;
};

bool operator<(FNAttribute a, FNAttribute b);

class MFTRecord {
public:
  MFTRecord(char* buffer, unsigned int len=1024);
  std::string toString(std::vector<File>& records);
  void insert(sqlite3_stmt* stmt, std::vector<File>& records);
  File asFile();

  unsigned int Record;
  SIAttribute Sia;

  // There may be multiple, but we'll pick just one.
  FNAttribute Fna;

private:
  uint64_t Lsn;
  bool isDir, isAllocated;
};
