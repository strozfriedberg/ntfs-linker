#include "aggregate.h"
#include "controller.h"
#include "file.h"
#include "log.h"
#include "mft.h"
#include "usn.h"
#include "vss.h"
#include "walkers.h"

#include <boost/scoped_array.hpp>

IOContainer::IOContainer(Options& opts) : Dir(opts.input) {
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

  fs::create_directories(opts.output);
  prep_ofstream(OUsnJrnl, (opts.output / fs::path("usnjrnl.txt")).string(), opts.overwrite);
  prep_ofstream(OLogFile, (opts.output / fs::path("logfile.txt")).string(), opts.overwrite);
}

void setupIO(Options& opts, IOBundle& ioBundle) {
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

int processStep(IOContainer& container, SQLiteHelper& sqliteHelper, std::string snapshot, bool extra) {
  //Set up db connection
  std::vector<File> records;
  std::cout << "Creating MFT Map..." << std::endl;
  parseMFT(records, container.IMft);

  std::cout << "Parsing USNJrnl..." << std::endl;
  parseUSN(records, sqliteHelper, container.IUsnJrnl, container.OUsnJrnl, snapshot, extra);
  std::cout << "Parsing LogFile..." << std::endl;
  parseLog(records, sqliteHelper, container.ILogFile, container.OLogFile, snapshot, extra);
  return 0;
}

int processFinalize(IOBundle& bundle, IOContainer& container, std::string snapshot) {
  std::vector<File> records;
  parseMFT(records, container.IMft);

  outputEvents(records, bundle.SqliteHelper, bundle.Events, snapshot);

  return 0;
}

void run(Options& opts) {
  IOBundle bundle;
  setupIO(opts, bundle);

  for (auto& container: bundle.Containers) {
    std::cout << "Pre-processing: " << container->Dir << std::endl;
    processStep(*container, bundle.SqliteHelper, container->Dir.string(), opts.extra);
  }
  bundle.SqliteHelper.commit();

  std::cout << "Generating unified events output..." << std::endl;
  bundle.Events << Event::getColumnHeaders();
  std::vector<IOContainerPtr>::reverse_iterator rIt;
  for (rIt = bundle.Containers.rbegin(); rIt != bundle.Containers.rend(); ++rIt) {
    std::cout << "Processing: " << (*rIt)->Dir << std::endl;
    processFinalize(bundle, **rIt, (*rIt)->Dir.string());
  }
  bundle.SqliteHelper.close();
  std::cout << "Process complete." << std::endl;

}
