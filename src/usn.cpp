#include "helper_functions.h"
#include "sqlite3.h"
#include "usn.h"
#include "progress.h"

/*
Returns the column names used for the USN CSV file
*/
std::string getUSNColumnHeaders() {
	return "MFTRecordNumber\tParentMFTRecordNumber\tUSN\tTimeStamp\tReason\tFileName\tPossiblePath\tPossibleParentPath";
}

/*
Decodes the USN reason code
USN uses a bit packing scheme to store reason codes.
Typically, as operations are performed on a file these reason codes are combined (&)
*/
std::string decodeUSNReason(int reason) {
	std::stringstream ss;
	ss << "USN|";
	if(reason & 0x00008000) ss << "BASIC_INFO_CHANGE|";
	if(reason & 0x80000000) ss << "CLOSE|";
	if(reason & 0x00020000) ss << "COMPRESSION_CHANGE|";
	if(reason & 0x00000002) ss << "DATA_EXTEND|";
	if(reason & 0x00000001) ss << "DATA_OVERWRITE|";
	if(reason & 0x00000004) ss << "DATA_TRUNCATION|";
	if(reason & 0x00000400) ss << "EXTENDED_ATTRIBUTE_CHANGE|";
	if(reason & 0x00040000) ss << "ENCRYPTION_CHANGE|";
	if(reason & 0x00000100) ss << "FILE_CREATE|";
	if(reason & 0x00000200) ss << "FILE_DELETE|";
	if(reason & 0x00010000) ss << "HARD_LINK_CHANGE|";
	if(reason & 0x00004000) ss << "INDEXABLE_CHANGE|";
	if(reason & 0x00000020) ss << "NAMED_DATA_EXTEND|";
	if(reason & 0x00000010) ss << "NAMED_DATA_OVERWRITE|";
	if(reason & 0x00000040) ss << "NAMED_DATA_TRUNCATION|";
	if(reason & 0x00080000) ss << "OBJECT_ID_CHANGE|";
	if(reason & 0x00002000) ss << "RENAME_NEW_NAME|";
	if(reason & 0x00001000) ss << "RENAME_OLD_NAME|";
	if(reason & 0x00100000) ss << "REPARSE_POINT_CHANGE|";
	if(reason & 0x00000800) ss << "SECURITY_CHANGE|";
	if(reason & 0x00200000) ss << "STREAM_CHANGE|";
	std::string rtn = ss.str();
	if(rtn.length() > 0)
		rtn = rtn.substr(0, rtn.length() - 1);
	return rtn;
}

