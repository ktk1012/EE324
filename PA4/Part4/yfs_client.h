#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
// Include lock_client for locking
#include "lock_client.h"
#include <vector>


  class yfs_client {
  extent_client *ec;
  // member for lock client
  lock_client *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);
  inum ilookup(inum di, std::string name);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);

  int getcont(inum, std::string &);
  int putcont(inum, std::string);
  int update(inum, const char *, inum &, bool is_file);
  int create(inum, const char *, inum &, bool is_file);


  int read(inum, char *, size_t, unsigned long, int &);
  int write(inum, const char *, size_t, unsigned long, int &);
  int set_size(inum, size_t);

  int unlink(inum, const char*);
};

#endif 
