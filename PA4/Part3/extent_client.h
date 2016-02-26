// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
 private:
  rpcc *cl;

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);

  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status update(extent_protocol::extentid_t eid, 
      const char *name, std::string &buf);

  extent_protocol::status read(extent_protocol::extentid_t eid, 
      unsigned long offset, size_t size, std::string &buf);
  extent_protocol::status write(extent_protocol::extentid_t eid,
      unsigned long offset, std::string buf, int &nwritten);
  extent_protocol::status set_size(extent_protocol::extentid_t eid, size_t size);

};

#endif 

