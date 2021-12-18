#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_socket.h"


Fid_t sys_Socket(port_t port) {
    if(port < 0 || port > MAX_PORT)
        return NOFILE;

    FCB* fcb;
    Fid_t fid;

    if(FCB_reserve(1, &fid, &fcb) != 1)
        return NOFILE;

	//..

    return fid;
}

int sys_Listen(Fid_t sock) {
	
	FCB* fcb = get_fcb(sock); // Get FCB from Fid table
	socketCB* socket_cb = (socketCB*)fcb->streamobj; // Get socket_cb from streamobj of FCB
	
	// Check if fcb is invalid 
	if(fcb == NULL || socket_cb->port == NOPORT)
		return -1;

	// Check if socket has already been initialized
	if(socket_cb->type != SOCKET_UNBOUND)
		return -1;

	// Port of socket
	port_t port = socket_cb->port;

	// Check if port is bound by another listener 
	if(PORT_MAP[port] == NULL && PORT_MAP[port]->type == SOCKET_LISTENER) // COMMENT: Μπορεί να μην χρειάζεται
		return -1;

	// Install socket to PORT_MAP and mark it as listener
	socket_cb->type = SOCKET_LISTENER;
	rlnode_init(&socket_cb->listener_s.queue, NULL);
	socket_cb->listener_s.req_available = COND_INIT;
	PORT_MAP[port] = socket_cb;

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}

