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
