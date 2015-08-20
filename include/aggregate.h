#include "sqlite3.h"

#include <fstream>
#include <map>

class Event {
public:
  Event();
  void init(sqlite3_stmt* stmt);
  void write(std::ostream& out, std::vector<File>& records);
  void update_records(std::vector<File>& records);

  int record, par_record, previous_par_record, usn_lsn, type, source;
  std::string timestamp, file_name, previous_file_name;
};

void outputEvents(std::vector<File>& records, sqlite3* db, std::ofstream& o_events);
