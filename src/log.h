#include "helper_functions.h"
extern "C" {
#include "sqlite3.h"
}
#include "file.h"
#include <iostream>
#include <vector>

#ifndef log_header
#define log_header

/*
returns the meaning of the operation code
*/
std::string decodeLogFileOpCode(int op);

/*
Parses the $LogFile stream input
Writes output to designated streams
*/
void parseLog(std::map<unsigned int, file*> records, sqlite3* db, std::istream& input = std::cin, std::ostream& output = std::cout);
//void parseLog(std::map<unsigned int, file*>& records, sqlite3* db, std::istream& input);

/*
Used for $LogFile event analysis
returns whether the transaction run represented by redo1, undo1 "matches" redo2, undo2
Here matching means that redo2 is a subsequence of redo1, and undo2 is a subsequence of undo1
if interchange is set, then it is considered that 0xc == 0xe and 0xd == 0xf
*/
bool transactionRunMatch(const std::vector<int>& redo1, const std::vector<int>& undo1, const std::vector<int>& redo2, const std::vector<int>& undo2, bool interchange = true);

class Log_Record {
public:
  unsigned long long cur_lsn, prev_lsn, undo_lsn;
  unsigned int client_id, record_type, flags, redo_op, undo_op, redo_offset, redo_length, undo_offset, undo_length;
  unsigned int target_attr, lcns_to_follow, record_offset, attribute_offset, mft_cluster_index, target_vcn, target_lcn;
  unsigned int client_data_length;
  char* data;

  int init(char* buffer);
  void clearFields();
  void insert(sqlite3*db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records);
  std::string toString(std::map<unsigned int, file*>& records);

};

class Log_Data {
public:
  unsigned long long mft_record_no, par_mft_record, prev_par_mft_record, timestamp;
  unsigned long long lsn;
  unsigned int name_len;
  std::string name, prev_name;
  std::vector<int> redo_ops, undo_ops;

  void clearFields();
  void processLogRecord(Log_Record& rec, std::map<unsigned int, file*>& records);
  std::string toCreateString(std::map<unsigned int, file*>& records);
  std::string toDeleteString(std::map<unsigned int, file*>& records);
  std::string toRenameString(std::map<unsigned int, file*>& records);
  std::string toMoveString(std::map<unsigned int, file*>& records);
  void insertEvent(unsigned int type, sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records);
  bool isCreateEvent();
  bool isDeleteEvent();
  bool isRenameEvent();
  bool isMoveEvent();
  bool isTransactionOver();


  static std::vector<int> create_redo, create_undo, delete_redo, delete_undo;
  static std::vector<int> rename_redo, rename_undo, write_redo, write_undo;
  static void initTransactionVectors();
};

#endif
