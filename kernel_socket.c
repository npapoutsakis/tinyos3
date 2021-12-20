#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_socket.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"

//static int counter=0;

int socket_read(void* this, char *buf, unsigned int size){
	
	socketCB* socket = (socketCB*) this;

	if(socket->type != SOCKET_PEER || socket->peer_s.read_pipe == NULL)
		return NOFILE;

	return pipe_read(socket->peer_s.read_pipe, buf, size);
}

int socket_write(void* this, const char *buf, unsigned int size){
	
	socketCB* socket = (socketCB*) this;

	if(socket->type != SOCKET_PEER || socket->peer_s.write_pipe == NULL)
		return NOFILE;

	return pipe_write(socket->peer_s.write_pipe, buf, size);
}

int socket_close(void* this){
	
	socketCB* socket = (socketCB*) this;

	switch (socket->type){
		case SOCKET_LISTENER:
			PORT_MAP[socket->port] = NULL;
			kernel_broadcast(&socket->listener_s.req_available);
			break;

		case SOCKET_PEER:
			if(!pipe_reader_close(socket->peer_s.read_pipe) || !pipe_writer_close(socket->peer_s.write_pipe))
				return NOFILE;
			break;

		case SOCKET_UNBOUND:
		default:
			socket = NULL;
			break;
	}	

	return 0;
}

file_ops socket_ops = {
  .Open = NULL,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close
};

Fid_t sys_Socket(port_t port) {
    if(port < 0 || port > MAX_PORT){
        return NOFILE;
	}

	Fid_t fid[1];
	FCB* fcb[1];

	// Reserve R and W FCB's with given FID's at currporc's FIDT
	if(FCB_reserve(1, fid, fcb) != 1){
		return NOFILE;
	}

	socketCB* socket = xmalloc(sizeof(socketCB));

	fcb[0]->streamobj = socket;
	fcb[0]->streamfunc = &socket_ops;

	socket->port = port;
	socket->fcb = fcb[0];
	socket->refcount = 0;
	socket->type = SOCKET_UNBOUND;

    return fid[0];
}

int sys_Listen(Fid_t sock) {
	
	FCB* fcb = get_fcb(sock); // Get FCB from Fid table
	
	// Verify FCB valid and refers to a socket
	if(fcb == NULL || fcb->streamfunc != &socket_ops)
		return NOFILE;

	socketCB* socket_cb = (socketCB*)fcb->streamobj; // Get socket_cb from streamobj of FCB

	// Socket must be bound to a port
	if(socket_cb->port == NOPORT)
		return NOFILE;

	// Check if socket has already been initialized
	if(socket_cb->type != SOCKET_UNBOUND)
		return NOFILE;

	// Port of socket
	port_t port = socket_cb->port;

	// Check if port is bound by another listener 
	if(PORT_MAP[port] != NULL && PORT_MAP[port]->type == SOCKET_LISTENER) // COMMENT: Μπορεί να μην χρειάζεται
		return NOFILE;

	// Install socket to PORT_MAP and mark it as listener
	socket_cb->type = SOCKET_LISTENER;
	rlnode_init(&socket_cb->listener_s.queue, NULL);
	socket_cb->listener_s.req_available = COND_INIT;
	PORT_MAP[port] = socket_cb;

	return 0;
}

Fid_t sys_Accept(Fid_t lsock) {
	
	FCB* fcb = get_fcb(lsock); // Get FCB from Fid table
	
	// Verify FCB valid and refers to a socket
	if(fcb == NULL || fcb->streamfunc != &socket_ops)
		return NOFILE;

	socketCB* socket_cb = (socketCB*)fcb->streamobj; // Get socket_cb from streamobj of FCB

	// Socket must be bound to a port
	if(socket_cb->port == NOPORT)
		return NOFILE;

	// Socket must be listener socket
	if(socket_cb->type != SOCKET_LISTENER)
		return NOFILE;

	// if(cb->type == PEER) return -1;							
	// 	if((PORT_MAP[cb->port])->type != LISTENER) return -1;	

	// 	socketCB* l = PORT_MAP[cb->port];

	// Increase refcount, Comment: here or Connect?
	socket_cb->refcount++;

	// While reqest queue is empty wait for signal
	while (is_rlist_empty(&socket_cb->listener_s.queue)) 
		kernel_wait(&socket_cb->listener_s.req_available, SCHED_USER);

	// Check if port is still valid
	if(socket_cb->port == NOPORT)
		return NOFILE;

	// Create new peer socket to connect with socket that sent the connect request
	Fid_t peer1_fid = sys_Socket(socket_cb->port);
	if(peer1_fid == NOFILE)
		return NOFILE;

	FCB* peer1_FCB = get_fcb(peer1_fid);
	socketCB* peer1 = (socketCB*) peer1_FCB;

	if(peer1 == NULL)
		return NOFILE;


	connection_request* req = (connection_request*) rlist_pop_front(&socket_cb->listener_s.queue)->obj;	
	// Fid_t peer2Id = (socketCB*) rlist_pop_front(&socket_cb->listener_s.queue)->obj;	

	req->admitted = 1;

	// socketCB* peer2 = req->peer; // Comment: if FCB's not needed during pipe init then uncomment this line
	Fid_t peer2_fid = req->peer_fid;
	FCB* peer2_FCB = get_fcb(peer2_fid);
	socketCB* peer2 = (socketCB*) peer2_FCB;

	if(peer2 == NULL)
		return NOFILE;

	// Create pipes to connect sockets
	// Comment: maybe create new method init_pipe for less code

	pipeCB* pipe1 = (pipeCB*) xmalloc(sizeof(pipeCB));
	pipe1->reader = peer2_FCB;
	pipe1->writer = peer1_FCB;
	pipe1->pipe_ends->read = peer2_fid;
	pipe1->pipe_ends->write = peer1_fid;
	pipe1->r_position = 0;
	pipe1->w_position = 0;
	pipe1->has_data = COND_INIT;
	pipe1->has_space = COND_INIT;
	
	pipeCB* pipe2 = (pipeCB*) xmalloc(sizeof(pipeCB));
	pipe2->reader = peer1_FCB;
	pipe2->writer = peer2_FCB;
	pipe2->pipe_ends->read = peer1_fid;
	pipe2->pipe_ends->write = peer2_fid;
	pipe2->r_position = 0;
	pipe2->w_position = 0;
	pipe2->has_data = COND_INIT;
	pipe2->has_space = COND_INIT;
	
	peer1->type = SOCKET_PEER;
	peer1->peer_s.read_pipe = pipe2;
	peer1->peer_s.write_pipe = pipe1;
	
	peer2->type = SOCKET_PEER;
	peer2->peer_s.read_pipe = pipe1;
	peer2->peer_s.write_pipe = pipe2;
	
	// Signal Connect side
	kernel_broadcast(&req->connected_cv);

	// Decrease refcount of initial socket
	socket_cb->refcount--;	

	return peer1_fid;

}

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}

int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}