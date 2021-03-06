// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"

using namespace std;

class extent_server {

 public:
  extent_server();
  // You must store file attributes in this map
  std::map<extent_protocol::extentid_t, extent_protocol::attr> attrmap;

  // You may store file contents in this map
  std::map<extent_protocol::extentid_t, std::string> contmap;

  int put(extent_protocol::extentid_t id, std::string, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);
  int update(extent_protocol::extentid_t id, int is_file, std::string, std::string &);
  int read(extent_protocol::extentid_t id, size_t, 
      unsigned long, std::string &);
  int write(extent_protocol::extentid_t id, std::string, unsigned long,
      int &);
  int set_size(extent_protocol::extentid_t id, size_t size, int &);
  int unlink(extent_protocol::extentid_t id, std::string, std::string &);
};

#endif 







