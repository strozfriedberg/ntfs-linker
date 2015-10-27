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

SnapshotIO::SnapshotIO(Options& opts, VolumeIO* parent) : Parent(parent), Name(opts.input.string()) {
  IMft.open((opts.input / fs::path("$MFT")).string(), std::ios::binary);
  IUsnJrnl.open((opts.input / fs::path("$UsnJrnl")).string(), std::ios::binary);
  ILogFile.open((opts.input / fs::path("$LogFile")).string(), std::ios::binary);


  if(!IMft) {
    std::cerr << "$MFT File not found in directory: " << opts.input.string() << ". Exiting." << std::endl;
    exit(0);
  }
  if(!IUsnJrnl) {
    IUsnJrnl.open((opts.input / fs::path("$J")).string(), std::ios::binary);
    if(!IUsnJrnl) {
      std::cerr << "$UsnJrnl/$J File not found in directory: " << opts.input.string() << ". Exiting." << std::endl;
      exit(0);
    }
  }
  if(!ILogFile) {
    std::cerr << "$LogFile File not found in directory: " << opts.input.string() << ". Exiting." << std::endl;
    exit(0);
  }

  fs::create_directories(opts.output);
  prep_ofstream(OUsnJrnl, (opts.output / fs::path("usnjrnl.txt")).string(), opts.overwrite);
  prep_ofstream(OLogFile, (opts.output / fs::path("logfile.txt")).string(), opts.overwrite);
}

VolumeIO::VolumeIO(Options& opts, ImageIO* parent) : Parent(parent), Count(0), Name(opts.input.string())  {
  std::vector<fs::path> snapshots;
  std::copy(fs::directory_iterator(opts.input), fs::directory_iterator(), std::back_inserter(snapshots));
  std::sort(snapshots.begin(), snapshots.end());
  bool containsDirectories = false;
  for (auto& snapshot: snapshots) {
    if (fs::is_directory(snapshot)) {
      containsDirectories = true;
      Options snapshotOpts = opts;
      snapshotOpts.input  /= snapshot.filename();
      snapshotOpts.output /= snapshot.filename();
      Snapshots.push_back(SnapshotIOPtr(new SnapshotIO(snapshotOpts, this)));
    }
  }

  if (!containsDirectories) {
    Snapshots.push_back(SnapshotIOPtr(new SnapshotIO(opts, this)));
  }
  prep_ofstream(Events, (opts.output / fs::path("events.txt")).string(), opts.overwrite);
}

ImageIO::ImageIO(Options& opts) {
  std::vector<fs::path> volumes;
  std::copy(fs::directory_iterator(opts.input), fs::directory_iterator(), std::back_inserter(volumes));
  std::sort(volumes.begin(), volumes.end());
  bool containsDirectories = false;
  for (auto& volume: volumes) {
    if (fs::is_directory(volume)) {
      containsDirectories = true;
      Options volumeOpts = opts;
      volumeOpts.input /= volume.filename();
      volumeOpts.output /= volume.filename();
      Volumes.push_back(VolumeIOPtr(new VolumeIO(volumeOpts, this)));
    }
  }

  if (!containsDirectories) {
    Volumes.push_back(VolumeIOPtr(new VolumeIO(opts, this)));
  }

  std::cout << "Setting up DB Connection..." << std::endl;
  std::string dbName = (opts.output / fs::path("ntfs.db")).string();
  SqliteHelper.init(dbName, opts.overwrite);
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

    std::cout << "Done copying" << std::endl;
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
  std::cout << "Process complete." << std::endl;

}
