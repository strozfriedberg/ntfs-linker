/*
 * ntfs-linker
 * Copyright 2015 Stroz Friedberg, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License is available at
 * <http://www.gnu.org/licenses/>.
 *
 * You can contact Stroz Friedberg by electronic and paper mail as follows:
 *
 * Stroz Friedberg, LLC
 * 32 Avenue of the Americas
 * 4th Floor
 * New York, NY, 10013
 * info@strozfriedberg.com
 */

#pragma once

#include "file.h"
#include "sqlite_util.h"

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
  SOURCE_USN = 0,
  SOURCE_LOG = 1,
  SOURCE_EMBEDDED_USN = 2,
};

enum EventTypes: unsigned int {
  TYPE_CREATE = 0,
  TYPE_DELETE = 1,
  TYPE_RENAME = 2,
  TYPE_MOVE = 3,
};

std::string toString(EventSources e);

std::string toString(EventTypes e);

std::ostream& operator<<(std::ostream& out, EventTypes e);

std::ostream& operator<<(std::ostream& out, EventSources e);

std::string pluralize(std::string name, int n);

int doFixup(char* buffer, unsigned int len, unsigned int sectorSize=512);

int ceilingDivide(int n, int m);
