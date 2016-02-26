// unmarshall RPCs and hand them to extent_server

#include "rpc.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include "extent_server.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(stderr, "Usage: %s port\n", argv[0]);
    exit(1);
  }

  setvbuf(stdout, NULL, _IONBF, 0);

  rpcs server(htons(atoi(argv[1])));
  extent_server ls;

  server.reg(extent_protocol::get, &ls, &extent_server::get);
  server.reg(extent_protocol::getattr, &ls, &extent_server::getattr);
  server.reg(extent_protocol::put, &ls, &extent_server::put);
  server.reg(extent_protocol::remove, &ls, &extent_server::remove);
  server.reg(extent_protocol::update, &ls, &extent_server::update);

  while(1)
    sleep(1000);
}
