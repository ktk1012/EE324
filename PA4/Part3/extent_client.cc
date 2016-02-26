// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
	make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::get, eid, buf);
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::getattr, eid, attr);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  int r;
  ret = cl->call(extent_protocol::put, eid, buf, r);
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  int r;
  ret = cl->call(extent_protocol::remove, eid, r);
  return ret;
}

extent_protocol::status
extent_client::update(extent_protocol::extentid_t eid, const char *name,
    std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  //as extend client receives string object, so convert 
  //const char * to string
  std::string cname(name);
  ret = cl->call(extent_protocol::update, eid, name, buf);
  return ret;
}

extent_protocol::status
extent_client::read(extent_protocol::extentid_t eid, 
    unsigned long offset, size_t size, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::read, eid, size, offset, buf);
  return ret;
}

extent_protocol::status
extent_client::write(extent_protocol::extentid_t eid,
    unsigned long offset, std::string buf, int &nwritten)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::write, eid, buf, offset, nwritten);
  return ret;
}

extent_protocol::status
extent_client::set_size(extent_protocol::extentid_t eid, size_t size)
{
  extent_protocol::status ret = extent_protocol::OK;
  int r;
  ret = cl->call(extent_protocol::set_size, eid, size, r);
  return ret;
}
