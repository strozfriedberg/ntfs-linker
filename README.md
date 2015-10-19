# NTFS-Linker
Author: Zack Weger

Copyright (c) Stroz Friedberg, LLC

Status: Alpha

## Description
NTFS Linker uses the `$MFT`, `$LogFile`, and `$UsnJrnl` to generate a "linked" 
history of file system activity on an NTFS volume. $LogFile and $UsnJrnl track
changes to files and folders over time. Linking the records in these logs with 
`$MFT` allows for the construction of a timeline of activity: 
creates, moves/renames, and deletes. NTFS Linker produces a UTF-8 tab separated 
value (TSV) report of records that can easily be filtered to review different 
types of activity. In addition, NTFS-Linker is able to run across all Volume
Shadow Copies (VSCs) on a volume, and produce output in a unified and
deduplicated manner.


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

NTFS-Linker produces three TSV reports: events.txt, log.txt, and usn.txt.

- log.txt: contains listing of log record headers. Does not contain the
redo/undo operation data, so this report is of limited use.

- usn.txt: contains a "raw" view of `$UsnJrnl` entries in more detail than 
what events.txt provides, including all of the Reason flags.

- events.txt: contains a unified view of all file system events, as parsed from
both $UsnJrnl and $LogFile, ordered(approximately - see below) by event time,
from most recent to oldest.

### General Notes
All timestamps are in the format YYYY-mm-dd HH:MM:SS.1234567.
Windows stores timestamps as the number of hundred nanoseconds since 1601 
(FILETIME). The routines used by NTFS-Linker to parse the time use standard 
C++ libraries, which may result in incorrect timestamps in some cases.
Specifically, if the time is BEFORE 1970 or AFTER 2038, the timestamp will 
not be displayed properly. The 1234567 refers to the 100-nanosecond part of 
the timestamp.

### usn.txt
The USN Journal reason code uses a bit packing scheme for each possible reason. 
From the time a file is opened to the time it is closed, the reasons will be 
combined. This means that multiple reasons may show up for a particular entry, 
even though only one operation happens at a time. The order the reasons are 
printed is completely arbitrary and has no correlation to the order in which 
they occurred.

#### USN Journal Example:
- USN|FILE_CREATE
- USN|CLOSE|FILE_CREATE
- USN|DATA_EXTEND
- USN|DATA_EXTEND|DATA_OVERWRITE
- USN|BASIC_INFO_CHANGE|DATA_EXTEND|DATA_OVERWRITE
- USN|BASIC_INFO_CHANGE|CLOSE|DATA_EXTEND|DATA_OVERWRITE

### log.txt
$LogFile event analysis is much more complicated.  Each record contains a redo 
and undo op code, as well as redo and undo data. In the case of data write 
events, it is possible to recover the data written for resident files, or the 
file sectors on disc for non-resident files. At this time NTFS-Linker does not 
recover the data written.

### events.txt

#### Event Source
This can be `$UsnJrnl/$J`, `$LogFile`, or "`$UsnJrnl` entry in `$LogFile`". `$LogFile` actually contains complete
`$UsnJrnl` entries.

#### File names and paths
While a file name can be extracted directly from a Log/Usn entry, the paths must be calculated.
The "folder" path is the calculated path to the parent directory, and the "Path" column is the calculated path to the
MFT record number if available.

#### Anchor and Event Ordering
While the exact order of `$LogFile` and `$UsnJrnl` events individually is known, the combined
ordering is not. They must be ordered according to the event timestamps, but NTFS-Linker places more confidence in
some timestamps than others. `$UsnJrnl` timestamps are taken at face value, while $LogFile create timestamps are not.
The timestamp for the `$LogFile` create event is the created timestamp in the MFT at the time of creation, which is not
necessarily the creation time. NTFS-Linker makes a principled best guess of the "true" `$LogFile` creation timestamps, 
and uses those to merge with `$UsnJrnl`. Anchored events denote events with timestamps which NTFS-Linker used for ordering.
The `$LogFile` events are represented in the order they occur, and will almost certainly have occurred between the 
anchoring `$UsnJrnl` events.

For example, the `$LogFile` events in the below snippet will almost certainly have occurred on 2012-04-08 02:55, as they
are anchored by two `$UsnJrnl` events within that time period.

| Timestamp   | Source                         | Event  | File                  |
|-------------|--------------------------------|--------|-----------------------|
| 4/8/12 2:55 | `$UsnJrnl/$J`                  | Delete | SAE.dat-journal       |
| 4/8/12 2:55 | `$UsnJrnl` entry in `$LogFile` | Create | SAE.dat-journal       |
| 4/8/12 2:55 | `$UsnJrnl` entry in `$LogFile` | Create | SAE.dat-journal       |
| 4/8/12 2:55 | `$LogFile`                     | Create | SAE.dat-journal       |
|             | `$LogFile`                     | Delete | 5.tmp                 |
|             | `$LogFile`                     | Create | 5.ini                 |
|             | `$LogFile`                     | Delete | 3.tmp                 |
|             | `$LogFile`                     | Create | 3.ini                 |
|             | `$LogFile`                     | Delete | 1.tmp                 |
|             | `$LogFile`                     | Create | 1.ini                 |
|             | `$LogFile`                     | Delete | SAEPolicy.dat-journal |
| 4/8/12 2:55 | `$UsnJrnl` entry in `$LogFile` | Create | SAE.dat-journal       |
| 4/8/12 2:55 | `$UsnJrnl/$J`                  | Create | SAE.dat-journal       |

#### Offset
The actual file offset (in decimal) to the beginning of the event record in the source file.

#### Created, Modified, Comment
For `$LogFile` create events, the timestamps from the Standard Information attribute
(regardless of the faith NTFS-Linker places in them) and whether those timestamps match the corresponding timestamps
in the File Name Attribute.

#### `$UsnJrnl` events
`$UsnJrnl` records are combined in events.txt to display one event for each logical event that
actually occurred. For instance, for a rename event, `$UsnJrnl` will contain at least two records: one containing the
"old name" and one containing the "new name" (and probably a couple other records, for the same event), but events.txt
displays this event just once. *However*, for the `$UsnJrnl` events embedded in `$LogFile`, this deduplication is not
performed. This means that for an embedded rename/move event, the File Name and MFT Record could be either from before
or after.

## Build Notes
The source is in C++ and uses autoconf.
