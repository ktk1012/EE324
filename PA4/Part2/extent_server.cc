// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

//for debugging
#include <iostream>

extent_server::extent_server() {
/*initialize stuff here, make sure to create
 *an entry for the root of the filesystem*/
  /* Attribute for root directory*/
  extent_protocol::attr root;
  root.atime = root.mtime = root.ctime = time(NULL);

  root.size = 0;
  attrmap[1] = root;
  // Set content with root server name ('.') and inum 1
  // Content naming rule is name inum with linewise oredering
  // filename inum\n filename inum\n ....
  // Note that we may assume that file has no spaces or tabs
  //  -- (see part2 pdf)
  contmap[1] = ". 1\n";
}

/* Create or put new content */
int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  /* If there is no file correspond to id, create it with accesstime 0,
   * Otherwise, change content of it */
  if (contmap.find(id) == contmap.end()) {
    extent_protocol::attr temp;
    temp.atime = 0;
    attrmap[id] = temp;
  }

  attrmap[id].size = buf.size();
  /* Update mtime and ctime (as we modified size and content)*/
  attrmap[id].mtime = attrmap[id].ctime = time(NULL);

  /* Put content */
  contmap[id] = buf;


	return extent_protocol::OK;	
}

/* Get content of corresponding file */
int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  /* If there is no file return NOENT error */
  if (contmap.find(id) == contmap.end()) {
    return extent_protocol::NOENT;	
  } else {
    /* Find file, copy to buf and update accessed time */
    buf = contmap[id];
    attrmap[id].atime = time(NULL);
    return extent_protocol::OK;
  }
}

/* Get attr of file, if found update accessed time and return */
int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  if(attrmap.find(id) != attrmap.end()){
	a = attrmap[id];
	return extent_protocol::OK;
  }else{
	return extent_protocol::NOENT;
  }

}

/* Remove file (find content and attr correspond to id and erase it) */
int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  if (contmap.find(id) == contmap.end()) {
    return extent_protocol::NOENT;
  } else {
    contmap.erase(id);
    attrmap.erase(id);
    return extent_protocol::OK;
  }
}

/* Update file content, difference btween put is that 
 * put just chage whole content of file it concatenate recevied buf 
 * so it reserve written data before */
// id: inum of parent dir
// name: file name of to create
// cid: child's id for return
int extent_server::update(extent_protocol::extentid_t id, std::string name,
    std::string &cid)
{
  if (contmap.find(id) == contmap.end()) {
    // No parent id
    return extent_protocol::NOENT;
  } else {
    // Get randomly generated inumber and mask to lower 32 bit 
    extent_protocol::extentid_t cid_temp = rand() & 0x7fffffff;
    // Set 32-nd bit to 1 (for indicating this is file, not directory)
    cid_temp = cid_temp | 0x80000000;
    // Check colision
    while (attrmap.find(cid_temp) != attrmap.end()) cid_temp++;

    // Using output stringstream make entry string for directory content
    std::ostringstream o_st;
    o_st << name << " " << cid_temp << "\n";
    // Add content of directory
    contmap[id] += o_st.str();

    // Update ctime and atime (do not update mtime because we did not implemented 
    // any file write functions (content is just part of attribute(set of files) */
    attrmap[id].atime = attrmap[id].ctime = time(NULL);
    attrmap[id].size = contmap[id].size();

    //Set cid's inum for returning to client
    o_st.str("");
    o_st.clear();
    o_st << cid_temp;
    cid = o_st.str();
    return extent_protocol::OK;
  }
}
