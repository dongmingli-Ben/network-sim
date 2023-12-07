#include "util.h"

// Linked list functions
int ll_get_length(LLnode* head) {
    LLnode* tmp;
    int count = 1;
    if (head == NULL)
        return 0;
    else {
        tmp = head->next;
        while (tmp != head) {
            count++;
            tmp = tmp->next;
        }
        return count;
    }
}

void* ll_peek_node(LLnode* head) {
    if (head == NULL) {
        return NULL;
    }

    return head->value; 

}

void ll_append_node(LLnode** head_ptr, void* value) {
    LLnode* prev_last_node;
    LLnode* new_node;
    LLnode* head;

    if (head_ptr == NULL) {
        return;
    }

    // Init the value pntr
    head = (*head_ptr);
    new_node = (LLnode*) malloc(sizeof(LLnode));
    new_node->value = value;

    // The list is empty, no node is currently present
    if (head == NULL) {
        (*head_ptr) = new_node;
        new_node->prev = new_node;
        new_node->next = new_node;
    } else {
        // Node exists by itself
        prev_last_node = head->prev;
        head->prev = new_node;
        prev_last_node->next = new_node;
        new_node->next = head;
        new_node->prev = prev_last_node;
    }
}

LLnode* ll_pop_node(LLnode** head_ptr) {
    LLnode* last_node;
    LLnode* new_head;
    LLnode* prev_head;

    prev_head = (*head_ptr);
    if (prev_head == NULL) {
        return NULL;
    }
    last_node = prev_head->prev;
    new_head = prev_head->next;

    // We are about to set the head ptr to nothing because there is only one
    // thing in list
    if (last_node == prev_head) {
        (*head_ptr) = NULL;
        prev_head->next = NULL;
        prev_head->prev = NULL;
        return prev_head;
    } else {
        (*head_ptr) = new_head;
        last_node->next = new_head;
        new_head->prev = last_node;

        prev_head->next = NULL;
        prev_head->prev = NULL;
        return prev_head;
    }
}

void ll_destroy_node(LLnode* node) {
    if (node->type == llt_string) {
        free((char*) node->value);
    }
    free(node);
}

// Compute the difference in usec for two timeval objects
long timeval_usecdiff(struct timeval* start_time, struct timeval* finish_time) {
    long usec;
    usec = (finish_time->tv_sec - start_time->tv_sec) * 1000000;
    usec += (finish_time->tv_usec - start_time->tv_usec);
    return usec;
}


// compute new time after adding some number of microseconds to it
void timeval_usecplus(struct timeval* curr_timeval, long usec) {
    long new_us = curr_timeval->tv_usec + usec;
    long new_s = curr_timeval->tv_sec;
    if (new_us >= 1000000) { // if the number of microseconds is >= a million, carry-over
        new_s += (new_us / 1000000);
        new_us = (new_us % 1000000);
    }
    curr_timeval->tv_usec = new_us;
    curr_timeval->tv_sec = new_s;
}


// Print out messages entered by the user
void print_cmd(Cmd* cmd) {
    fprintf(stderr, "src=%d, dst=%d, message=%s\n", cmd->src_id, cmd->dst_id,
            cmd->message);
}

// Print out frame contents
void print_frame(Frame* frame) {
    fprintf(stderr, "Frame bytes remaining %d\n", frame->remaining_msg_bytes);
    fprintf(stderr, "Frame dst_id: %d\n", frame->dst_id);
    fprintf(stderr, "Frame src_id: %d\n", frame->src_id);
    fprintf(stderr, "Frame seq_num: %d\n", frame->seq_num);
    fprintf(stderr, "Frame data: %s\n", frame->data);
}

// Print out frames in send window
void print_window(int recv_id, Host *host) {
    fprintf(stderr, "================================\n");
    fprintf(stderr, "Host %d send window %d: length %d\n", host->id, recv_id, cq_get_length_send(recv_id, host));
    for (int i = 0; i < cq_get_length_send(recv_id, host); i++) {
        print_frame(cq_get_item_send(recv_id, i, host)->frame);
    }
    fprintf(stderr, "================================\n");
}

char* convert_frame_to_char(Frame* frame) {
    char* char_buffer = malloc(sizeof(Frame));
    memcpy(char_buffer, frame, sizeof(Frame));
    return char_buffer;
}

char* cc_state_to_char(enum CCState state) {
    switch (state) {
        case cc_SS:
            return "Slow Start";
        case cc_AIMD:
            return "AIMD";
        case cc_FRFT:
            return "Fast Recovery & Fast Retransmit";
        default:
            return "Unknown";
    }
}

Frame* convert_char_to_frame(char* char_buf) {
    Frame* frame = malloc(sizeof(Frame));
    memcpy(frame, char_buf, sizeof(Frame));
    return frame;
}

// helper function for wraparound case 
int seq_num_diff(uint8_t x1, uint8_t x2) { 
    int diff = x2 - x1;
    if (diff > 127) {
        diff -= (MAX_SEQ_NUM + 1);
    }
    else if (diff < -127) {
        diff += (MAX_SEQ_NUM + 1);
    }
    return diff;
}

int recv_frame_in_window(uint8_t seq_num, uint8_t src_id, Host* host) {
    int buffer_idx = seq_num_diff(host->recv_lfr[src_id], seq_num);
    return buffer_idx > 0 && buffer_idx <= glb_sysconfig.window_size;
}

