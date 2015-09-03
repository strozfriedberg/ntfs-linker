#include "helper_functions.h"
#include "file.h"
#include "log.h"
#include "mft.h"
#include "usn.h"
#include "aggregate.h"

#include <sqlite3.h>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

namespace po = boost::program_options;
namespace fs = boost::filesystem;

struct Options {
  std::string inputDir;
  std::string outputDir;
  bool overwrite;

};

void printHelp(const po::options_description& desc) {
  std::cout << "ntfs-linker, Copyright (c) Stroz Friedberg, LLC" << std::endl;
  std::cout << "Version " << VERSION << std::endl;
  std::cout << desc << std::endl;
  std::cout << "Note: this program will also look for files named $J when looking for $UsnJrnl file." << std::endl;
}

int process(Options& opts) {
  std::ifstream i_mft, i_usnjrnl, i_logfile;
  std::ofstream o_mft, o_usnjrnl, o_logfile, o_events;

  fs::path inDir(opts.inputDir);
  i_mft.open((inDir / fs::path("$MFT")).string(), std::ios::binary);
  i_usnjrnl.open((inDir / fs::path("$UsnJrnl")).string(), std::ios::binary);
  i_logfile.open((inDir / fs::path("$LogFile")).string(), std::ios::binary);


  if(!i_mft) {
    std::cerr << "$MFT File not found." << std::endl;
    exit(0);
  }
  if(!i_usnjrnl) {
    i_usnjrnl.open((inDir / fs::path("$J")).string(), std::ios::binary);
    if(!i_usnjrnl) {
      std::cerr << "$UsnJrnl File not found." << std::endl;
      exit(0);
    }
  }
  if(!i_logfile) {
    std::cerr << "$LogFile File not found: " << std::endl;
    exit(0);
  }

  fs::path outDir(opts.outputDir);
  fs::create_directories(outDir);

  prep_ofstream(o_usnjrnl, (outDir / fs::path("usnjrnl.txt")).string(), opts.overwrite);
  prep_ofstream(o_logfile, (outDir / fs::path("logfile.txt")).string(), opts.overwrite);
  prep_ofstream(o_events , (outDir / fs::path("events.txt")).string() , opts.overwrite);

  //Set up db connection
  std::cout << "Setting up DB Connection..." << std::endl;
  std::string dbName = (outDir / fs::path("ntfs.db")).string();
  SQLiteHelper sqliteHelper(dbName, opts.overwrite);

  std::vector<File> records;
  std::cout << "Creating MFT Map..." << std::endl;
  parseMFT(records, sqliteHelper, i_mft, o_mft, true);

  std::cout << "Parsing USNJrnl..." << std::endl;
  parseUSN(records, sqliteHelper, i_usnjrnl, o_usnjrnl);
  std::cout << "Parsing LogFile..." << std::endl;
  parseLog(records, sqliteHelper, i_logfile, o_logfile);
  sqliteHelper.commit();

  std::cout << "Generating unified events output..." << std::endl;
  outputEvents(records, sqliteHelper, o_events);

  sqliteHelper.close();
  std::cout << "Process complete." << std::endl;
  return 0;
}

int main(int argc, char** argv) {
  Options opts;

  po::options_description desc("Allowed options");
  po::positional_options_description posOpts;
  posOpts.add("input-dir", 1);
  posOpts.add("output-dir", 1);
  desc.add_options()
    ("help", "display help and exit")
    ("input-dir", po::value<std::string>(&opts.inputDir), "location of directory containing input files: $MFT, $UsnJrnl, $LogFile")
    ("output-dir", po::value<std::string>(&opts.outputDir), "directory in which to dump output files")
    ("version", "display version number and exit")
    ("overwrite", "overwrite files in the output directory. Default: append");

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).options(desc).positional(posOpts).run(), vm);
    po::notify(vm);

    if (vm.count("overwrite"))
      opts.overwrite = true;

    if (vm.count("help"))
      printHelp(desc);
    else if (vm.count("version"))
        std::cout << "ntfs_linker version: " << VERSION << std::endl;
    else if (vm.count("input-dir") && vm.count("output-dir")) {
      // Run
      return process(opts);
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
