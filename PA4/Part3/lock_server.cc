// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>


/* 
 * In construction of lock server,
 * we should initialize mutex for sync
 */
lock_server::lock_server():
  nacquire (0)
{
  pthread_mutex_init(&lock_mutex, NULL);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  pthread_mutex_lock(&lock_mutex);
  /* Find lock structure in managed set */
  lock_map::iterator iter = lock_man.find(lid);
 
  /* If there is no lock correspond to lock id,
   * create new lock object and put into lock manage structure 
   * (unordered map structure) 
   * Othrewise, check lock is currently occupied by other client, 
   * if occupied wait until lock relesased, on the other hand,
   * set client id of lock as clt, and set is_occupied flag to true,
   * that implies granted this lock*/
  if (iter != lock_man.end()) {
    while ((*iter).second->is_occupied) 
      pthread_cond_wait(&(iter->second->lock_cv), &lock_mutex);

    iter->second->clt = clt;
    iter->second->is_occupied = true;
  }
  else {
    lock_sync_t *new_lock = new lock_sync_t(clt);
    lock_man.insert(std::make_pair<lock_protocol::lockid_t, lock_sync_t *>(lid, new_lock));
  }

  pthread_mutex_unlock(&lock_mutex);
  return lock_protocol::OK;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  pthread_mutex_lock(&lock_mutex);

  lock_map::iterator iter = lock_man.find(lid);

  lock_protocol::status st;
  /* 
   * Error when request to release the lock 
   * which does not exist
   */
  if (iter == lock_man.end()) {
    printf("Try to releasing lock which does not exist\n");
    st = lock_protocol::RPCERR;
  }
  /* Recieved request from who does not have lock */
  else if (iter->second->clt != clt) {
    printf("Try to release lock who does not grant lock\n");
    st = lock_protocol::RPCERR;
  }
  /* Client is matched to locked one */
  else {
    /* Signal to lock syncs for relased lock */
    pthread_cond_signal(&(iter->second->lock_cv));
    iter->second->is_occupied = false;
    st = lock_protocol::OK;
  }

  pthread_mutex_unlock(&lock_mutex);
  return st;
}