//sanity check the frame
void frame_sanity_check(Frame* frame) {
    if (frame->dst_id >= glb_num_hosts) {
        printf("Invalid frame: Wrong dest_id: %d\n", frame->dst_id); 
        exit(-1); 
    } else if (frame->src_id >= glb_num_hosts) {
        printf("Invalid frame: Wrong src_id: %d\n", frame->src_id); 
        exit(-1); 
    } else if (frame->src_id == frame->dst_id) {
        printf("Cannot send message from/to the same host: %d.\n", frame->src_id); 
        exit(-1); 
    }
}

// function to compute crc
uint8_t compute_crc8(char* char_buf){
    uint8_t generator=0x07;
    uint8_t remainder=char_buf[0];
    for(int i=1;i<64;i++){
        char byte=char_buf[i];
        for(int j=7;j>=0;j--){
            if(((0x80&remainder))){
                remainder=(remainder<<1);
                remainder|=((byte>>j)&1);
                remainder^=generator;
            }else{
                remainder=(remainder<<1);
                remainder|=((byte>>j)&1);
            }        
        }
    }
    return remainder;
}

void set_checksum(Frame *frame) {
    char *frame_buffer;
    uint8_t checksum;
    frame->checksum = 0;
    frame_buffer = convert_frame_to_char(frame);
    checksum = compute_crc8(frame_buffer);
    free(frame_buffer);
    frame->checksum = checksum;
}

uint8_t frame_valid(Frame *frame) {
    char *frame_buffer;
    frame_buffer = convert_frame_to_char(frame);
    uint8_t valid = compute_crc8(frame_buffer) == 0;
    free(frame_buffer);
    return valid;
}

void retransmit_frame(Host* host, uint8_t dst_id, uint8_t seq_num) {
    struct timeval curr_timeval;
    gettimeofday(&curr_timeval, NULL);

    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    int found_frame = 0;
    struct send_window_slot *slot;
    for (int i = 0; i < cq_get_length_send(dst_id, host); i++) {
        slot = cq_get_item_send(dst_id, i, host);
        if (slot->frame->seq_num == seq_num) {
            found_frame = 1;
            Frame *frame = malloc(sizeof(Frame));
            memcpy(frame, slot->frame, sizeof(Frame));  // copy! Otherwise the frame is freed when sending!
            ll_append_node(&host->outgoing_frames_head, frame);
            break;
        }
    }
    if (!found_frame) {
        fprintf(stderr, "Error: host %d -> %d: cannot find frame with seq num %d to retransmit!\n", host->id, dst_id, seq_num);
    }

    struct timeval* next_timeout = malloc(sizeof(struct timeval));
    memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC);

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval));
    timeval_usecplus(host->latest_timeout, 10000);   // additional 10 ms for each frame sent

    if (slot->timeout == NULL) {
        slot->timeout = next_timeout;
    } else {
        memcpy(slot->timeout, next_timeout, sizeof(struct timeval));
        free(next_timeout);
    }
    fprintf(stderr, "Host %d: resending frame %d (%d -> %d)\n", host->id, slot->frame->seq_num, slot->frame->src_id, slot->frame->dst_id);
}

struct send_window_slot* cq_get_item_send(int id, int i, Host *host) {
    int base = id * glb_sysconfig.window_size;
    int offset = (host->send_window_start[id] + i) % glb_sysconfig.window_size;
    return &(host->send_window[base + offset]);
}

Frame **cq_get_item_recv(int id, int i, Host *host) {
    int base = id * glb_sysconfig.window_size;
    int offset = (host->recv_window_start[id] + i) % glb_sysconfig.window_size;
    return &(host->recv_window[base + offset]);
}

int cq_get_length_send(int id, Host *host) {
    return host->send_window_length[id];
}

void cq_ndequeue_send(int id, int n, Host *host) {
    n = n <= host->send_window_length[id] ? n : host->send_window_length[id];
    for (int i = 0; i < n; i++) {
        struct send_window_slot *slot = cq_get_item_send(id, i, host);
        free(slot->frame);
        free(slot->timeout);
        slot->frame = NULL;
        slot->timeout = NULL;
    }
    int start = (host->send_window_start[id] + n) % glb_sysconfig.window_size;
    host->send_window_length[id] -= n;
    host->send_window_start[id] = start;
}

void cq_ndequeue_recv(int id, int n, Host *host) {
    n = n <= glb_sysconfig.window_size ? n : glb_sysconfig.window_size;
    for (int i = 0; i < n; i++) {
        Frame **frame = cq_get_item_recv(id, i, host);
        if (*frame != NULL) {
            // free(*frame);
            *frame = NULL;
        }
    }
    int start = (host->recv_window_start[id] + n) % glb_sysconfig.window_size;
    host->recv_window_start[id] = start;
}

void cq_append_item_send(int id, Frame *frame, struct timeval * timeout, Host *host) {
    if (host->send_window_length[id] == glb_sysconfig.window_size) {
        fprintf(stderr, "Send window is full. Cannot append to window.\n");
        return;
    }
    struct send_window_slot *slot = cq_get_item_send(id, host->send_window_length[id], host);
    slot->frame = frame;
    slot->timeout = timeout;
    host->send_window_length[id]++;
}

void cq_set_item_recv(int id, int i, Frame *frame, Host *host) {
    int base = id * glb_sysconfig.window_size;
    int offset = (host->recv_window_start[id] + i) % glb_sysconfig.window_size;
    host->recv_window[base + offset] = frame;
}

int min(int a, int b) {
    if (a <= b) {
        return a;
    }
    else {
        return b; 
    }
}

double min_double(double a, double b) {
    if (a <= b) {
        return a;
    }
    else {
        return b; 
    }
}

int max(int a, int b) {
    if (a >= b) {
        return a;
    }
    else {
        return b; 
    }
}

double max_double(double a, double b) {
    if (a >= b) {
        return a;
    }
    else {
        return b; 
    }
}