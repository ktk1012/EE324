/*
 * receive request from fuse and call methods of yfs_client
 *
 * started life as low-level example in the fuse distribution.  we
 * have to use low-level interface in order to get i-numbers.  the
 * high-level interface only gives us complete paths.
 */

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>
#include "yfs_client.h"
// For string tokenizer
#include <vector>
// for debugging
#include <iostream>


// Data type and function implementation for parsing string 
typedef std::vector<std::string> Token;

/* Parse input string str into string vetor token */
void
Tokenizer(const std::string &str, Token &tokens, const std::string &delim = "\n")
{
  std::string::size_type lastPos = str.find_first_not_of(delim, 0);

  std::string::size_type pos = str.find_first_of(delim, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos) {
    tokens.push_back(str.substr(lastPos, pos - lastPos));

    lastPos = str.find_first_not_of(delim, pos);

    pos = str.find_first_of(delim, lastPos);
  }
}
/**************************************************************/

int myid;
yfs_client *yfs;

int id() {
  return myid;
}

yfs_client::status
getattr(yfs_client::inum inum, struct stat &st)
{
  yfs_client::status ret;

  bzero(&st, sizeof(st));

  st.st_ino = inum;
  printf("getattr %016llx %d\n", inum, yfs->isfile(inum));
  if(yfs->isfile(inum)){
     yfs_client::fileinfo info;
     ret = yfs->getfile(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFREG | 0666;
     st.st_nlink = 1;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     st.st_size = info.size;
     printf("   getattr -> %llu\n", info.size);
   } else {
     yfs_client::dirinfo info;
     ret = yfs->getdir(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFDIR | 0777;
     st.st_nlink = 2;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     printf("   getattr -> %lu %lu %lu\n", info.atime, info.mtime, info.ctime);
   }
   return yfs_client::OK;
}


void
fuseserver_getattr(fuse_req_t req, fuse_ino_t ino,
          struct fuse_file_info *fi)
{
    struct stat st;
    yfs_client::inum inum = ino; // req->in.h.nodeid;
    yfs_client::status ret;

    ret = getattr(inum, st);
    if(ret != yfs_client::OK){
      fuse_reply_err(req, ENOENT);
      return;
    }
    fuse_reply_attr(req, &st, 0);
}

void
fuseserver_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 3*/

  printf("fuseserver_setattr 0x%x\n", to_set);
  if (FUSE_SET_ATTR_SIZE & to_set) {
    printf("   fuseserver_setattr set size to %zu\n", attr->st_size);
    struct stat st;

    // You fill this in
    // Don't forget to fill in st with the file's new attributes!
    
#if 0
    fuse_reply_attr(req, &st, 0);
#else
    fuse_reply_err(req, ENOSYS);
#endif
  } else {
    fuse_reply_err(req, ENOSYS);
  }
}

void
fuseserver_read(fuse_req_t req, fuse_ino_t ino, size_t size,
      off_t off, struct fuse_file_info *fi)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 3*/

  // You fill this in
#if 0
  fuse_reply_buf(req, buf, size);
#else
  fuse_reply_err(req, ENOSYS);
#endif
}

void
fuseserver_write(fuse_req_t req, fuse_ino_t ino,
  const char *buf, size_t size, off_t off,
  struct fuse_file_info *fi)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 3*/

  // You fill this in 

  // Remember that bytes_written may not just be size_t size
  // if off_t off is greater than the file's current size.
  // Make sure to return the number of actual bytes written at 
  // the extent server.


#if 0
  fuse_reply_write(req, bytes_written);
#else
  fuse_reply_err(req, ENOSYS);
#endif
}

