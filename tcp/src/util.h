#ifndef __UTIL_H__
#define __UTIL_H__

#include "common.h"

#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

// Linked list functions
int ll_get_length(LLnode*);
void* ll_peek_node(LLnode*); 
void ll_append_node(LLnode**, void*);
LLnode* ll_pop_node(LLnode**);
void ll_destroy_node(LLnode*);

// circular queue functions

// @param id: window id
// @param i: index of the element in the queue
// @param host: pointer to the host struct
struct send_window_slot* cq_get_item_send(int id, int i, Host* host);
Frame** cq_get_item_recv(int id, int i, Host* host);

// @param id: window id
// @param host: pointer to the host struct
int cq_get_length_send(int id, Host* host);

// Dequeue n times. If n > length, dequeue all.
void cq_ndequeue_send(int id, int n, Host *host);
void cq_ndequeue_recv(int id, int n, Host *host);
void cq_append_item_send(int id, Frame *frame, struct timeval *timeout, Host *host);
void cq_set_item_recv(int id, int i, Frame *frame, Host *host);

// Print functions
void print_cmd(Cmd*);
void print_frame(Frame*); 
void print_window(int, Host*);

// Time functions
void timeval_usecplus(struct timeval*, long); 
long timeval_usecdiff(struct timeval*, struct timeval*);

char* convert_frame_to_char(Frame*);
Frame* convert_char_to_frame(char*);
uint8_t frame_valid(Frame*);
void set_checksum(Frame*);

int recv_frame_in_window(uint8_t seq_num, uint8_t src_id, Host* host);

void retransmit_frame(Host*, uint8_t dst_id, uint8_t seq_num);

void frame_sanity_check(Frame*); 
char* cc_state_to_char(enum CCState);
int seq_num_diff(uint8_t, uint8_t); 

// CRC-8 Computation
uint8_t compute_crc8(char* frameChar);

int min(int, int); 
double min_double(double, double); 
int max(int, int); 
double max_double(double, double); 
#endif
