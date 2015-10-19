# NTFS-Linker
Author: Zack Weger

Copyright (c) Stroz Friedberg, LLC

## Description
NTFS-Linker is a tool designed to parse the special NTFS files $MFT, $LogFile, and $J. 

The Master File Table ($MFT) stores all of the metadata for all files on disc, as well as either the file data itself or the location of the file data on disc.
The USN Journal ($J) contains high-level information about changes to the file system.
The Transactional Journal ($LogFile) contains more detailed information about changes to the file system.
Combined, these files can provide a wealth of information about the history of a file system.

## Usage
```
ntfs-linker, Copyright (c) Stroz Friedberg, LLC
Version 0.1.0
Allowed options:
  --help                display help and exit
  --output arg          directory in which to dump output files
  --input arg           If no image specified, location of directory containing
                        input files: $MFT, $UsnJrnl, $LogFile. Otherwise, root 
                        directory in which to dump files extracted from image.
  --image arg           Path to image
  --version             display version number and exit
  --overwrite           overwrite files in the output directory. Default: 
                        append
  --extra               Outputs supplemental lower-level parsed data from 
                        $UsnJrnl and $LogFile

  ```


## Output

### General Notes
All timestamps are in the format YYYY-mm-dd HH:MM:SS.1234567. This format ensures that a text-based sort of the timestamps will also sort by time.
Windows stores timestamps as the number of hundred nanoseconds since 1601 (FILETIME). The routines used by NTFS-Linker to parse the time use standard C++ libraries, which may result in incorrect timestamps in some cases.
Specifically, if the time is BEFORE 1970 or AFTER 2038, the timestamp will not be displayed properly.
The 1234567 refers to the 100-nanosecond part of the timestamp.

The create, delete, rename, and move tables contain events pulled from both the USN Journal and the LogFile. A note on the file path columns in these tables:
the file paths are not 100% reliable. Specifically, if the name of a directory changes between the time of the event and the time the MFT was saved, then the NEW
directory name will be shown. The file paths are all recreated using the current MFT, not some version of the MFT at the time of the event (because it is not available).

Also, because the events provide both the file reference number and the parent file reference number, NTFS-Linker will show the full path for both. In many cases there is a very obvious disconnect between the two. In general, I have found the parent path and reference number to be more reliable.

### USN Journal ($UsnJrnl, $J)
The USN Journal reason code uses a bit packing scheme for each possible reason. From the time a file is opened to the time it is closed, the reasons will be combined.
This means that multiple reasons may show up for a particular entry, even though only one operation happens at a time. The order the reasons are printed is completely arbitrary and has no correlation to the order in which they occurred.

#### USN Journal Example:
- USN|FILE_CREATE
- USN|CLOSE|FILE_CREATE
- USN|DATA_EXTEND
- USN|DATA_EXTEND|DATA_OVERWRITE
- USN|BASIC_INFO_CHANGE|DATA_EXTEND|DATA_OVERWRITE
- USN|BASIC_INFO_CHANGE|CLOSE|DATA_EXTEND|DATA_OVERWRITE

### $LogFile
$LogFile event analysis is much more complicated.  Each record contains a redo and undo op code, as well as redo and undo data. In the case of data write events, it is possible to recover the data written for resident files, or the file sectors on disc for non-resident files.
At this time NTFS-Linker does not recover the data written.

For delete, move, and rename events (coming from $LogFile), the time displayed is the SIA mft_modified timestamp of the parent directory. This is the closest indicator available. Note that it is not necessarily the event time. One thing you can do to verify the time is to check the LSN number, and compare it with create events (which has a more accurrate associated timestamp) and their LSN numbers, since the LSN number is monotonic increasing.

#### $LogFile Example:
I have a delete event with LSN 62631278170 and two create events LSNs: 62631273253 and 62631278339, with timestamps 2013-08-07 21:53:51 0078983 and 2013-08-07 21:53:54 8288983, respectively. This indicates that the delete event probably occurred within this timeframe.

## Build Notes
The ntfs-linker executable is built from C/C++ source and can be compiled with the included makefile. In a Windows environment, I recommend using MinGW, moving to the ntfs-linker directory, and running 'make'. The make file is programmed to auto-increment the build number. You might need to change some of the compiler flags to build in another environment.

The Python script is intended to run with version 2.7.5+ and is used to launch the main executable. Its main purpose is to deal with excessively large USN Journal files (the USN Journal is a sparse file, but EnCase fills all areas of the file when copying it out of an image) by making a new file (`$USN`) with only the relevant data, which is located at the end of the file.