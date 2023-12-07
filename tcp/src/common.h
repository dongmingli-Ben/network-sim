#ifndef __COMMON_H__
#define __COMMON_H__

#include <time.h>
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

//---------------------------------
//   DO NOT CHANGE THIS SECTION
//---------------------------------
#define MAX_COMMAND_LENGTH 16
#define MAX_FRAME_SIZE 64
#define MAX_SEQ_NUM 255
#define TIMEOUT_INTERVAL_USEC 100000

typedef unsigned char uchar_t;

enum LLtype { llt_string, llt_frame, llt_integer, llt_head } LLtype;
enum CCState { cc_SS, cc_AIMD, cc_FRFT} CCState;
// System configuration information
struct SysConfig_t {
    int window_size; 
    double corrupt_prob;
    int recv_accept_rate; 
    
    //TESTING FIELDS
    int test_case_id; 
    int host_send_cc_id; 
    int host_recv_cc_id; 
    char config_path[1500]; 
};
typedef struct SysConfig_t SysConfig;

// Struct for the IO reader
struct Reader_t {
    fd_set read_fds; 
    fd_set master_fds;
    int fd_max;
    struct timeval timeout; 
}; 
typedef struct Reader_t Reader; 

// Linked list information
struct LLnode_t {
    struct LLnode_t* prev;
    struct LLnode_t* next;
    enum LLtype type;

    void* value;
};
typedef struct LLnode_t LLnode;

// Command line input information
struct Cmd_t {
    uint16_t src_id;
    uint16_t dst_id;
    char* message;
};
typedef struct Cmd_t Cmd;

struct IngressPort_t {
    LLnode* buffer_queue_head;
    int buffer_queue_size; 
}; 
typedef struct IngressPort_t Ingress; 

struct EgressPort_t {
    LLnode* buffer_queue_head; 
    int buffer_queue_size; 
}; 
typedef struct EgressPort_t Egress; 


//---------------------------------------
// DO NOT CHANGE ANYTHING ABOVE THIS LINE
//---------------------------------------

//****************************************
// YOU CAN EDIT/ADD FIELDS BELOW THIS LINE
//****************************************


/* TODO: Add fields to your frame that help you achieve the desired functionality.
   Please note that the first four fields of your frame are fixed. 
   DO NOT CHANGE their order or names or data types.

   In the rest portions of the frame, you can add fields and change FRAME_PAYLOAD_SIZE as
   you want. However, MAX_FRAME_SIZE is fixed (i.e. 64 bytes).
*/

#define FRAME_PAYLOAD_SIZE 56
struct __attribute__((packed)) Frame_t {
    /* DO NOT CHANGE:
        1) remaining_msg_bytes
        2) dst_id
        3) src_id
        4) seq_num

        5) data (you can change payload size)
    */   
    uint16_t remaining_msg_bytes; 
    uint8_t dst_id; 
    uint8_t src_id; 
    uint8_t seq_num; 

    uint8_t is_ack;
    uint8_t length;

    char data[FRAME_PAYLOAD_SIZE]; 

    uint8_t checksum; 
};
typedef struct Frame_t Frame; 

// Struct that holds the timeout of the frame and the frame that was sent out
struct send_window_slot {
    Frame* frame;
    struct timeval* timeout;
}; 

// PA1b ONLY
struct CongestionControl_t {
    double cwnd; 
    double ssthresh; 
    uint8_t dup_acks; 
    enum CCState state;
}; 
typedef struct CongestionControl_t CongestionControl;

//Host data structure
struct Host_t {
    /* DO NOT CHANGE:
        1) id
        2) active
        3) input_cmdlist_head
        4) incoming_frames_head;
        5) buffered_outframes_head
        6) outgoing_frames_head
        7) send_window
        8) latest_timeout
        9) Any of the fields in cc and cc itself
    */   
    int id;
    int awaiting_ack; 
    int active;  
    int round_trip_num; 
    int csv_out; 

    LLnode* input_cmdlist_head;
    LLnode* incoming_frames_head; 

    LLnode* buffered_outframes_head; 
    LLnode* outgoing_frames_head;

    struct send_window_slot* send_window; // all slots for sliding windows (one for each receiver)
    struct timeval* latest_timeout; 

    // array of start and end of windows (without base offset).
    // Each of the window is implemented as a circular queue.
    // There should be at most 256 windows.
    uint8_t* send_window_start;
    uint8_t* send_window_length;

    Frame **recv_window;
    uint8_t* recv_window_start;  // there is no end for receiver window. The receiver window is always the RWS.
    uint8_t *recv_lfr;  // LAF can be deduced from LFR.

    LLnode **recv_msg_head;  // head of the list of messages received by the receiver.

    // the next sequence number to be used
    // for each receiver
    uint8_t *curr_seq_num;

    CongestionControl* cc; //PA1b ONLY
    // seq num for latest duplicated ACKs (for CC)
    uint8_t *dup_acks_seq_num;
};
typedef struct Host_t Host;
/*
Global variables declared below.
DO NOT CHANGE:
  1) glb_hosts_array
  2) glb_ingress_ports_array
  3) glb_egress_ports_array; 
  4) glb_io_reader
  5) glb_exit_main
  6) glb_num_hosts
  7) INGRESS_PORT_QUEUE_CAPACITY; 
  8) glb_sysconfig
  9) cc_datafile
*/

Host* glb_hosts_array;
Ingress* glb_ingress_ports_array; 
Egress* glb_egress_ports_array; 
Reader* glb_io_reader; 

int glb_exit_main; 
int glb_num_hosts; 
int INGRESS_PORT_QUEUE_CAPACITY; 
SysConfig glb_sysconfig;

FILE *cc_diagnostics; 
#endif
