// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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
  extent_protocol::attr temp;
  if (contmap.find(id) == contmap.end()) {
    temp.atime = 0;
    attrmap[id] = temp;
  }

  attrmap[id].size = buf.length();
  /* Update mtime and ctime */
  temp.ctime = temp.mtime = time(NULL);
  attrmap[id] = temp;

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

/* Remove file */
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
int extent_server::update(extent_protocol::extentid_t id, int is_file, std::string name,
    std::string &cid)
{
  if (contmap.find(id) == contmap.end()) {
    // No parent id
    return extent_protocol::NOENT;
  } else {
    std::string cont = contmap[id];
    // Check corresponding name is already existing
    if (cont.find(name) != std::string::npos) {
        // If already exists, find conrresponding inum
        size_t a = cont.find(name);
        size_t b = cont.find(" ", a);
        size_t c = cont.find("\n", b);
        // Note that each entry is written in line by line with filename inum\n
        // a is the starting position of file name and b is end of file name
        // and c is end of this line so string between b and c is desired inum
        cid = cont.substr(b + 1, c);
    } else {
      // if there is no existing file, get new inum 
      // Get randomly generated inumber and mask to lower 32 bit 
       extent_protocol::extentid_t cid_temp = rand() & 0x7fffffff;
      // Set 32-nd bit to 1 (for indicating this is file, not directory)
      // If appending object is file set MSB to 1
      if (is_file)
          cid_temp = cid_temp | 0x80000000;
      else
          cid_temp = cid_temp & 0x7fffffff;
      // Check colision
      while (attrmap.find(cid_temp) != attrmap.end()) cid_temp++;


      // Using output stringstream make entry string for directory content
      std::ostringstream o_st;
      o_st << name << " " << cid_temp << "\n";
      // Add content of directory
      contmap[id] += o_st.str();
      
      //Set cid's inum for returning to client
      o_st.str("");
      o_st.clear();
      o_st << cid_temp;
      cid = o_st.str();
    }
    // Update ctime and atime (do not update mtime because we did not implemented 
    // any file write functions (content is just part of attribute(set of files) */
    attrmap[id].mtime = attrmap[id].atime = attrmap[id].ctime = time(NULL);
    attrmap[id].size = contmap[id].size();


    return extent_protocol::OK;
  }
}

int
extent_server::read(extent_protocol::extentid_t id, size_t size, 
    unsigned long offset, std::string &buf)
{
  if (contmap.find(id) != contmap.end()) {
    if (offset > contmap[id].size()) {
      return extent_protocol::IOERR;
    } else {
      size_t read_avail = contmap[id].size() - (int)offset;
      size_t read_upto = read_avail > size ? size : read_avail;
      buf = contmap[id].substr(offset, read_upto);
      return extent_protocol::OK;
    }
  } else {
    return extent_protocol::NOENT;
  }
}

int
extent_server::write(extent_protocol::extentid_t id, std::string buf,
    unsigned long offset, int &nwritten)
{
  printf("%llx\n", id);
  if (contmap.find(id) != contmap.end()) {
    nwritten = buf.size();
    size_t content_size = contmap[id].size();
    if (content_size < offset + buf.size()) 
      contmap[id].resize(offset + buf.size());
    contmap[id].replace(offset,  nwritten, buf);
    nwritten = (int)buf.size();
    attrmap[id].mtime = time(NULL);
    attrmap[id].size = contmap[id].size();
    return extent_protocol::OK;
  } else {
    return extent_protocol::NOENT;
  }
}

int
extent_server::set_size(extent_protocol::extentid_t id, size_t size, int &)
{
  if (contmap.find(id) != contmap.end()) {
    if (size) {
      size_t before = contmap[id].size();
      if (size < before) {
        contmap[id] = contmap[id].substr(size);
      } else {
        contmap[id].resize(size, ' ');
      }
    } else {
      contmap[id] = "";
    }
    attrmap[id].mtime = time(NULL);
    return extent_protocol::OK;
  } else {
    return extent_protocol::NOENT;
  }
}

int 
extent_server::unlink(extent_protocol::extentid_t id, std::string name, std::string &buf)
{
  size_t a, b;  //For parsing child's name
  if (contmap.find(id) != contmap.end()) {
    std::string cont = contmap[id];
    if ((a = cont.find(name)) == std::string::npos)
        return extent_protocol::NOENT;
    b = cont.find("\n", a);
    contmap[id] = cont.substr(0, a) + cont.substr(b + 1);
    // Find child's ino
    a = cont.find(" ", a);
    buf = cont.substr(a + 1, b);
    attrmap[id].size = cont.size();

    // Update atime, ctime, and mtime
    extent_protocol::attr parent_attr = attrmap[id];
    parent_attr.atime = parent_attr.ctime = parent_attr.mtime = time(NULL);
    attrmap[id] = parent_attr;

    return extent_protocol::OK;
  } else {
    return extent_protocol::NOENT;
  }
}