yfs_client::status
fuseserver_createhelper(fuse_ino_t parent, const char *name,
     mode_t mode, struct fuse_entry_param *e)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 2*/
  /* This function will only be called for files, not directories,
     which would use mkdir().  You may safely ignore the "mode"
     parameter. */

  /* Create new inum with 32 bit */
  /* Todo : check colision (done; see next commands)  */
  int r = yfs_client::OK;
  yfs_client::inum cid;

  // Newly update dir entry and get unique inumber
  // (Note that colision is checked by extent server) 
  // -- In extend server it randomly generate some number
  // and check it is already in use, if not directly return ti cid
  // and new file has a inum as cid. Otherwise, server increase
  // inum until it is not in use.
  if ((r = yfs->update(parent, name, cid)) != yfs_client::OK) {
    return r;
  }

  // Convert inum to ino in entry parameter
  e->ino = cid;
  std::string buf("");

  // Now create child file
  if ((r = yfs->putcont(cid, buf)) != yfs_client::OK) {
    return r;
  }

  // Set generation and timeout to 0
  e->generation = 0;
  e->attr_timeout = 0;
  e->entry_timeout = 0;

  // Get attributes of created file
  r = getattr(e->ino, e->attr);

  return r;
  // You may choose 0 for the generation number and timeout entries
}

void
fuseserver_create(fuse_req_t req, fuse_ino_t parent, const char *name,
   mode_t mode, struct fuse_file_info *fi)
{
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    fuse_reply_create(req, &e, fi);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void fuseserver_mknod( fuse_req_t req, fuse_ino_t parent, 
    const char *name, mode_t mode, dev_t rdev ) {
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    fuse_reply_entry(req, &e);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void
fuseserver_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 2*/

  struct fuse_entry_param e;
  bool found = false;

  e.attr_timeout = 0.0;
  e.entry_timeout = 0.0;

  /* cont for received content of parent dir */
  std::string cont;
  /* For parsing */
  Token entries, entry;

  /* Get content of parent dir */
  yfs->getcont(parent, cont);

  /* As we formatted entry linewise, tokenize entry with delimeter "\n",
   * which is default argument of Tokenizer function
   * (Tokenizer is implemented above of this file)*/
  Tokenizer(cont, entries);
  
  /* Iterator for traversing tokens */
  Token::iterator iter;
  

  for (iter = entries.begin(); iter != entries.end(); iter++) {
    /* Parse each entry (name inum) for name and inum 
     * in separetely in entry vector */
    Tokenizer(*iter, entry, " ");
    /* If name and name of entry matches get attribute of this file */
    if (name == entry[0]) {
      std::istringstream st(entry[1]);
      st >> e.ino;
      getattr(e.ino, e.attr);
      found = true;
      break;
    }
    entry.clear();
  }

  // You fill this in:
  // Look up the file named `name' in the directory referred to by
  // `parent' in YFS. If the file was found, initialize e.ino and
  // e.attr appropriately.

  if (found)
    fuse_reply_entry(req, &e);
  else 
    fuse_reply_err(req, ENOENT);
}


struct dirbuf {
    char *p;
    size_t size;
};

void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_dirent_size(strlen(name));
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
          off_t off, size_t maxsize)
{
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

void
fuseserver_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
          off_t off, struct fuse_file_info *fi)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 2*/

  yfs_client::inum inum = ino; // req->in.h.nodeid;
  struct dirbuf b;
  yfs_client::dirent e;

  if(!yfs->isdir(inum)){
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  memset(&b, 0, sizeof(b));

  /* Get content and tokenize content (same logic in lookup function) */
  Token entries, entry;
  Token::iterator iter;
  std::string cont;
  yfs->getcont(inum, cont);

  Tokenizer(cont, entries);

  for (iter = entries.begin(); iter != entries.end(); iter++) {
    /* Add each parsed entry in dirbuf */
    Tokenizer(*iter, entry, " ");
    std::istringstream st(entry[1]);
    st >> e.inum;
    dirbuf_add(&b, entry[0].c_str(), e.inum);
    entry.clear();
  }


   // fill in the b data structure using dirbuf_add


   reply_buf_limited(req, b.p, b.size, off, size);
   free(b.p);
 }


void
fuseserver_open(fuse_req_t req, fuse_ino_t ino,
     struct fuse_file_info *fi)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 3*/
 
  // You fill this in
#if 0
  fuse_reply_open(req, fi);
#else
  fuse_reply_err(req, ENOSYS);
#endif
}

