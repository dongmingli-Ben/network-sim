#include "host.h"
#include "sender.h"
#include "receiver.h"
#include "switch.h"
#include <assert.h>

void init_host(Host* host, int id) {
    host->id = id;
    host->active = 0; 
    host->round_trip_num = 0;
    host->awaiting_ack = 0; 
    host->input_cmdlist_head = NULL;
    host->incoming_frames_head = NULL; 
    host->buffered_outframes_head = NULL; 
    host->outgoing_frames_head = NULL; 
    host->send_window = calloc(glb_sysconfig.window_size * glb_num_hosts, sizeof(struct send_window_slot)); 
    for (int i = 0; i < glb_sysconfig.window_size * glb_num_hosts; i++) {
        host->send_window[i].frame = NULL;
        host->send_window[i].timeout = NULL;
    }
    host->latest_timeout = malloc(sizeof(struct timeval));
    gettimeofday(host->latest_timeout, NULL);

    // TODO: You should fill in this function as necessary to initialize variables
    host->send_window_start = calloc(glb_num_hosts, sizeof(uint8_t));
    host->send_window_length = calloc(glb_num_hosts, sizeof(uint8_t));
    memset(host->send_window_start, 0, glb_num_hosts * sizeof(uint8_t));
    memset(host->send_window_length, 0, glb_num_hosts * sizeof(uint8_t));
    host->curr_seq_num = calloc(glb_num_hosts, sizeof(uint8_t));
    memset(host->curr_seq_num, 0, glb_num_hosts * sizeof(uint8_t));

    host->recv_window = calloc(glb_num_hosts * glb_sysconfig.window_size, sizeof(Frame*)); 
    for (int i = 0; i < glb_sysconfig.window_size * glb_num_hosts; i++) {
        host->recv_window[i] = NULL;
    }

    host->recv_window_start = calloc(glb_num_hosts, sizeof(uint8_t));
    memset(host->recv_window_start, 0, glb_num_hosts * sizeof(uint8_t));
    host->recv_lfr = calloc(glb_num_hosts, sizeof(uint8_t));
    memset(host->recv_lfr, 255, glb_num_hosts * sizeof(uint8_t));  // todo: check 255 = -1

    host->recv_msg_head = calloc(glb_num_hosts, sizeof(LLnode *));
    memset(host->recv_msg_head, 0, glb_num_hosts * sizeof(LLnode *));

    // *********** PA1b ONLY ***********
    host->cc = calloc(glb_num_hosts, sizeof(CongestionControl));
    for (int i = 0; i < glb_num_hosts; i++) {
        host->cc[i].cwnd = 1.0; 
        host->cc[i].ssthresh = (double)glb_sysconfig.window_size; 
        host->cc[i].dup_acks = 0; 
        host->cc[i].state = cc_SS; 
    }
    host->dup_acks_seq_num = calloc(glb_num_hosts, sizeof(uint8_t));
    memset(host->dup_acks_seq_num, 255, glb_num_hosts);
}

void run_hosts() {
    run_senders(); 
    send_data_frames(); 
    run_receivers(); 
    send_ack_frames(); 
}