/*
Parses all records found in the USN file represented by input. Uses the records map to recreate file paths
Outputs the results to several streams.
*/
void parseUSN(std::map<unsigned int, file*>& records, sqlite3* db, std::ostream& create, std::ostream& del, std::ostream& rename, std::ostream& move, std::istream& input, std::ostream& output) {
//void parseUSN(std::map<unsigned int, file*>& records, sqlite3* db, std::istream& input) {
	if(sizeof(long long) < 8) {
		std::cerr << "64-bit arithmetic not available. This won't work." << std::endl;
		exit(1);
	}

	unsigned int bufferSize = 4096;
	char* buffer = new char[bufferSize];

	if(getEpochDifference() != 0) {
		std::cerr << "Non-standard time epoch in use. Scrutinize dates/times closely.\n Continuing...\n";
	}

	bool done = false;
	int records_processed = -1;
	std::stringstream columns(getUSNColumnHeaders());
	std::string field;
	int reasonColumn;
	int count = 0;
	while(columns.good()) {
		std::getline(columns, field, ',');
		if(field == "Reason")
			reasonColumn = count;
		count++;
	}
	//print the column headers
//	output << getUSNColumnHeaders() << std::endl;
	
	//This section of code to be left commented out.
	//Originally intended to skip past the leading 0's in the USNJrnl file
	//However, due to issues with large file support in C++,
	//This functionality is transferred to the companion python script
	//Which does the same thing

	/*The USNJrnl file uses a sparse file format.
	New records are always written to the end of the file.
	When the file becomes too large, the beginning of the file is deallocated.
	Since the file is sparse, the logical position of the rest of the data is unchanged.
	Because of the way some tools extract the USNJrnl file, we may be left with several GB of 0's at the front of the file.
	Start at the end, seek backwards to the beginning of the data, then continue.
	*/
	input.seekg(0, std::ios::end);
	std::streampos end = input.tellg();
//	while(!done) {
//		//std::cout << input.tellg() << std::endl;
//		input.seekg(-(1 << 20), std::ios::cur);
//		input.read(buffer, 4096);
//		done = true;
//		for(int i = 0; i < 4096 && done; i++) {
//			if(buffer[i] != 0)
//				done = false;
//		}
//	}
	//if adding in the above functionality, delete these 2 lines below after
	input.seekg(0, std::ios::beg);
	input.read(buffer, 4096);

	std::streampos start = input.tellg();
	ProgressBar status(end - start);
	done = false;

	USN_Record temp_rec;
	int rc;
	std::string usn_sql = "insert into usn values(?, ?, ?, ?, ?, ?, ?, ?);";
	std::string create_sql = "insert into create_events values (?, ?, ?, ?, ?, ?, ?, ?);";
	std::string delete_sql  = "insert into delete_events values (?, ?, ?, ?, ?, ?, ?, ?);";
	std::string rename_sql  = "insert into rename_events values (?, ?, ?, ?, ?, ?, ?, ?, ?);";
	std::string move_sql  = "insert into move_events values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
	sqlite3_stmt* usn_stmt;
	sqlite3_stmt* create_stmt;
	sqlite3_stmt* delete_stmt;
	sqlite3_stmt* rename_stmt;
	sqlite3_stmt* move_stmt;
	rc = 0;
	rc &= sqlite3_prepare_v2(db, usn_sql.c_str(), usn_sql.length() + 1, &usn_stmt, NULL);
	rc &= sqlite3_prepare_v2(db, create_sql.c_str(), create_sql.length() + 1, &create_stmt, NULL);
	rc &= sqlite3_prepare_v2(db, delete_sql.c_str(), delete_sql.length() + 1, &delete_stmt, NULL);
	rc &= sqlite3_prepare_v2(db, rename_sql.c_str(), rename_sql.length() + 1, &rename_stmt, NULL);
	rc &= sqlite3_prepare_v2(db, move_sql.c_str(), move_sql.length() + 1, &move_stmt, NULL);
	if (rc) {
		std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
		std::cerr << sqlite3_errmsg(db) << std::endl;
		sqlite3_close(db);
		exit(2);
	}

//	rc = sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);



	unsigned int offset = 0;

	//scan through the $USNJrnl one record at a time. Each record is variable length.
	while(!input.eof() && !done) {
		status.setDone((unsigned long long) input.tellg() - start);	

		if(offset + 4 > bufferSize || hex_to_long(buffer + offset, 4) + offset > bufferSize) {
			char* temp = new char[4096 + bufferSize - offset];
			memcpy(temp, buffer + offset, bufferSize - offset);
			input.read(temp + bufferSize - offset, 4096);
			bufferSize = 4096 + bufferSize - offset;
			delete [] buffer;
			buffer = temp;
			offset = 0;

		}

		//input.read(buffer, 8);
		unsigned long long record_length = hex_to_long(buffer + offset, 4);

		if(record_length == 0) {
			offset += 8;
			continue;
		}
		if(record_length > 0x1000) {
			std::cerr << std::setw(60) << std::left << std::setfill(' ') << "\r";
			std::cerr << "\r";
			std::cerr.flush();
			std::cerr << std::hex << record_length << " is an awfully large record_length!" << std::endl;
			std::cerr << "Cannot continue. Check that we're not missing much at " 
				<< std::hex << input.tellg()<< std::endl;
			break;
		}
		records_processed++;
		if(bufferSize < record_length) {
			char* temp = new char[record_length + bufferSize - offset];
			memcpy(temp, buffer, bufferSize);

			delete[] buffer;
			buffer = temp;

			input.read(buffer + bufferSize, record_length - bufferSize);
			bufferSize = record_length + bufferSize - offset;
		}
		//input.read(buffer + 8, record_length - 8);
		USN_Record rec(buffer + offset, records);
		
		output << rec.toString(records);

		rec.insert(db, usn_stmt, records);		
		//determine which, if any, secondary streams should be written to.
		if(rec.reason & 0x100) { //CREATE
			create << rec.toCreateString(records);
			rec.insert(db, create_stmt, records);
		}
		if(rec.reason & 0x200) { //DELETE
			del << rec.toDeleteString(records);
			rec.insert(db, delete_stmt, records);
		}
		if(rec.reason & 0x1000 || rec.reason & 0x2000) { //RENAME

			/*
			Both rename and move events are classified as rename for USNJrnl purposes
			Furthermore, duplicate entries are present for each (with the name/location before and after)
			We do a little extra processing to avoid printing duplicates into create and move csv files
			*/
			if(temp_rec.usn == 0) {
				temp_rec = rec;
			}
			if(rec.reason & 0x1000) { //OLD
				temp_rec.file_name = rec.file_name;
				temp_rec.par_mft_record_no = rec.par_mft_record_no;
			}
			if(rec.reason & 0x2000) { //NEW
				temp_rec.file_name_after = rec.file_name;
				temp_rec.par_record_after = rec.par_mft_record_no;
			}
			temp_rec.reason |= rec.reason;
		}
		if(temp_rec.mft_record_no != rec.mft_record_no || rec.reason & 0x80000000) { //CLOSE
			if (temp_rec.file_name_after != "") { //if(temp_rec.reason & 0x1000 || temp_rec.reason & 0x2000) {
				//RENAME
				if(temp_rec.file_name != temp_rec.file_name_after) {
					rename << temp_rec.toRenameString(records);
					temp_rec.insertRename(db, rename_stmt, records);
				}
				if(temp_rec.par_mft_record_no != temp_rec.par_record_after) {
					move << temp_rec.toMoveString(records);
					temp_rec.insertMove(db, move_stmt, records);
				}
			}
			temp_rec.clearFields();	
		}
//		memset(buffer, 0, record_length);
		offset += record_length;

	}
//	rc = sqlite3_exec(db, "END TRANSACTION", 0, 0, 0);
	status.finish();
	sqlite3_finalize(usn_stmt);
	sqlite3_finalize(create_stmt);
	sqlite3_finalize(delete_stmt);
	sqlite3_finalize(rename_stmt);
	sqlite3_finalize(move_stmt);
	delete[] buffer;
}

