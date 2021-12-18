#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "bios.h"
#include "tinyos.h"
#include "util.h"
#include "kernel_streams.h"

typedef enum {
    SOCKET_LISTENER,
    SOCKET_UNBOUND,
    SOCKET_PEER
} Socket_type;

// Socket Control Block
typedef struct socket_control_block {
    uint refcount;
    FCB* fcb;
    Socket_type type;
    port_t port;

    union kernel_socket {
        listener_socket listener_s;
        unbound_socket unbound_s;
        peer_socket peer_s;
    };
    
} socketCB;

typedef struct listener_socket {
    rlnode queue;
    CondVar req_available;
} listener_socket;

typedef struct unbound_socket {
    rlnode unbound_socket;
} unbound_socket;

typedef struct peer_socket {
    socketCB* peer;
    pipeCB* write_pipe;
    pipeCB* read_pipe;
} peer_socket;

typedef struct connection_request {
    int admitted;
    socketCB* peer;
    CondVar connected_cv;
    rlnode queue_node;
} connection_request;

#endif