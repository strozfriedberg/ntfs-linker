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

#include "aggregate.h"
#include "controller.h"
#include "file.h"
#include "log.h"
#include "mft.h"
#include "usn.h"
#include "vss.h"
#include "walkers.h"

#include <boost/scoped_array.hpp>
#include <sstream>

SnapshotIO::SnapshotIO(Options& opts, VolumeIO* parent) : Parent(parent), Name(opts.input.string()), Good(false) {
  IMft.open((opts.input / fs::path("$MFT")).string(), std::ios::binary);
  IUsnJrnl.open((opts.input / fs::path("$UsnJrnl")).string(), std::ios::binary);
  ILogFile.open((opts.input / fs::path("$LogFile")).string(), std::ios::binary);

  if(!IMft) {
    std::cerr << "$MFT File not found in directory: " << opts.input.string() << std::endl;
    return;
  }
  if(!IUsnJrnl) {
    IUsnJrnl.open((opts.input / fs::path("$J")).string(), std::ios::binary);
    if(!IUsnJrnl) {
      std::cerr << "$UsnJrnl/$J File not found in directory: " << opts.input.string() << std::endl;
      return;
    }
  }
  if(!ILogFile) {
    std::cerr << "$LogFile File not found in directory: " << opts.input.string() << std::endl;
    return;
  }

  fs::create_directories(opts.output);
  prep_ofstream(OUsnJrnl, (opts.output / fs::path("usnjrnl.txt")).string(), opts.overwrite);
  prep_ofstream(OLogFile, (opts.output / fs::path("logfile.txt")).string(), opts.overwrite);
  Good = true;
}

VolumeIO::VolumeIO(Options& opts, ImageIO* parent) : Parent(parent), Count(0), Name(opts.input.string()), Good(false)  {
  std::vector<fs::path> snapshots;
  std::copy(fs::directory_iterator(opts.input), fs::directory_iterator(), std::back_inserter(snapshots));
  std::sort(snapshots.begin(), snapshots.end());
  for (auto& snapshot: snapshots) {
    if (fs::is_directory(snapshot)) {
      Options snapshotOpts = opts;
      snapshotOpts.input  /= snapshot.filename();
      snapshotOpts.output /= snapshot.filename();
      auto snapshot(std::make_shared<SnapshotIO>(snapshotOpts, this));
      if (snapshot->Good) {
        Snapshots.push_back(snapshot);
        Good = true;
      }
    }
  }

  if (!Good) {
    auto snapshot(std::make_shared<SnapshotIO>(opts, this));
    if (snapshot->Good) {
      Snapshots.push_back(snapshot);
      Good = true;
    }
    else {
      std::cerr << "Unable to process folder: " << opts.input << " as a volume folder. Neither this folder nor any subdirectory contain all of $MFT, $J, $LogFile" << std::endl;
      return;
    }
  }
  prep_ofstream(Events, (opts.output / fs::path("events.txt")).string(), opts.overwrite);
}

ImageIO::ImageIO(Options& opts) : Good(false) {
  std::vector<fs::path> volumes;
  std::copy(fs::directory_iterator(opts.input), fs::directory_iterator(), std::back_inserter(volumes));
  std::sort(volumes.begin(), volumes.end());
  for (auto& volume: volumes) {
    if (fs::is_directory(volume)) {
      Options volumeOpts = opts;
      volumeOpts.input /= volume.filename();
      volumeOpts.output /= volume.filename();
      auto volume(std::make_shared<VolumeIO>(volumeOpts, this));
      if (volume->Good) {
        Good = true;
        Volumes.push_back(volume);
      }
    }
  }

  if (!Good) {
    auto volume(std::make_shared<VolumeIO>(opts, this));
    if (volume->Good) {
      Good = true;
      Volumes.push_back(volume);
    }
    else {
      std::cerr << "Unable to process folder: " << opts.input << " as an image folder. Neither this folder, nor any of its subdirectories could be processed as a volume." << std::endl;
      return;
    }
  }

  std::cout << "Setting up DB Connection..." << std::endl;
  std::string dbName = (opts.output / fs::path("ntfs.db")).string();
  SqliteHelper.init(dbName, opts.overwrite);
}