USN_Record::USN_Record(char* buffer, std::map<unsigned int, file*>& records) {
	
	file_ref_no = hex_to_long(buffer + 0x8, 8);
	mft_record_no = hex_to_long(buffer + 0x8, 6);

	par_file_ref_no = hex_to_long(buffer + 0x10, 8);
	par_mft_record_no = hex_to_long(buffer + 0x10, 6);

	usn = hex_to_long(buffer + 0x18, 8);
	timestamp = hex_to_long(buffer + 0x20, 8);
	reason = hex_to_long(buffer + 0x28, 4);
	file_len = hex_to_long(buffer + 0x38, 2);
	name_offset = hex_to_long(buffer + 0x3A, 2);
	file_name = mbcatos(buffer + name_offset, file_len >> 1);
}

void USN_Record::clearFields() {
	
	file_ref_no = 0;
	mft_record_no = 0;

	par_file_ref_no = 0;
	par_mft_record_no = 0;

	usn = 0;
	timestamp = 0;
	reason = 0;
	file_len = 0;
	name_offset = 0;
	file_name = "";
	file_name_after = "";
	par_record_after = 0;
}

USN_Record::USN_Record() {
	clearFields();
}

std::string USN_Record::toString(std::map<unsigned int, file*>& records) {
	std::stringstream ss;
	ss << mft_record_no << "\t" << par_mft_record_no << "\t" << usn << "\t" << filetime_to_iso_8601(timestamp)
		<< "\t" << decodeUSNReason(reason) << "\t" << file_name << "\t" << getFullPath(records, mft_record_no)
		<< "\t" << getFullPath(records, par_mft_record_no);
	ss << std::endl;
	return ss.str();
}

std::string USN_Record::toCreateString(std::map<unsigned int, file*>& records) {
	std::stringstream ss;
	ss << mft_record_no << "\t" << par_mft_record_no << "\t" << usn << "\t" << filetime_to_iso_8601(timestamp)
		<< "\t" << decodeUSNReason(reason) << "\t" << file_name << "\t" << getFullPath(records, mft_record_no)
		<< "\t" << getFullPath(records, par_mft_record_no);
	ss << std::endl;
	return ss.str();
}

