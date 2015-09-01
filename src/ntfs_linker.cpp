#include "helper_functions.h"
#include "file.h"
#include "log.h"
#include "mft.h"
#include "usn.h"
#include "aggregate.h"

#include <sqlite3.h>

int main(int argc, char** argv) {
  std::vector<File> records;
  /*
  Parse cmd options, or display help
  Notice that unix style cmd option flags are used ('-') rather
  than windows style flags ('/')
  */
  if(!cmdOptionExists(argv, argv+argc, "--python")) {
    std::cout << "Please launch this tool from the Python script (ntfs-linker.py)" << std::endl;
    exit(0);
  }
  if(cmdOptionExists(argv, argv+argc, "-h")) {
    std::cout << argv[0] << " Version: " << VERSION << ": Usage" << std::endl;
    std::cout << "The following options apply to input files:" << std::endl;
    std::cout << "\t1. {no options}" << std::endl;
    std::cout << "\t\tUse current folder to look for files named $MFT, $LogFile, $J" << std::endl;
    std::cout << "\t2. -i input_directory" << std::endl;
    std::cout << "\t\tUse the given directory to look for files named $MFT, $LogFile, $USN" << std::endl;
    std::cout << "\t3. " << argv[0] << " -m mft -u usn -l logfile" << std::endl;
    std::cout << "\t\tSpecify each file individually." << std::endl;
    std::cout << "\tNOTE: When searching for USNJrnl file, this application will look for $J first," << std::endl;
    std::cout << "\tbut will also use $UsnJrnl.$J ($USNJR~1 short file name) if it is present" << std::endl;
    std::cout << std::endl;
    std::cout << "The following options apply to output files:" << std::endl;
    std::cout << "\t1. {no options}" << std::endl;
    std::cout << "\t\tUse current folder for all output files." << std::endl;
    std::cout << "\t2. -o output_directory" << std::endl;
    std::cout << "\t\tUse the given directory for all output files." << std::endl;
    std::cout << std::endl;
    std::cout << "And here are some misc. options:" << std::endl;
    std::cout << "\t1. --overwrite" << std::endl;
    std::cout << "\t\tOverwrite all output files if they currently exist (default: append)" << std::endl;
    std::cout << "\t2. --version" << std::endl;
    std::cout << "\t\tDisplay the info and exit." << std::endl;
    std::cout << "\t3. -h" << std::endl;
    std::cout << "\t\tDispaly this help screen and exit." << std::endl;
    exit(0);
  }
  if(cmdOptionExists(argv, argv+argc, "--version")) {
    std::cout << argv[0] << " Version " << VERSION << std::endl;
    exit(0);
  }

  std::ifstream i_mft, i_usnjrnl, i_logfile;
  std::ofstream o_mft, o_usnjrnl, o_logfile, o_events;
  std::string dbName;

  if(cmdOptionExists(argv, argv + argc, "-i")) {
    char* dir = getCmdOption(argv, argv + argc, "-i");
    std::stringstream ss_mft, ss_usn, ss_log;
    char sep = getPathSeparator();
    ss_mft << dir << sep << "$MFT";
    ss_usn << dir << sep << "$J";
    ss_log << dir << sep << "$LogFile";
    i_mft.open(ss_mft.str().c_str(), std::ios::binary);
    i_usnjrnl.open(ss_usn.str().c_str(), std::ios::binary);
    i_logfile.open(ss_log.str().c_str(), std::ios::binary);
  } else if(cmdOptionExists(argv, argv+argc, "-m")) {
    i_mft.open(getCmdOption(argv, argv+argc, "-m"), std::ios::binary);
    i_usnjrnl.open(getCmdOption(argv, argv+argc, "-u"), std::ios::binary);
    i_logfile.open(getCmdOption(argv, argv+argc, "-l"), std::ios::binary);
  } else {
    i_mft.open("$MFT", std::ios::binary);
    i_usnjrnl.open("$USNJR~1", std::ios::binary);
    i_logfile.open("$LogFile", std::ios::binary);

//    if(!i_usnjrnl) {
//      i_usnjrnl.open("$USNJR~1", std::ios::binary);
//    }
  }

  if(!i_mft) {
    std::cerr << "$MFT File not found." << std::endl;
    exit(0);
  }
  if(!i_usnjrnl) {
    std::cerr << "$J File not found." << std::endl;
    exit(0);
  }
  if(!i_logfile) {
    std::cerr << "$LogFile File not found: " << std::endl;
    exit(0);
  }
  bool overwrite = false;
  if(cmdOptionExists(argv, argv+argc, "--overwrite")) {
    overwrite = true;
  }

  if(cmdOptionExists(argv, argv+argc, "-o")) {
    char* out = getCmdOption(argv, argv+argc, "-o");
    std::stringstream ss_mft, ss_usn, ss_log, ss_events;
    std::stringstream cmd, ss_db;

    if(isUnix())
      cmd << "mkdir -p " << out << " 2> /dev/null";
    else
      cmd << "if not exist \"" << out << "\" mkdir " << out << " 2> nul";
    if (system(cmd.str().c_str())) {
      std::cerr << "Couldn't create output directory!" << std::endl;
      exit(0);
    }
    ss_mft << out << getPathSeparator() << "mft.txt";
    ss_usn << out << getPathSeparator() << "usnjrnl.txt";
    ss_log << out << getPathSeparator() << "logfile.txt";
    ss_events << out << getPathSeparator() << "events.txt";
    ss_db << out << getPathSeparator() << "ntfs.db";

    prep_ofstream(o_mft, ss_mft.str().c_str(), overwrite);
    prep_ofstream(o_usnjrnl, ss_usn.str().c_str(), overwrite);
    prep_ofstream(o_logfile, ss_log.str().c_str(), overwrite);
    prep_ofstream(o_events, ss_events.str().c_str(), overwrite);
    dbName = ss_db.str();
  } else {
    prep_ofstream(o_mft, "mft.txt", overwrite);
    prep_ofstream(o_usnjrnl, "usn.txt", overwrite);
    prep_ofstream(o_logfile, "logfile.txt", overwrite);
    prep_ofstream(o_events, "events.txt", overwrite);
    dbName = "ntfs.db";
  }


  //Set up db connection
  std::cout << "Setting up DB Connection..." << std::endl;
  SQLiteHelper sqliteHelper(dbName, overwrite);

  std::cout << "Creating MFT Map..." << std::endl;
  parseMFT(records, sqliteHelper, i_mft, o_mft, true);

  i_mft.clear();
  i_mft.seekg(0);

  //print column headers
  o_events << "Order\t"
           << "MFTRecNo\t"
           << "ParRecNo\t"
           << "PreviousParRecNo\t"
           << "USN_LSN\t"
           << "Timestamp\t"
           << "FileName\t"
           << "PreviousFileName\t"
           << "Path\t"
           << "ParPath\t"
           << "PreviousParPath\t"
           << "EventType\t"
           << "EventSource\t"
           << "IsAnchor\t"
           << "IsEmbedded\t"
           << std::endl;

  std::cout << "Parsing USNJrnl..." << std::endl;
  parseUSN(records, sqliteHelper, i_usnjrnl, o_usnjrnl);
  std::cout << "Parsing LogFile..." << std::endl;
  parseLog(records, sqliteHelper, i_logfile, o_logfile);
  sqliteHelper.commit();

  std::cout << "Generating unified events output..." << std::endl;
  outputEvents(records, sqliteHelper, o_events);

  sqliteHelper.close();
  std::cout << "Process complete." << std::endl;
}
