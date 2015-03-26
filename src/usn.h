#include "file.h"
#include "sqlite3.h"
#include <map>
#include <iostream>

#ifndef usn_header
#define usn_header

std::string getUSNColumnHeaders();

std::string decodeUSNReason(int reason);

std::string parseUSNJrnlRecord(char* buffer, std::map<unsigned int, file*>& records);

void parseUSN(std::map<unsigned int, file*>& records, sqlite3* db, std::ostream& create, std::ostream& del, std::ostream& rename, std::ostream& move, std::istream& input = std::cin, std::ostream& output = std::cout);
//void parseUSN(std::map<unsigned int, file*>& records, sqlite3* db, std::istream& input);

class USN_Record {
public:
	unsigned long long file_ref_no, mft_record_no, par_file_ref_no, par_mft_record_no, par_record_after, usn, timestamp;
	unsigned int reason, file_len, name_offset;
	std::string file_name, file_name_after;

	USN_Record(char* buffer, std::map<unsigned int, file*>& records);
	USN_Record();
	//USN_Record& operator=(const USN_Record& rhs);
	void clearFields();
	std::string toString(std::map<unsigned int, file*>& records);
	std::string toCreateString(std::map<unsigned int, file*>& records);
	std::string toDeleteString(std::map<unsigned int, file*>& records);
	std::string toRenameString(std::map<unsigned int, file*>& records);
	std::string toMoveString(std::map<unsigned int, file*>& records);

	void insert(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records);
	void insertRename(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records);
	void insertMove(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records);
};

#endif
