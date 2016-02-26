// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"
/* For managing lock structure use unordered map STL */
#ifdef __GNUC__
#include <tr1/unordered_map>
using namespace std::tr1;
#else
#include <unordered_map>
#endif


/* 
 * sturcut type of synchronization of each lock
 * this lock structure is uniquely assinged to 
 * corresponding lockid_t (and managed by unordered map)
 */
typedef struct lock_sync_t lock_sync_t;
/*
 * STL unordered map container that manage lock id / lock object pairs
 */
typedef unordered_map<lock_protocol::lockid_t, lock_sync_t *> lock_map;

struct lock_sync_t {
  /* Client number who currently acquired lock */
  int clt;
  /* Flag that client currently occupying this lock */
  bool is_occupied;
  /* Condition variable for this lock */
  pthread_cond_t lock_cv;

  lock_sync_t (int x): is_occupied(true) {
    pthread_cond_init(&lock_cv, NULL);
    clt = x;
  }

  ~lock_sync_t () {
    pthread_cond_destroy(&lock_cv);
  }
};

/* 
 * Lock server object, it contains mutex for mutual exclusion,
 * and contains lock_map for lock_management
 */
class lock_server {

 protected:
  int nacquire;
  pthread_mutex_t lock_mutex;
  /* Lock synchronization management */
  lock_map lock_man;

 public:
  lock_server();
  ~lock_server() {};
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);
};

#endif 