std::string USN_Record::toDeleteString(std::map<unsigned int, file*>& records) {
	std::stringstream ss;
	ss << mft_record_no << "\t" << par_mft_record_no << "\t" << usn << "\t" << filetime_to_iso_8601(timestamp)
		<< "\t" << decodeUSNReason(reason) << "\t" << file_name << "\t" << getFullPath(records, mft_record_no)
		<< "\t" << getFullPath(records, par_mft_record_no);
	ss << std::endl;
	return ss.str();
}

std::string USN_Record::toRenameString(std::map<unsigned int, file*>& records) {
	std::stringstream ss;
	ss << mft_record_no << "\t" << par_mft_record_no << "\t" << usn << "\t"
		<< filetime_to_iso_8601(timestamp) << "\t" << decodeUSNReason(reason) << "\t" 
		<< file_name
		<< "\t" << file_name_after << "\t" << getFullPath(records, mft_record_no) << "\t"
		<< getFullPath(records, par_mft_record_no);
	ss << std::endl;
	return ss.str();
}

std::string USN_Record::toMoveString(std::map<unsigned int, file*>& records) {
	std::stringstream ss;
	ss << mft_record_no << "\t" << par_mft_record_no << "\t" << par_record_after << "\t" 
		<< usn << "\t"
		<< filetime_to_iso_8601(timestamp) << "\t" << decodeUSNReason(reason) << "\t" 
		<< file_name << "\t"
		<< getFullPath(records, mft_record_no) << "\t" << getFullPath(records, par_mft_record_no)
		<< "\t" << getFullPath(records, par_record_after);
	ss << std::endl;
	return ss.str();
}

void USN_Record::insert(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records) {
	sqlite3_bind_int64(stmt, 1, mft_record_no);
	sqlite3_bind_int64(stmt, 2, par_mft_record_no);
	sqlite3_bind_int64(stmt, 3, usn);
	sqlite3_bind_text(stmt, 4, filetime_to_iso_8601(timestamp).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 5, decodeUSNReason(reason).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 6, file_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 7, getFullPath(records, mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 8, getFullPath(records, par_mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
	
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
}

void USN_Record::insertRename(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records) {
	sqlite3_bind_int64(stmt, 1, mft_record_no);
	sqlite3_bind_int64(stmt, 2, par_mft_record_no);
	sqlite3_bind_int64(stmt, 3, usn);
	sqlite3_bind_text(stmt, 4, filetime_to_iso_8601(timestamp).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 5, decodeUSNReason(reason).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 6, file_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 7, file_name_after.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 8, getFullPath(records, mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 9, getFullPath(records, par_mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
	
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
}

void USN_Record::insertMove(sqlite3* db, sqlite3_stmt* stmt, std::map<unsigned int, file*>& records) {
	sqlite3_bind_int64(stmt, 1, mft_record_no);
	sqlite3_bind_int64(stmt, 2, par_mft_record_no);
	sqlite3_bind_int64(stmt, 3, par_record_after);
	sqlite3_bind_int64(stmt, 4, usn);
	sqlite3_bind_text(stmt, 5, filetime_to_iso_8601(timestamp).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 6, decodeUSNReason(reason).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 7, file_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 8, getFullPath(records, mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 9, getFullPath(records, par_mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 10, getFullPath(records, par_record_after).c_str(), -1, SQLITE_TRANSIENT);
	
	sqlite3_step(stmt);
	sqlite3_reset(stmt);
}

//USN_Record& USN_Record::operator=(const USN_Record& rhs) {
//	file_ref_no = rhs.file_ref_no;
//	mft_record_no = rhs.mft_record_no;
//	par_file_ref_no = rhs.par_file_ref_no;
//	par_mft_record_no = rhs.par_mft_record_no;
//	usn = rhs.usn;
//	timestamp = rhs.timestamp;
//	file_len = rhs.file_len;
//	name_offset = rhs.name_offset;
//	file_name = rhs.file_name;
//	return *this;
//}
