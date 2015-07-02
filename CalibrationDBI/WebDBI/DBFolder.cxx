#ifndef DBFOLDER_CXX 
#define DBFOLDER_CXX 1

#include "DBFolder.h"
#include "WebDBIConstants.h"
#include "CalibrationDBI/IOVData/IOVDataConstants.h"
#include "CalibrationDBI/IOVData/TimeStampDecoder.h"
#include "WebError.h"
#include <sstream>
#include <limits>
#include <iomanip>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include "wda.h"

namespace lariov {

  DBFolder::DBFolder(const std::string& name, const std::string& url, const std::string& tag /*= ""*/) :
    fCachedStart(0,0), fCachedEnd(0,0) {

    fFolderName = name;
    fURL = url;
    fTag = tag;
    if (fURL[fURL.length()-1] == '/') {
      fURL = fURL.substr(0, fURL.length()-1);
    }

    fCachedDataset = 0;
    fNRows =0;
    fColumns.clear();
    fTypes.clear();
    fCachedRow = -1;
    fCachedChannel = 0;
  }
  
  DBFolder::~DBFolder() {
    if (fCachedDataset) releaseDataset(fCachedDataset);
  }


  int DBFolder::GetNamedChannelData(std::uint64_t channel, const std::string& name, long& data) {

    Tuple tup;
    size_t col = this->GetTupleColumn(channel, name, tup);
    int err=0;
    data = getLongValue(tup, col, &err);
    releaseTuple(tup);
    return err;
  }

  int DBFolder::GetNamedChannelData(std::uint64_t channel, const std::string& name, double& data) {

    Tuple tup;
    size_t col = this->GetTupleColumn(channel, name, tup);
    int err=0;
    data = getDoubleValue(tup, col, &err);
    releaseTuple(tup);
    return err;
  }

  int DBFolder::GetNamedChannelData(std::uint64_t channel, const std::string& name, std::string& data) {

    Tuple tup;
    size_t col = this->GetTupleColumn(channel, name, tup);
    int err=0;
    char buf[kBUFFER_SIZE];
    int str_size = getStringValue(tup, col, buf, kBUFFER_SIZE, &err);
    data = std::string(buf, str_size);
    releaseTuple(tup);
    return err;
  }
  
  int DBFolder::GetChannelList( std::vector<std::uint64_t>& channels ) const {
    
    channels.clear();
    if (!fCachedDataset) return 1;
    
    Tuple tup;
    int err=0;
    for ( int row = 0; row != fNRows; ++row) {
      tup = getTuple(fCachedDataset, row + kNUMBER_HEADER_ROWS);
      channels.push_back( (std::uint64_t)getLongValue(tup,0,&err) );
      releaseTuple(tup);
    }  
    return err;
  }


  size_t DBFolder::GetTupleColumn(std::uint64_t channel, const std::string& name, Tuple& tup ) {

    //check if cached row is still valid
    int err;
    int row = -1;
    if (fCachedRow != -1 && fCachedChannel == channel) {
      tup = getTuple(fCachedDataset, fCachedRow + kNUMBER_HEADER_ROWS);
      if ( channel == (std::uint64_t)getLongValue(tup,0,&err) ) {
	row = fCachedRow;
      }
      else releaseTuple(tup);
    }

    //if cached row is not valid, find the new row
    if (row == -1) {   
//std::cout<<"Channel "<<channel<<" not cached"<<std::endl;
      //binary search for channel
      std::uint64_t val;
      int l = 0, h = fNRows - 1;
      row = (l + h )/2;
      while ( l <= h ) {
//std::cout<<"  "<<l<<"  "<<h<<"  "<<row<<std::endl;
	tup = getTuple(fCachedDataset, row + kNUMBER_HEADER_ROWS);
	val = getLongValue(tup, 0, &err);
	releaseTuple(tup);

	if (val == channel ) break;
	  
	if (val > channel) h = row - 1;
	else            l = row + 1;

	row = (l + h)/2;
      }
      
      //get the tuple to be returned, check that the found row matches the requested channel
      tup = getTuple(fCachedDataset, row + kNUMBER_HEADER_ROWS); 
      if ( channel != (std::uint64_t)getLongValue(tup, 0, &err) ) {
        releaseTuple(tup);
	std::string msg = "Channel " + std::to_string(channel) + " is not found in database!";
	throw WebError(msg);
      }
      
      
      //update caching info
      fCachedChannel = channel;
      fCachedRow = row;
    
    }

    //get the column corresponding to input string name and return
    for (size_t c=1; c < fColumns.size(); ++c ) {
      if (name == fColumns[c]) return c;
    }

    std::string msg = "Column named " + name + " is not found in the database!";
    throw WebError(msg);
    return 0;
  }

