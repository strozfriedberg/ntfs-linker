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

void printHelp(const po::options_description& desc) {
  std::cout << "ntfs-linker, Copyright (c) Stroz Friedberg, LLC" << std::endl;
  std::cout << "Version " << VERSION << std::endl;
  std::cout << desc << std::endl;
  std::cout << "Note: this program will also look for files named $J when looking for $UsnJrnl file." << std::endl;
}

void setupIO(Options& opts, IContainer& iContainer, OContainer& oContainer, std::vector<std::string>& imgSegs) {
  if (!opts.isImage) {
    fs::path inDir(opts.input);

    iContainer.i_mft.open((inDir / fs::path("$MFT")).string(), std::ios::binary);
    iContainer.i_usnjrnl.open((inDir / fs::path("$UsnJrnl")).string(), std::ios::binary);
    iContainer.i_logfile.open((inDir / fs::path("$LogFile")).string(), std::ios::binary);


    if(!iContainer.i_mft) {
      std::cerr << "$MFT File not found." << std::endl;
      exit(0);
    }
    if(!iContainer.i_usnjrnl) {
      iContainer.i_usnjrnl.open((inDir / fs::path("$J")).string(), std::ios::binary);
      if(!iContainer.i_usnjrnl) {
        std::cerr << "$UsnJrnl File not found." << std::endl;
        exit(0);
      }
    }
    if(!iContainer.i_logfile) {
      std::cerr << "$LogFile File not found: " << std::endl;
      exit(0);
    }
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
  fs::path outDir(opts.outputDir);
  fs::create_directories(outDir);

  prep_ofstream(oContainer.o_usnjrnl, (outDir / fs::path("usnjrnl.txt")).string(), opts.overwrite);
  prep_ofstream(oContainer.o_logfile, (outDir / fs::path("logfile.txt")).string(), opts.overwrite);
  prep_ofstream(oContainer.o_events , (outDir / fs::path("events.txt")).string() , opts.overwrite);

  std::cout << "Setting up DB Connection..." << std::endl;
  std::string dbName = (outDir / fs::path("ntfs.db")).string();
  oContainer.sqliteHelper.init(dbName, opts.overwrite);
}

int process(IContainer& iContainer, OContainer& oContainer) {
  //Set up db connection

  std::vector<File> records;
  std::cout << "Creating MFT Map..." << std::endl;
  parseMFT(records, oContainer.sqliteHelper, iContainer.i_mft, oContainer.o_mft, true);

  std::cout << "Parsing USNJrnl..." << std::endl;
  parseUSN(records, oContainer.sqliteHelper, iContainer.i_usnjrnl, oContainer.o_usnjrnl);
  std::cout << "Parsing LogFile..." << std::endl;
  parseLog(records, oContainer.sqliteHelper, iContainer.i_logfile, oContainer.o_logfile);
  oContainer.sqliteHelper.commit();

  std::cout << "Generating unified events output..." << std::endl;
  outputEvents(records, oContainer.sqliteHelper, oContainer.o_events);

  oContainer.sqliteHelper.close();
  std::cout << "Process complete." << std::endl;
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

      IContainer iContainer;
      OContainer oContainer;
      setupIO(opts, iContainer, oContainer, imgSegs);
      return process(iContainer, oContainer);
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
