#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"

// Write FCB
static file_ops W = {
    .Open = NULL,
    .Read = NULL, // Write end doesn't need read implementation
    .Write = pipe_write,
    .Close = pipe_writer_close
};

// Read FCB
static file_ops R = {
    .Open = NULL,
    .Read = pipe_read,
    .Write = NULL, // Read end doesn't need read implementation
    .Close = pipe_reader_close
};

// Initialize new Pipe
int sys_Pipe(pipe_t* pipe){	
	pipeCB* pipe_cb = xmalloc(sizeof(pipeCB));
	
	Fid_t fid[2];
	FCB* fcb[2];

	// Reserve R and W FCB's with given FID's at currporc's FIDT
	if(FCB_reserve(2, fid, fcb) != 1) 
		return -1;
	
	// Initialize Pipe_CB
	pipe_cb->pipe_ends = pipe;	
	pipe_cb->pipe_ends->read = fid[0];
	pipe_cb->pipe_ends->write = fid[1];
	pipe_cb->reader = fcb[0];
	pipe_cb->writer = fcb[1];
	pipe_cb->has_space = COND_INIT;
	pipe_cb->has_data = COND_INIT;
	pipe_cb->r_position = PIPE_BUFFER_SIZE-1;
	pipe_cb->w_position = 0;
	pipe_cb->reader->streamobj = pipe_cb;
	pipe_cb->writer->streamobj = pipe_cb;
	pipe_cb->reader->streamfunc = &R;
	pipe_cb->writer->streamfunc = &W;

	return 0;
}

int pipe_read(void* pipe, char* buf, unsigned int size){
	pipeCB* pipe_in_use = (pipeCB*) pipe;
	
	// if reader is closed return -1
	if(pipe_in_use->reader==NULL)
		return -1;
	
	// if there is no data to read from buffer and writer is closed return 0 (EOF)
	if(pipe_in_use->r_position == pipe_in_use->w_position && pipe_in_use->writer==NULL)
		return 0;
	
	int i;
	for(i = 0; i < size; i++){
		// Increase r_position
		pipe_in_use->r_position = (pipe_in_use->r_position + 1) % PIPE_BUFFER_SIZE; // % PIPE_BUFFER_SIZE -> Circular Buffer

		//if all data is read wait until writer writes more data
		while(pipe_in_use->r_position == pipe_in_use->w_position && pipe_in_use->writer!=NULL){
			kernel_broadcast(&pipe_in_use->has_space);
			kernel_wait(&pipe_in_use->has_data, SCHED_PIPE);
		}

		// All contents of buffer was read, wait for writer to write new data at buffer
		if(size == PIPE_BUFFER_SIZE) 
			break;

		//if all data is read and write end is closed then return i
		if(pipe_in_use->r_position == pipe_in_use->w_position && pipe_in_use->writer == NULL)
			return i;
		
		// Read data from pipe buffer
		buf[i] = pipe_in_use->BUFFER[pipe_in_use->r_position];
	}

	// Wake up writer end
	kernel_broadcast(&pipe_in_use->has_space);
	return i;
}

int pipe_write(void* pipe, const char* buf, unsigned int size){
	pipeCB* pipe_in_use = (pipeCB*) pipe;
	
	// if write or read end are closed return -1
	if(pipe_in_use->writer==NULL || pipe_in_use->reader==NULL)
		return -1;
	
	int i;
	for(i = 0; i < size; i++){
		// if read end is open and next block of buffer has not been read sleep until its read
		while(pipe_in_use->reader != NULL && pipe_in_use->r_position == (pipe_in_use->w_position + 1) % PIPE_BUFFER_SIZE){
			kernel_broadcast(&pipe_in_use->has_data);
			kernel_wait(&pipe_in_use->has_space, SCHED_PIPE);
		}

		// if read end is closed return i
		if(pipe_in_use->reader == NULL)  
			return i;
		
		// Write data to pipe buffer and increase w_position
		pipe_in_use->BUFFER[pipe_in_use->w_position] = buf[i];
		pipe_in_use->w_position = (pipe_in_use->w_position + 1) % PIPE_BUFFER_SIZE;
	}

	// Wake up reader end
	kernel_broadcast(& pipe_in_use->has_data);
	return i;
}

// Close reader end
int pipe_reader_close(void* pipe){

	if(pipe == NULL)
		return NOFILE;

	pipeCB* pipe_in_use = (pipeCB*) pipe;
	pipe_in_use->reader = NULL;

	if(pipe_in_use->writer == NULL)
		// If both ends are closed, free pipe
		free(pipe_in_use);
	else
		// else wake up write end
		kernel_broadcast(&pipe_in_use->has_space);
	
	return 0;
}

// Close writer end
int pipe_writer_close(void* pipe){

	if(pipe == NULL)
		return NOFILE;

	pipeCB* pipe_in_use = (pipeCB*) pipe;
	pipe_in_use->writer = NULL;
	
	if(pipe_in_use->reader == NULL)
		// If both ends are closed, free pipe
		free(pipe_in_use);
	else
		// else wake up read end
		kernel_broadcast(&pipe_in_use->has_data);

	return 0;
}