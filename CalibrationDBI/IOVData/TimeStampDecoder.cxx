#ifndef TIMESTAMPDECODER_CXX
#define TIMESTAMPDECODER_CXX

#include <string>
#include "TimeStampDecoder.h"
#include "IOVDataConstants.h"
#include "IOVDataError.h"

namespace lariov {

  IOVTimeStamp TimeStampDecoder::DecodeTimeStamp(std::uint64_t ts) {
        
    std::string time = std::to_string(ts);

    //microboone stores timestamp as ns from epoch, so there should be 19 digits.
    if (time.length() == 19) {
      //make timestamp conform to database precision
      time = time.substr(0, 10+kMAX_SUBSTAMP_LENGTH);

      //insert decimal point
      time.insert(10,".");

      //finish construction
      IOVTimeStamp tmp = IOVTimeStamp::GetFromString(time);
      return tmp;
    }
    else {
      std::string msg = "TimeStampDecoder: I do not know how to convert this timestamp: " + ts;
      throw IOVDataError(msg);
    } 
  }
}//end namespace lariov

#endif
