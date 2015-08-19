#include "sqlite3.h"

#include <fstream>
#include <map>

void outputEvents(std::map<unsigned int, File*> records, sqlite3* db, std::ofstream& o_events);