void
fuseserver_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
     mode_t mode)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 4*/

  struct fuse_entry_param e;

  // You fill this in
#if 0
  fuse_reply_entry(req, &e);
#else
  fuse_reply_err(req, ENOSYS);
#endif
}

void
fuseserver_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  /*IMPLEMENT THIS FUNCTION FOR LAB 4*/

  // You fill this in
  // Success:	fuse_reply_err(req, 0);
  // Not found:	fuse_reply_err(req, ENOENT);
  fuse_reply_err(req, ENOSYS);
}

void
fuseserver_statfs(fuse_req_t req)
{
  struct statvfs buf;

  printf("statfs\n");

  memset(&buf, 0, sizeof(buf));

  buf.f_namemax = 255;
  buf.f_bsize = 512;

  fuse_reply_statfs(req, &buf);
}

struct fuse_lowlevel_ops fuseserver_oper;

int
main(int argc, char *argv[])
{
  char *mountpoint = 0;
  int err = -1;
  int fd;

  setvbuf(stdout, NULL, _IONBF, 0);

  if(argc != 4){
    fprintf(stderr, "Usage: yfs_client <mountpoint> <port-extent-server> <port-lock-server>\n");
    exit(1);
  }
  mountpoint = argv[1];

  srandom(getpid());

  myid = random();

  yfs = new yfs_client(argv[2], argv[3]);

  fuseserver_oper.getattr    = fuseserver_getattr;
  fuseserver_oper.statfs     = fuseserver_statfs;
  fuseserver_oper.readdir    = fuseserver_readdir;
  fuseserver_oper.lookup     = fuseserver_lookup;
  fuseserver_oper.create     = fuseserver_create;
  fuseserver_oper.mknod      = fuseserver_mknod;
  
  /* Uncomment these 4 lines for LAB 3 */
  //fuseserver_oper.open       = fuseserver_open;
  //fuseserver_oper.read       = fuseserver_read;
  //fuseserver_oper.write      = fuseserver_write;
  //fuseserver_oper.setattr    = fuseserver_setattr;
  
  /* Uncomment these 4 lines for LAB 4 */
  //fuseserver_oper.unlink     = fuseserver_unlink;
  //fuseserver_oper.mkdir      = fuseserver_mkdir;

  const char *fuse_argv[20];
  int fuse_argc = 0;
  fuse_argv[fuse_argc++] = argv[0];
#ifdef __APPLE__
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "nolocalcaches"; // no dir entry caching
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "daemon_timeout=86400";
#endif

  // everyone can play, why not?
  //fuse_argv[fuse_argc++] = "-o";
  //fuse_argv[fuse_argc++] = "allow_other";

  fuse_argv[fuse_argc++] = mountpoint;
  fuse_argv[fuse_argc++] = "-d";

  fuse_args args = FUSE_ARGS_INIT( fuse_argc, (char **) fuse_argv );
  int foreground;
  int res = fuse_parse_cmdline( &args, &mountpoint, 0 /*multithreaded*/, 
        &foreground );
  if( res == -1 ) {
    fprintf(stderr, "fuse_parse_cmdline failed\n");
    return 0;
  }
  
  args.allocated = 0;

  fd = fuse_mount(mountpoint, &args);
  if(fd == -1){
    fprintf(stderr, "fuse_mount failed\n");
    exit(1);
  }

  struct fuse_session *se;

  se = fuse_lowlevel_new(&args, &fuseserver_oper, sizeof(fuseserver_oper),
       NULL);
  if(se == 0){
    fprintf(stderr, "fuse_lowlevel_new failed\n");
    exit(1);
  }

  struct fuse_chan *ch = fuse_kern_chan_new(fd);
  if (ch == NULL) {
    fprintf(stderr, "fuse_kern_chan_new failed\n");
    exit(1);
  }

  fuse_session_add_chan(se, ch);
  // err = fuse_session_loop_mt(se);   // FK: wheelfs does this; why?
  err = fuse_session_loop(se);
    
  fuse_session_destroy(se);
  close(fd);
  fuse_unmount(mountpoint);

  return err ? 1 : 0;
}
