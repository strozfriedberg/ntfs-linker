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

std::string parseUSNJrnlRecord(char* buffer, std::map<unsigned int, File*>& records);

void parseUSN(std::map<unsigned int, File*>& records, sqlite3* db, std::istream& input = std::cin, std::ostream& output = std::cout);

class USN_Record {
public:
  unsigned long long file_ref_no, mft_record_no, par_file_ref_no, par_record, prev_par_record, usn, timestamp;
  unsigned int reason, file_len, name_offset;
  std::string file_name, prev_file_name;

  USN_Record(char* buffer, std::map<unsigned int, File*>& records);
  USN_Record();
  //USN_Record& operator=(const USN_Record& rhs);
  void clearFields();
  std::string toString(std::map<unsigned int, File*>& records);
  std::string toCreateString(std::map<unsigned int, File*>& records);
  std::string toDeleteString(std::map<unsigned int, File*>& records);
  std::string toRenameString(std::map<unsigned int, File*>& records);
  std::string toMoveString(std::map<unsigned int, File*>& records);

  void insert(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, File*>& records);
  void insertEvent(unsigned int type, sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, File*>& records);
};

#endif
