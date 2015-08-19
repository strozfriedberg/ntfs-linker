#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <string.h>

#include "file.h"
#ifndef help_h
#define help_h

static const unsigned long BUILD_NUMBER = __BUILD_NUMBER;

std::string byte_to_str(char* bytes, int size);

unsigned long long hex_to_long(char* arr, int size);

long long filetime_to_unixtime(long long t);

std::wstring string_to_wstring(const std::string &str);

std::string filetime_to_iso_8601(unsigned long long t);

std::string mbcatos(char* arr, unsigned long long len);

std::string getFlagMeaning(int flags);

time_t getEpochDifference();

int max(int a, int b);

void mem_dump(char* buffer, int length, std::ostream& output = std::cout);

std::string getFullPath(std::map<unsigned int, File*>& records, unsigned int recordNo);

char* getCmdOption(char** begin, char** end, const std::string& option);

bool cmdOptionExists(char** begin, char** end, const std::string& option);

char getPathSeparator();

bool isUnix();

void prep_ofstream(std::ofstream& out, const char* name, bool overwrite);

enum event_sources: unsigned int {
  USN = 0,
  LOG = 1
};

enum event_types: unsigned int {
  CREATE = 0,
  DELETE = 1,
  RENAME = 2,
  MOVE = 3
};

#endif