std::string ImageIO::getSummary() {
  std::ostringstream ss;
  int sum = 0;
  for (auto const& volume: Volumes) {
    ss << "Volume " << volume->Name << ": processed " << volume->Snapshots.size() << " snapshot"
       << (volume->Snapshots.size() != 1? "s.\n" : ".\n");
    sum += volume->Snapshots.size();
  }
  ss << "Total: processed " << Volumes.size() << " volume" << (Volumes.size() != 1? "s" : "")
     << ", " << sum << " snapshot" << (sum != 1? "s." : ".");
  return ss.str();
}

void copyAllFiles(Options& opts) {
  if (opts.imgSegs.size()) {
    std::cout << "Copying files out of image..." << std::endl;
    boost::scoped_array<const char*> segments(new const char*[opts.imgSegs.size()]);
    for (unsigned int i = 0; i < opts.imgSegs.size(); ++i) {
      segments[i] = opts.imgSegs[i].c_str();
    }
    VolumeWalker walker(opts.input);
    walker.openImageUtf8(opts.imgSegs.size(), segments.get(), TSK_IMG_TYPE_DETECT, 0);
    walker.findFilesInImg();

    if (walker.DidItWork) {
      std::cout << "Copying completed successfully. Summary: " << std::endl;
      std::cout << walker.getSummary() << std::endl;
    }
    else {
      std::cerr << "Error: unable to copy out files. Terminating." << std::endl;
      exit(1);
    }

  }
}

int processStep(SnapshotIO& snapshotIO, bool extra) {
  //Set up db connection
  std::vector<File> records;
  SQLiteHelper& sqliteHelper = snapshotIO.Parent->Parent->SqliteHelper;
  std::cout << "Parsing $MFT" << std::endl;
  parseMFT(records, snapshotIO.IMft);

  std::cout << "Parsing $UsnJrnl..." << std::endl;
  parseUSN(records, sqliteHelper, snapshotIO.IUsnJrnl, snapshotIO.OUsnJrnl, VersionInfo(snapshotIO.Name, snapshotIO.Parent->Name), extra);
  std::cout << "Parsing $LogFile..." << std::endl;
  parseLog(records, sqliteHelper, snapshotIO.ILogFile, snapshotIO.OLogFile, VersionInfo(snapshotIO.Name, snapshotIO.Parent->Name), extra);
  return 0;
}

int processFinalize(SnapshotIO& snapshotIO) {
  std::vector<File> records;
  SQLiteHelper& sqliteHelper = snapshotIO.Parent->Parent->SqliteHelper;
  VolumeIO& volumeIO = *snapshotIO.Parent;

  parseMFT(records, snapshotIO.IMft);

  outputEvents(records, sqliteHelper, volumeIO, VersionInfo(snapshotIO.Name, volumeIO.Name));

  return 0;
}

void run(Options& opts) {
  copyAllFiles(opts);

  ImageIO imageIO(opts);
  if (!imageIO.Good) {
    std::cerr << "Unable to process input folder structure. Terminating." << std::endl;
    exit(1);
  }

  for (auto& volumeIO: imageIO.Volumes) {
    std::cout << "Finding events on Volume: " << volumeIO->Name << std::endl;

    imageIO.SqliteHelper.beginTransaction();
    for (auto& snapshotIO: volumeIO->Snapshots) {
      std::cout << "Parsing input files for snapshot: " << snapshotIO->Name << std::endl;
      processStep(*snapshotIO, opts.extra);
    }
    imageIO.SqliteHelper.endTransaction();
    imageIO.SqliteHelper.beginTransaction();

    std::cout << "Generating unified events output..." << std::endl;
    volumeIO->Events << Event::getColumnHeaders();
    std::vector<SnapshotIOPtr>::reverse_iterator rIt;
    for (rIt = volumeIO->Snapshots.rbegin(); rIt != volumeIO->Snapshots.rend(); ++rIt) {
      std::cout << "Processing events from snapshot: " << (*rIt)->Name << std::endl;
      processFinalize(**rIt);
    }

    imageIO.SqliteHelper.endTransaction();
  }
  imageIO.SqliteHelper.close();
  std::cout << imageIO.getSummary() << std::endl;
  std::cout << "Process complete." << std::endl;

}
