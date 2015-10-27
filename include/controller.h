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
#include "sqlite_util.h"

#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <memory>

namespace fs = boost::filesystem;

struct Options {
  Options() : overwrite(false), extra(false) {}
  fs::path input;
  fs::path output;
  bool overwrite;
  bool extra;
  std::vector<std::string> imgSegs;
};

struct VolumeIO;

struct SnapshotIO {
  SnapshotIO(Options& opts, VolumeIO* parent);

  VolumeIO* Parent;
  std::ifstream IMft, IUsnJrnl, ILogFile;
  std::ofstream OUsnJrnl, OLogFile;
  std::string Name;
};
typedef std::unique_ptr<SnapshotIO> SnapshotIOPtr;

struct ImageIO;

struct VolumeIO {
  VolumeIO(Options& opts, ImageIO* parent);

  ImageIO* Parent;
  std::vector<SnapshotIOPtr> Snapshots;
  std::ofstream Events;
  unsigned int Count;
  std::string Name;
};
typedef std::unique_ptr<VolumeIO> VolumeIOPtr;

struct ImageIO {
  ImageIO(Options& opts);
  std::vector<VolumeIOPtr> Volumes;
  SQLiteHelper SqliteHelper;
};

void run(Options& opts);
