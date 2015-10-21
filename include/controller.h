#pragma once
#include "sqlite_util.h"

#include <boost/filesystem.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <memory>

namespace fs = boost::filesystem;

struct Options {
  fs::path input;
  fs::path output;
  bool overwrite;
  bool extra;
  std::vector<std::string> imgSegs;
};

class IOContainer {
  public:
    IOContainer(Options& opts);

    std::ifstream IMft, IUsnJrnl, ILogFile;
    std::ofstream OUsnJrnl, OLogFile;
    fs::path Dir;
};

typedef std::unique_ptr<IOContainer> IOContainerPtr;

struct IOBundle {
  std::vector<IOContainerPtr> Containers;
  SQLiteHelper SqliteHelper;
  std::ofstream Events;
};

void run(Options& opts);
