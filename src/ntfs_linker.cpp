#include "aggregate.h"
#include "file.h"
#include "log.h"
#include "mft.h"
#include "usn.h"
#include "vss.h"
#include "walkers.h"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/scoped_array.hpp>
#include <sqlite3.h>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

struct Options {
  fs::path input;
  fs::path output;
  bool overwrite;
};

class IOContainer {
public:
  IOContainer(Options& opts) {
    IMft.open((opts.input / fs::path("$MFT")).string(), std::ios::binary);
    IUsnJrnl.open((opts.input / fs::path("$UsnJrnl")).string(), std::ios::binary);
    ILogFile.open((opts.input / fs::path("$LogFile")).string(), std::ios::binary);


    if(!IMft) {
      std::cerr << "$MFT File not found." << std::endl;
      exit(0);
    }
    if(!IUsnJrnl) {
      IUsnJrnl.open((opts.input / fs::path("$J")).string(), std::ios::binary);
      if(!IUsnJrnl) {
        std::cerr << "$UsnJrnl File not found." << std::endl;
        exit(0);
      }
    }
    if(!ILogFile) {
      std::cerr << "$LogFile File not found: " << std::endl;
      exit(0);
    }

    prep_ofstream(OUsnJrnl, (opts.output / fs::path("usnjrnl.txt")).string(), opts.overwrite);
    prep_ofstream(OLogFile, (opts.output / fs::path("logfile.txt")).string(), opts.overwrite);
  }

  std::ifstream IMft, IUsnJrnl, ILogFile;
  std::ofstream OUsnJrnl, OLogFile;
};

typedef std::unique_ptr<IOContainer> IOContainerPtr;

struct IOBundle {
  std::vector<IOContainerPtr> Containers;
  SQLiteHelper SqliteHelper;
  std::ofstream Events;
};

void printHelp(const po::options_description& desc) {
  std::cout << "ntfs-linker, Copyright (c) Stroz Friedberg, LLC" << std::endl;
  std::cout << "Version " << VERSION << std::endl;
  std::cout << desc << std::endl;
  std::cout << "Note: this program will also look for files named $J when looking for $UsnJrnl file." << std::endl;
}


void setupIO(Options& opts, IOBundle& ioBundle, std::vector<std::string>& imgSegs) {
  fs::create_directories(opts.output);
  if (imgSegs.size()) {
    std::cout << "Copying files out of image..." << std::endl;
    boost::scoped_array<const char*> segments(new const char*[imgSegs.size()]);
    for (unsigned int i = 0; i < imgSegs.size(); ++i) {
      segments[i] = imgSegs[i].c_str();
    }
    VolumeWalker walker(opts.input);
    walker.openImageUtf8(imgSegs.size(), segments.get(), TSK_IMG_TYPE_DETECT, 0);
    walker.findFilesInImg();

    std::cout << "Done copying" << std::endl;

  }

  std::vector<fs::path> children;
  std::copy(fs::directory_iterator(opts.input), fs::directory_iterator(), std::back_inserter(children));
  std::sort(children.begin(), children.end());
  bool containsDirectories = false;
  for (auto& child: children) {
    if (fs::is_directory(child)) {
      containsDirectories = true;
      Options vssOpts = opts;
      vssOpts.input /= child.filename();
      vssOpts.output /= child.filename();
      ioBundle.Containers.push_back(IOContainerPtr(new IOContainer(vssOpts)));
    }
  }

  if (!containsDirectories) {
    ioBundle.Containers.push_back(IOContainerPtr(new IOContainer(opts)));
  }

  prep_ofstream(ioBundle.Events, (opts.output / fs::path("events.txt")).string() , opts.overwrite);
  std::cout << "Setting up DB Connection..." << std::endl;
  std::string dbName = (opts.output / fs::path("ntfs.db")).string();
  ioBundle.SqliteHelper.init(dbName, opts.overwrite);
}

int processStep(IOContainer& container, SQLiteHelper& sqliteHelper, unsigned int snapshot) {
  //Set up db connection

  std::vector<File> records;
  std::cout << "Creating MFT Map..." << std::endl;
  parseMFT(records, container.IMft);

  std::cout << "Parsing USNJrnl..." << std::endl;
  parseUSN(records, sqliteHelper, container.IUsnJrnl, container.OUsnJrnl, snapshot);
  std::cout << "Parsing LogFile..." << std::endl;
  parseLog(records, sqliteHelper, container.ILogFile, container.OLogFile, snapshot);
  sqliteHelper.commit();
  return 0;
}

int processFinalize(IOBundle& bundle, IOContainer& container, unsigned int snapshot) {
  std::vector<File> records;
  std::cout << "Creating MFT Map..." << std::endl;
  parseMFT(records, container.IMft);

  std::cout << "Generating unified events output..." << std::endl;
  outputEvents(records, bundle.SqliteHelper, bundle.Events, snapshot);

  return 0;
}

int main(int argc, char** argv) {
  Options opts;

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "display help and exit")
    ("output", po::value<std::string>(), "directory in which to dump output files")
    ("input", po::value<std::string>(), "If no image specified, location of directory containing input files: $MFT, $UsnJrnl, $LogFile. Otherwise, root directory in which to dump files extracted from image.")
    ("image", po::value<std::vector<std::string>>(), "Path to image")
    ("version", "display version number and exit")
    ("overwrite", "overwrite files in the output directory. Default: append");

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    po::notify(vm);
    std::vector<std::string> imgSegs;


    if (vm.count("overwrite"))
      opts.overwrite = true;

    if (vm.count("help"))
      printHelp(desc);
    else if (vm.count("version"))
        std::cout << "ntfs_linker version: " << VERSION << std::endl;
    else if (vm.count("input") && vm.count("output")) {
      // Run
      opts.input = fs::path(vm["input"].as<std::string>());
      opts.output = fs::path(vm["output"].as<std::string>());
      if (vm.count("image")) {
        imgSegs = vm["image"].as<std::vector<std::string>>();
      }

      IOBundle bundle;
      setupIO(opts, bundle, imgSegs);

      std::vector<IOContainerPtr>::iterator it;
      std::vector<IOContainerPtr>::reverse_iterator rIt;
      unsigned int snapshot;
      for (snapshot = 0, it = bundle.Containers.begin(); it != bundle.Containers.end(); ++it, ++snapshot) {
        processStep(**it, bundle.SqliteHelper, snapshot);
      }
      for (rIt = bundle.Containers.rbegin(); rIt != bundle.Containers.rend(); ++rIt, --snapshot) {
        processFinalize(bundle, **rIt, snapshot);
      }
      bundle.SqliteHelper.close();
      std::cout << "Process complete." << std::endl;
    }
    else {
      std::cerr << "Error: did not understand arguments" << std::endl;
      printHelp(desc);
    }
  }
  catch (std::exception& err) {
    std::cerr << "Error: " << err.what() << std::endl << std::endl;
    printHelp(desc);
    return 1;
  }
  return 0;
}
