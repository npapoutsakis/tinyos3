
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"


int pipe_read_off(void* pipe_cb, char* buf, unsigned int size){
	return -1;
}
int pipe_write_off(void* pipe_cb, const char* buf, unsigned int size){
	return -1;
}
int pipe_read(void* pipe, char* buf, unsigned int size);
int pipe_write(void* pipe, const char* buf, unsigned int size);
int pipe_reader_close(void* pipe);
int pipe_writer_close(void* pipe);

static file_ops W = {
    .Open = NULL,
    .Read = pipe_read_off,
    .Write = pipe_write,
    .Close = pipe_writer_close
};

static file_ops R = {
    .Open = NULL,
    .Read = pipe_read,
    .Write = pipe_write_off,
    .Close = pipe_reader_close
};

int sys_Pipe(pipe_t* pipe)
{	
	pipeCB* pipe_cb = xmalloc(sizeof(pipeCB));

	Fid_t fid[2];
	FCB* fcb[2];
	if(FCB_reserve(2, fid, fcb)!=1) return -1;
	
	pipe_cb->has_space = COND_INIT;
	pipe_cb->has_data = COND_INIT;
	pipe_cb->pipe_ends = pipe;	
	pipe_cb->r_position = PIPE_BUFFER_SIZE-1;
	pipe_cb->w_position = 0;
	pipe_cb->reader = fcb[0];
	pipe_cb->writer = fcb[1];
	pipe_cb->pipe_ends->read = fid[0];
	pipe_cb->pipe_ends->write = fid[1];
	pipe_cb->reader->streamfunc = &R;
	pipe_cb->writer->streamfunc = &W;
	pipe_cb->reader->streamobj = pipe_cb;
	pipe_cb->writer->streamobj = pipe_cb;
	return 0;

}

int pipe_read(void* pipe, char* buf, unsigned int size){
	pipeCB* pipe_in_use = (pipeCB*) pipe;
	//if reader is closed return -1
	if(pipe_in_use->reader==NULL){
		return -1;
	}
	//if there is no data to read and writer is closed return 0 (EOF)
	if(pipe_in_use->r_position==pipe_in_use->w_position && pipe_in_use->writer==NULL){
		return 0;
	}

	int i;
	for( i=0; i<size; i++){
		pipe_in_use->r_position = (pipe_in_use->r_position + 1) % PIPE_BUFFER_SIZE;
		//if all data is read wait until writer writes more data
		while(pipe_in_use->r_position==pipe_in_use->w_position && pipe_in_use->writer!=NULL){
			kernel_broadcast(& pipe_in_use->has_space);
			kernel_wait(& pipe_in_use->has_data, SCHED_PIPE);
		}
		if(size==PIPE_BUFFER_SIZE) break;
		//if all data is read and write end is closed then return i
		if(pipe_in_use->r_position==pipe_in_use->w_position && pipe_in_use->writer==NULL){
			return i;
		}
		buf[i] = pipe_in_use->BUFFER[pipe_in_use->r_position];
	}
	kernel_broadcast(& pipe_in_use->has_space);
	return i;
}

int pipe_write(void* pipe, const char* buf, unsigned int size){
	pipeCB* pipe_in_use = (pipeCB*) pipe;
	//if write and read end are closed return -1
	if(pipe_in_use->writer==NULL || pipe_in_use->reader==NULL){
		return -1;
	}
	
	int i;
	for(i=0; i<size; i++){
		//if read end is open and next block of buffer has not been read sleep until its read
		while(pipe_in_use->reader!=NULL && pipe_in_use->r_position==(pipe_in_use->w_position+1)%PIPE_BUFFER_SIZE){
			kernel_broadcast(& pipe_in_use->has_data);
			kernel_wait(& pipe_in_use->has_space, SCHED_PIPE);
		}
		//if next block in buffer hasnt been read and read end is closed return i
		if(pipe_in_use->r_position==(pipe_in_use->w_position+1)%PIPE_BUFFER_SIZE && pipe_in_use->reader==NULL){
			return i;
		}
		pipe_in_use->BUFFER[pipe_in_use->w_position] = buf[i];
		pipe_in_use->w_position = (pipe_in_use->w_position + 1) % PIPE_BUFFER_SIZE;
	}
	kernel_broadcast(& pipe_in_use->has_data);
	return i;
}

int pipe_reader_close(void* pipe){
	pipeCB* pipe_in_use = (pipeCB*) pipe;
	pipe_in_use->reader = NULL;
	if(pipe_in_use->writer == NULL){
		free(pipe_in_use);
	}else{
		kernel_broadcast(& pipe_in_use->has_space);
	}
	return 0;
}

int pipe_writer_close(void* pipe){
	pipeCB* pipe_in_use = (pipeCB*) pipe;
	pipe_in_use->writer = NULL;
	if(pipe_in_use->reader == NULL){
		free(pipe_in_use);
	}else{
		kernel_broadcast(& pipe_in_use->has_data);
	}
	return 0;
}