  //returns true if an Update is performed, false if not
  bool DBFolder::UpdateData( std::uint64_t raw_time) {
  
    //convert to IOVTimeStamp
    IOVTimeStamp ts = TimeStampDecoder::DecodeTimeStamp(raw_time);

    //check if cache is updated
    if (this->IsValid(ts)) return false;

    int err=0;

    //release old dataset
    if (fCachedDataset) releaseDataset(fCachedDataset);

    //get full url string
    std::stringstream fullurl;
    fullurl << fURL << "/data?f=" << fFolderName
            << "&t=" << ts.DBStamp();
    if (fTag.length() > 0) fullurl << "&tag=" << fTag;

    //get new dataset
    int status = -1;
    int tries = 0;
    long int delay;
    srandom(getpid() * getppid());
    while (status != 200 && tries < 7) { 
      fCachedDataset = getData(fullurl.str().c_str(), NULL, &err);
      status = getHTTPstatus(fCachedDataset);
      if ( status != 200) {
	delay = random() % (5 * (1 << tries));
	sleep(delay);
      }
      tries++;
    }

    if (status != 200) {
      std::string msg = "HTTP error: status: " + std::to_string(status) + ": " + std::string(getHTTPmessage(fCachedDataset));
      throw WebError(msg);
    }

    //update info about cached data
    fNRows = getNtuples(fCachedDataset) - kNUMBER_HEADER_ROWS;
    if (fNRows < 1) {
      std::stringstream msg;
      msg << "Time " << ts.DBStamp() << ": Data not found in database.";
      throw WebError(msg.str());
      fCachedStart = fCachedEnd = ts;
    }

    //start and end times
    Tuple tup;
    tup = getTuple(fCachedDataset, 0);   
    char buf[kBUFFER_SIZE];
    getStringValue(tup,0, buf, kBUFFER_SIZE, &err);
    fCachedStart = IOVTimeStamp::GetFromString(std::string(buf));
    releaseTuple(tup);

    tup = getTuple(fCachedDataset, 1);
    getStringValue(tup,0, buf, kBUFFER_SIZE, &err);
    if ( 0 == strcmp(buf,"-") ) {
      fCachedEnd = IOVTimeStamp::MaxTimeStamp();
    }
    else {
      fCachedEnd = IOVTimeStamp::GetFromString(std::string(buf));
    }
    releaseTuple(tup);

    //column names
    tup = getTuple(fCachedDataset, 2);
    fColumns.clear();
    for (int c=0; c < getNfields(tup); ++c) {
      getStringValue(tup, c, buf, kBUFFER_SIZE, &err);
      fColumns.push_back(buf);
    }
    releaseTuple(tup);

    //column types
    tup = getTuple(fCachedDataset, 3);
    fTypes.clear();
    for (int c=0; c < getNfields(tup); ++c) {
      getStringValue(tup, c, buf, kBUFFER_SIZE, &err);
      fTypes.push_back(buf);
    }
    releaseTuple(tup);

    return true;
  }

}//end namespace lariov
  
#endif  
  
  
  
