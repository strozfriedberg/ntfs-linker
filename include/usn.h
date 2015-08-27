#include "file.h"
extern "C" {
#include "sqlite3.h"
}
#include <map>
#include <iostream>

#ifndef usn_header
#define usn_header

std::string getUSNColumnHeaders();

std::string decodeUSNReason(int reason);

std::string parseUSNJrnlRecord(char* buffer, std::vector<File>& records);

void parseUSN(std::vector<File>& records, sqlite3* db, std::istream& input = std::cin, std::ostream& output = std::cout);

class UsnRecord {
public:
  unsigned long long file_ref_no, mft_record_no, par_file_ref_no, par_record, prev_par_record, usn, timestamp;
  unsigned int reason, file_len, name_offset;
  std::string file_name, prev_file_name;

  UsnRecord(char* buffer, std::vector<File>& records, int len = -1);
  UsnRecord();
  void clearFields();
  std::string toString(std::vector<File>& records);
  std::string toCreateString(std::vector<File>& records);
  std::string toDeleteString(std::vector<File>& records);
  std::string toRenameString(std::vector<File>& records);
  std::string toMoveString(std::vector<File>& records);

  void insert(sqlite3* db, sqlite3_stmt* stmt, std::vector<File>& records);
  void insertEvent(unsigned int type, sqlite3* db, sqlite3_stmt* stmt, std::vector<File>& records);
};

#endif
