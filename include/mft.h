#include "file.h"
extern "C" {
#include "sqlite3.h"
}
#include <iostream>
#include <string>
#include <map>

#ifndef mft
#define mft

/*
Returns the column headers used in the MFT csv file
*/
std::string getMFTColumnHeaders();

/*
Parses through the $MFT input stream
to initialize the map of file records
*/
void initMFTMap(std::istream& input, std::vector<File>& records);

void freeMFTMap(std::vector<File>& records);


/*
Parses all the MFT records
*/
void parseMFT(std::vector<File>& records, sqlite3* db, std::istream& input = std::cin, std::ostream& output = std::cout);

class MFT_Record {
private:
  long long lsn, mft_record_no, update_seq_no, sia_created, sia_modified, sia_mft_modified, sia_accessed;
  long long fna_created, fna_modified, fna_mft_modified, fna_accessed;
  long long logical_size, physical_size;
  unsigned long long parent_dir;
  bool isDir, isAllocated;
  int sia_flags;
  std::string file_name;

public:
  MFT_Record(char* buffer, std::vector<File>& records);
  std::string toString(std::vector<File>& records);
  void insert(sqlite3* db, sqlite3_stmt* stmt, std::vector<File>& records);
};


#endif
