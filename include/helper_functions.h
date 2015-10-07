#pragma once

#include "file.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <tsk/libtsk.h>

static const std::string VERSION = __VERSION;

uint64_t hex_to_long(const char* arr, int size);

int64_t filetime_to_unixtime(int64_t t);

std::string filetime_to_iso_8601(uint64_t t);

std::string mbcatos(const char* arr, uint64_t len);

std::string getFullPath(const std::vector<File>& records, unsigned int recordNo);

void prep_ofstream(std::ofstream& out, const std::string& name, bool overwrite);

enum EventSources: unsigned int {
  USN = 0,
  LOG = 1,
  EMBEDDED_USN = 2,
};

enum EventTypes: unsigned int {
  CREATE = 0,
  DELETE = 1,
  RENAME = 2,
  MOVE = 3,
};

std::ostream& operator<<(std::ostream& out, EventTypes e);

std::ostream& operator<<(std::ostream& out, EventSources e);

bool compareNames(std::string a, std::string b);

int doFixup(char* buffer, unsigned int len, unsigned int sectorSize=512);
