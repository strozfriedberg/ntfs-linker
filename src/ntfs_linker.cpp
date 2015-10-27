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

#include "controller.h"
#include "util.h"

#include <boost/program_options.hpp>

namespace po = boost::program_options;

void printHelp(const po::options_description& desc) {
  std::cout << "ntfs-linker, Copyright (c) Stroz Friedberg, LLC" << std::endl;
  std::cout << "Version " << VERSION << std::endl;
  std::cout << desc << std::endl;
  std::cout << "Note: this program will also look for files named $J when looking for $UsnJrnl file." << std::endl;
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
    ("overwrite", "overwrite files in the output directory. Default: append")
    ("extra", "Outputs supplemental lower-level parsed data from $UsnJrnl and $LogFile");

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    po::notify(vm);

    opts.overwrite = vm.count("overwrite");
    opts.extra = vm.count("extra");

    if (vm.count("help")) {
      printHelp(desc);
    }
    else if (vm.count("version")) {
        std::cout << "ntfs_linker version: " << VERSION << std::endl;
    }
    else if (vm.count("input") && vm.count("output")) {
      // Run
      opts.input = fs::path(vm["input"].as<std::string>());
      opts.output = fs::path(vm["output"].as<std::string>());
      if (vm.count("image")) {
        opts.imgSegs = vm["image"].as<std::vector<std::string>>();
      }
      run(opts);
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
