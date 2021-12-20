#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

#include "bios.h"
#include "tinyos.h"
#include "util.h"

// References
int pipe_read(void* pipe, char* buf, unsigned int size);
int pipe_write(void* pipe, const char* buf, unsigned int size);
int pipe_reader_close(void* pipe);
int pipe_writer_close(void* pipe);

#endif