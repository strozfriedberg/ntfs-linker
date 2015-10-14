#include "aggregate.h"
#include "file.h"
#include "log.h"
#include "mft.h"
#include "usn.h"
#include "vss_handler.h"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/scoped_array.hpp>
#include <sqlite3.h>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

struct Options {
  std::string input;
  std::string outputDir;
  bool overwrite;
  bool isImage;

};

class IOContainer {
public:
  IOContainer(Options& opts) {
    fs::path inDir(opts.input);

    IMft.open((inDir / fs::path("$MFT")).string(), std::ios::binary);
    IUsnJrnl.open((inDir / fs::path("$UsnJrnl")).string(), std::ios::binary);
    ILogFile.open((inDir / fs::path("$LogFile")).string(), std::ios::binary);


    if(!IMft) {
      std::cerr << "$MFT File not found." << std::endl;
      exit(0);
    }
    if(!IUsnJrnl) {
      IUsnJrnl.open((inDir / fs::path("$J")).string(), std::ios::binary);
      if(!IUsnJrnl) {
        std::cerr << "$UsnJrnl File not found." << std::endl;
        exit(0);
      }
    }
    if(!ILogFile) {
      std::cerr << "$LogFile File not found: " << std::endl;
      exit(0);
    }
    fs::path outDir(opts.outputDir);
    fs::create_directories(outDir);

    prep_ofstream(OUsnJrnl, (outDir / fs::path("usnjrnl.txt")).string(), opts.overwrite);
    prep_ofstream(OLogFile, (outDir / fs::path("logfile.txt")).string(), opts.overwrite);
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
  fs::path outDir(opts.outputDir);
  prep_ofstream(ioBundle.Events, (outDir / fs::path("events.txt")).string() , opts.overwrite);
  if (!opts.isImage) {
    ioBundle.Containers.push_back(IOContainerPtr(new IOContainer(opts)));

    std::cout << "Setting up DB Connection..." << std::endl;
    std::string dbName = (outDir / fs::path("ntfs.db")).string();
    ioBundle.SqliteHelper.init(dbName, opts.overwrite);
  }
  else {
    std::cout << "Copying files out of image..." << std::endl;
    boost::scoped_array<const char*> segments(new const char*[imgSegs.size()]);
    for (unsigned int i = 0; i < imgSegs.size(); ++i) {
      segments[i] = imgSegs[i].c_str();
    }
    VolumeWalker walker;
    walker.openImageUtf8(imgSegs.size(), segments.get(), TSK_IMG_TYPE_DETECT, 0);
    walker.findFilesInImg();

    std::cout << "Done copying" << std::endl;

  }
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
  po::positional_options_description posOpts;
  posOpts.add("output-dir", 1);
  posOpts.add("input", 1);
  desc.add_options()
    ("help", "display help and exit")
    ("output-dir", po::value<std::string>(&opts.outputDir), "directory in which to dump output files")
    ("input", po::value<std::vector<std::string>>(), "location of directory containing input files: $MFT, $UsnJrnl, $LogFile, OR ev-files")
    ("is-image", "specifies that input is actually a list of image segments")
    ("version", "display version number and exit")
    ("overwrite", "overwrite files in the output directory. Default: append");

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).options(desc).positional(posOpts).run(), vm);
    po::notify(vm);
    std::vector<std::string> imgSegs;


    if (vm.count("overwrite"))
      opts.overwrite = true;

    if (vm.count("help"))
      printHelp(desc);
    else if (vm.count("version"))
        std::cout << "ntfs_linker version: " << VERSION << std::endl;
    else if (vm.count("input") && vm.count("output-dir")) {
      // Run
      if (vm.count("is-image")) {
        opts.isImage = true;
        imgSegs = vm["input"].as<std::vector<std::string>>();
      }
      else {
        opts.input = imgSegs[0];
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
