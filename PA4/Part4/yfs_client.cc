// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client(lock_dst);
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;


  //printf("getfile %016llx\n", inum);
  // Get lock correspond to inum
  lc->acquire(inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  //printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:
  // release acquired lock 
  lc->release(inum);
  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;


  //printf("getdir %016llx\n", inum);
  lc->acquire(inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  lc->release(inum);
  return r;
}


/* Get content */
int
yfs_client::getcont(inum inum, std::string &buf) 
{
  int r = OK;
  lc->acquire(inum);
  if (ec->get(inum, buf) != extent_protocol::OK) {
    r = IOERR;
  }
  lc->release(inum);
  return r;
}

/* Put content */
int
yfs_client::putcont(inum inum, std::string buf)
{
  int r = OK;

  if (ec->put(inum, buf) != extent_protocol::OK) {
    std::cout << "IO error" << std::endl;
    r = IOERR;
  }

  return r;
}


/* Update content of existing file */
int
yfs_client::update(inum inum, const char *name, yfs_client::inum &cid, bool is_file)
{
  int r = OK;
  std::string buf("");
  if (ec->update(inum, name, is_file, buf) != extent_protocol::OK) {
    r = IOERR;
  }
  std::istringstream i_st(buf);
  i_st >> cid;

  return r;
}

/* Append file / directory information to parent and create,
 * This wrapps two previous used function (putcont, update) with
 * corresponding locks*/
int 
yfs_client::create(inum inum, const char *name, yfs_client::inum &cid, bool is_file)
{
  int r = OK;
  lc->acquire(inum);
  if ((r = update(inum, name, cid, is_file)) != yfs_client::OK) {
      lc->release(inum);
      return r;
  }
  std::string buf("");
  lc->acquire(cid);
  if ((r = putcont(cid, buf) != yfs_client::OK)) {
      lc->release(cid);
      lc->release(inum);
      return r;
  }
  lc->release(cid);
  lc->release(inum);
  return r;
}

int
yfs_client::read(inum inum, char *buf, size_t size,
    unsigned long offset, int &nread)
{
  int r = OK;
  std::string temp;
  lc->acquire(inum);
  extent_protocol::status ret = ec->read(inum, offset, size, temp);
  if (ret == extent_protocol::OK) {
    nread = temp.size();
    std::memcpy(buf, temp.c_str(), nread);
  } else if (ret == extent_protocol::NOENT) {
    r = NOENT;
  } else {
    r = IOERR;
  }
  lc->release(inum);
  return r;
}
    
int 
yfs_client::write(inum inum, const char *buf, size_t size, 
    unsigned long offset, int &nwritten) 
{
  int r = OK;
  std::string temp(buf, size);
  lc->acquire(inum);
  extent_protocol::status ret = ec->write(inum, offset, temp, nwritten);
  if (ret != extent_protocol::OK) {
    if (ret == extent_protocol::NOENT){
      r = NOENT;
    } else {
      r = IOERR;
    }
  }
  lc->release(inum);
  return r;
}

int
yfs_client::set_size(inum inum, size_t size)
{
  int r = OK;
  lc->acquire(inum);
  extent_protocol::status ret = ec->set_size(inum, size);
  if (ret != extent_protocol::OK) {
    if (ret == extent_protocol::NOENT) {
      r = NOENT;
    } else {
      r = IOERR;
    }
  }
  lc->release(inum);
  return r;
}

int
yfs_client::unlink(inum parent, const char *name)
{
  std::string buf("");
  inum cid;
  lc->acquire(parent);
  if (ec->unlink(parent, name, buf) != extent_protocol::OK) {
      lc->release(parent);
      return IOERR;
  }

  std::istringstream st(buf);
  st >> cid;
  lc->acquire(cid);
  if (ec->remove(cid) != extent_protocol::OK) {
      lc->release(cid);
      lc->release(parent);
      return IOERR;
  }

  lc->release(cid);
  lc->release(parent);

  return OK;
}
