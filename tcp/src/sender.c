#include "host.h"
#include <assert.h>
#include "switch.h"

struct timeval* host_get_next_expiring_timeval(Host* host) {
    // TODO: You should fill in this function so that it returns the 
    // timeval when next timeout should occur
    // 
    // 1) Check your send_window for the timeouts of the frames. 
    // 2) Return the timeout of a single frame. 
    // HINT: It's not the frame with the furtherst/latest timeout. 
    struct timeval *next_timeout = NULL;
    for (int recv_id = 0; recv_id < glb_num_hosts; recv_id++) {
        if (recv_id == host->id) continue;
        for (int i = 0; i < cq_get_length_send(recv_id, host); i++) {
            struct timeval *timeout = cq_get_item_send(recv_id, i, host)->timeout;
            if (timeout == NULL) continue;
            if (next_timeout == NULL || timeval_usecdiff(next_timeout, timeout) < 0) {
                next_timeout = timeout;
            }
        }
    }
    return next_timeout;  // this is not a copy
}

void handle_incoming_acks(Host* host, struct timeval curr_timeval) {

    // Num of acks received from each receiver
    uint8_t num_acks_received[glb_num_hosts]; 
    memset(num_acks_received, 0, glb_num_hosts); 

    // Num of duplicate acks received from each receiver this rtt
    uint8_t num_dup_acks_for_this_rtt[glb_num_hosts];     //PA1b
    memset(num_dup_acks_for_this_rtt, 0, glb_num_hosts); 

    // TODO: Suggested steps for handling incoming ACKs
    //    1) Dequeue the ACK frame from host->incoming_frames_head
    //    2) Compute CRC of the ack frame to know whether it is corrupted
    //    3) Check if the ack is valid i.e. within the window slot 
    //    4) Implement logic as per sliding window protocol to track ACK for what frame is expected,
    //       and what to do when ACK for expected frame is received
    LLnode *node = host->incoming_frames_head;
    int length = ll_get_length(node);
    // fprintf(stderr, "before Host %d handling acks\n", host->id);
    // print_window(1, &glb_hosts_array[0]);
    for (int i = 0; i < length && node != NULL; i++) {
        Frame *frame = convert_char_to_frame((char *) node->value);
        if (!frame_valid(frame)) {
            LLnode *corrupt_node;
            if (node == host->incoming_frames_head) {
                corrupt_node = ll_pop_node(&host->incoming_frames_head);
                node = host->incoming_frames_head;
            } else {
                corrupt_node = ll_pop_node(&node);
            }
            ll_destroy_node(corrupt_node);
            fprintf(stderr, "Host %d: dropping a corrupted frame in handle_incoming_acks\n", host->id);
            continue;
        }
        if (!frame->is_ack) {
            node = node->next;
            continue;
        }
        LLnode *ack_node;
        if (node == host->incoming_frames_head) {
            ack_node = ll_pop_node(&host->incoming_frames_head);
            node = host->incoming_frames_head;
        } else {
            ack_node = ll_pop_node(&node);
        }
        uint8_t seq_num = frame->seq_num;
        uint8_t src_id = frame->src_id;
        ll_destroy_node(ack_node);

        fprintf(stderr, "Host %d: received ACK %d from host %d\n", host->id, seq_num, src_id);
        // print_window(src_id, host);

        num_acks_received[src_id]++;
        // update dup ACKs
        if (seq_num_diff(host->dup_acks_seq_num[src_id], seq_num) > 0) {
            fprintf(stderr, "Host %d: update latest ACK seq num to %d\n", host->id, seq_num);
            host->dup_acks_seq_num[src_id] = seq_num;
            host->cc[src_id].dup_acks = 0;
            num_dup_acks_for_this_rtt[src_id] = 0;
        } else if (host->dup_acks_seq_num[src_id] == seq_num) {
            num_dup_acks_for_this_rtt[src_id]++;
            host->cc[src_id].dup_acks++;
            if (host->cc[src_id].state == cc_FRFT) {
                fprintf(stderr, "Host %d: dup ACK in FRFT stage\n", host->id);
                host->cc[src_id].cwnd++;
            }
            fprintf(stderr, "Host %d: duplicated ACK seq num %d, cumulative dup cnt = %d\n", host->id, seq_num, host->cc[src_id].dup_acks);
        }
        // update CC
        if (host->cc[src_id].state == cc_SS) {
            host->cc[src_id].cwnd++;
            fprintf(stderr, "Host %d: received ACK in slow start stage, cwnd = %f\n", host->id, host->cc[src_id].cwnd);
            if (host->cc[src_id].cwnd > host->cc[src_id].ssthresh) {
                host->cc[src_id].state = cc_AIMD;
                fprintf(stderr, "Host %d: slow start -> AIMD\n", host->id);
            }
        } else if (host->cc[src_id].state == cc_AIMD && host->cc[src_id].dup_acks == 0) {
            host->cc[src_id].cwnd += 1. / host->cc[src_id].cwnd;
            fprintf(stderr, "Host %d: received new ACK in AIMD stage, cwnd = %f\n", host->id, host->cc[src_id].cwnd);
        } else if (host->cc[src_id].state == cc_FRFT && host->cc[src_id].dup_acks == 0) {
            // exit FRFT
            host->cc[src_id].state = cc_AIMD;
            host->cc[src_id].cwnd = host->cc[src_id].ssthresh;
            fprintf(stderr, "Host %d: FRFT -> AIMD, cwnd = %f\n", host->id, host->cc[src_id].cwnd);
        } else if (host->cc[src_id].dup_acks == 3 && host->cc[src_id].state != cc_FRFT) {
            // enter FTFR
            host->cc[src_id].state = cc_FRFT;
            // host->cc[src_id].ssthresh = min_double(host->cc[src_id].cwnd, glb_sysconfig.window_size) / 2.;  // todo: check for piazza answer
            host->cc[src_id].ssthresh = host->cc[src_id].cwnd / 2.;  // todo: check for piazza answer
            host->cc[src_id].ssthresh = max_double(host->cc[src_id].ssthresh, 2);
            host->cc[src_id].cwnd = host->cc[src_id].ssthresh + 3;
            fprintf(stderr, "Host %d: 3 dup ACKs on %d, enter FRFT, ssthresh = %f, cwnd = %f\n", 
                host->id, src_id, host->cc[src_id].ssthresh, host->cc[src_id].cwnd);
            // fast retransmit
            retransmit_frame(host, src_id, seq_num+1);
        }

        if (cq_get_length_send(src_id, host) == 0 || seq_num_diff(cq_get_item_send(src_id, 0, host)->frame->seq_num, seq_num) < 0) {
            fprintf(stderr, "Host %d: dropped ACK %d from host %d (older than expected)\n", host->id, seq_num, src_id);
            continue;
        }
        for (int j = 0; j < cq_get_length_send(src_id, host); j++) {
            Frame *frame = cq_get_item_send(src_id, j, host)->frame;
            if (frame->seq_num != seq_num) {
                continue;
            }

            // implement protocol logic
            cq_ndequeue_send(src_id, j + 1, host);
            fprintf(stderr, "Host %d: dequeued %d from window, window length %d\n", host->id, j+1, cq_get_length_send(src_id, host));
            break;
        }
    }

    if (host->id == glb_sysconfig.host_send_cc_id) {
        fprintf(cc_diagnostics,"%d,%d,%d,",host->round_trip_num, num_acks_received[glb_sysconfig.host_recv_cc_id], num_dup_acks_for_this_rtt[glb_sysconfig.host_recv_cc_id]); 
    }
}

void handle_input_cmds(Host* host, struct timeval curr_timeval) {
    // TODO: Suggested steps for handling input cmd
    //    1) Dequeue the Cmd from host->input_cmdlist_head
    //    2) Implement fragmentation if the message length is larger than FRAME_PAYLOAD_SIZE
    //    3) Set up the frame according to the protocol
    //    4) Append each frame to host->buffered_outframes_head

    int input_cmd_length = ll_get_length(host->input_cmdlist_head);

    while (input_cmd_length > 0) {
        // Pop a node off and update the input_cmd_length
        LLnode* ll_input_cmd_node = ll_pop_node(&host->input_cmdlist_head);
        input_cmd_length = ll_get_length(host->input_cmdlist_head);

        // Cast to Cmd type and free up the memory for the node
        Cmd* outgoing_cmd = (Cmd*) ll_input_cmd_node->value;
        free(ll_input_cmd_node);
 
        int msg_length = strlen(outgoing_cmd->message) + 1; // +1 to account for null terminator 
        int start = 0;
        while (start < msg_length) {
            int end = start + FRAME_PAYLOAD_SIZE;
            if (end > msg_length) {
                end = msg_length;
            }
            Frame* outgoing_frame = malloc(sizeof(Frame));
            assert(outgoing_frame);
            strncpy(outgoing_frame->data, outgoing_cmd->message + start, end-start);
            if (end == msg_length) {
                outgoing_frame->data[end - start - 1] = '\0';
            }
            outgoing_frame->length = end - start;
            outgoing_frame->remaining_msg_bytes = msg_length - end;
            outgoing_frame->src_id = outgoing_cmd->src_id;
            outgoing_frame->dst_id = outgoing_cmd->dst_id;
            outgoing_frame->is_ack = 0;

            int dst_id = outgoing_frame->dst_id;
            outgoing_frame->seq_num = host->curr_seq_num[dst_id];
            host->curr_seq_num[dst_id]++;

            // add CRC_8 checksum
            set_checksum(outgoing_frame);
            fprintf(stderr, "validating checksum: %d\n", frame_valid(outgoing_frame));

            ll_append_node(&host->buffered_outframes_head, outgoing_frame);
            start = end;
        }
        // At this point, we don't need the outgoing_cmd
        free(outgoing_cmd->message);
        free(outgoing_cmd);
    }
}

void handle_timedout_frames(Host* host, struct timeval curr_timeval) {

    // TODO: Handle frames that have timed out
    // Check your send_window for the frames that have timed out and set send_window[i]->timeout = NULL
    for (int recv_id = 0; recv_id < glb_num_hosts; recv_id++) {
        if (recv_id == host->id) continue;
        for (int j = 0; j < cq_get_length_send(recv_id, host); j++) {
            struct send_window_slot *slot = cq_get_item_send(recv_id, j, host);
            if (slot->timeout != NULL && timeval_usecdiff(&curr_timeval, slot->timeout) <= 0) {
                // free all frames in the send window
                fprintf(stderr, "Host %d: frame %d (%d -> %d) timed out, set timeout to NULL\n", host->id, slot->frame->seq_num, slot->frame->src_id, slot->frame->dst_id);
                for (int i = 0; i < cq_get_length_send(recv_id, host); i++) {
                    slot = cq_get_item_send(recv_id, i, host);
                    free(slot->timeout);
                    slot->timeout = NULL;
                }
                host->cc[recv_id].state = cc_SS;
                // host->cc[recv_id].ssthresh = min_double(host->cc[recv_id].cwnd, glb_sysconfig.window_size) / 2;
                host->cc[recv_id].ssthresh = host->cc[recv_id].cwnd / 2;  // todo: choose from RFC2001 or specs
                host->cc[recv_id].cwnd = 1;
                break;
            }
        }
    }
}

void handle_outgoing_frames(Host* host, struct timeval curr_timeval) {

    long additional_ts = 0; 

    if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
        memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    }

    // maximum frames that can be sent in this round trip time
    int *frames_per_rtt = calloc(sizeof(int), glb_num_hosts);
    for (int i = 0; i < glb_num_hosts; i++) {
        frames_per_rtt[i] = host->cc[i].cwnd;
    }

    //TODO: Send out the frames that have timed out(i.e. timeout = NULL)
    for (int recv_id = 0; recv_id < glb_num_hosts; recv_id++) {
        if (recv_id == host->id) continue;
        for (int i = 0; i < cq_get_length_send(recv_id, host); i++) {
            struct send_window_slot *slot = cq_get_item_send(recv_id, i, host);
            if (slot->timeout == NULL && frames_per_rtt[recv_id] > 0) {
                retransmit_frame(host, recv_id, slot->frame->seq_num);
                frames_per_rtt[recv_id]--;
            }
        }
    }

    //TODO: The code is incomplete and needs to be changed to have a correct behavior
    //Suggested steps: 
    //1) Within the for loop, check if the window is not full and there's space to send more frames 
    //2) If there is, pop from the buffered_outframes_head queue and fill your send_window_slot data structure with appropriate fields. 
    //3) Append the popped frame to the host->outgoing_frames_head
    LLnode *node = host->buffered_outframes_head;
    int length = ll_get_length(host->buffered_outframes_head);
    for (int i = 0; i < length; i++) {
        Frame *frame = ll_peek_node(node);
        int dst_id = frame->dst_id;
        if (cq_get_length_send(dst_id, host) == glb_sysconfig.window_size || frames_per_rtt[dst_id] == 0) {
            node = node->next;
            continue;
        }
        frames_per_rtt[dst_id]--;
        LLnode* ll_outframe_node;
        if (node == host->buffered_outframes_head) {
            ll_outframe_node = ll_pop_node(&host->buffered_outframes_head); // need this because otherwise the head points to the freed node
            node = host->buffered_outframes_head;
        } else {
            ll_outframe_node = ll_pop_node(&node);
        }
        Frame* outgoing_frame = ll_outframe_node->value; 

        ll_append_node(&host->outgoing_frames_head, outgoing_frame); 

        fprintf(stderr, "frame %d: %d -> %d, window length %d\n", 
            outgoing_frame->seq_num, outgoing_frame->src_id, 
            outgoing_frame->dst_id, cq_get_length_send(outgoing_frame->dst_id, host));
        
        //Set a timeout for this frame
        //NOTE: Each dataframe(not ack frame) that is appended to the 
        //host->outgoing_frames_head has to have a 10ms offset from 
        //the previous frame to enable selective retransmission mechanism. 
        //Already implemented below
        struct timeval* next_timeout = malloc(sizeof(struct timeval));
        memcpy(next_timeout, &curr_timeval, sizeof(struct timeval)); 
        timeval_usecplus(next_timeout, TIMEOUT_INTERVAL_USEC + additional_ts);
        additional_ts += 10000; //ADD ADDITIONAL 10ms

        Frame *out_frame = malloc(sizeof(Frame));
        memcpy(out_frame, outgoing_frame, sizeof(Frame));
        cq_append_item_send(dst_id, out_frame, next_timeout, host);
        fprintf(stderr, "Host %d: add frame %d to window, window length %d\n", 
            host->id, outgoing_frame->seq_num, 
            cq_get_length_send(outgoing_frame->dst_id, host));
        // print_window(dst_id, host);

        free(ll_outframe_node);
    }

    memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    timeval_usecplus(host->latest_timeout, additional_ts);

    free(frames_per_rtt);
    
    //NOTE:
    // Dont't worry about latest_timeout field for PA1a, but you need to understand what it does.
    // You may or may not use in PA1b when you implement fast recovery & fast retransmit in handle_incoming_acks(). 
    // If you choose to use it in PA1b, all you need to do is:

    // ****************************************
    // long additional_ts = 0; 
    // if (timeval_usecdiff(&curr_timeval, host->latest_timeout) > 0) {
    //     memcpy(&curr_timeval, host->latest_timeout, sizeof(struct timeval)); 
    // }

    //  YOUR FRFT CODE FOES HERE

    // memcpy(host->latest_timeout, &curr_timeval, sizeof(struct timeval)); 
    // timeval_usecplus(host->latest_timeout, additional_ts);
    // ****************************************


    // It essentially fixes the following problem:
    
    // 1) You send out 8 frames from sender0. 
    // Frame 1: curr_time + 0.09 + additional_ts(0.01) 
    // Frame 2: curr_time + 0.09 + additiona_ts (0.02) 
    // …

    // 2) Next time you send frames from sender0
    // Curr_time could be less than previous_curr_time + 0.09 + additional_ts. 
    // which means for example frame 9 will potentially timeout faster than frame 6 which shouldn’t happen. 

    // Latest timeout fixes that. 

}

// WE HIGHLY RECOMMEND TO NOT MODIFY THIS FUNCTION
void run_senders() {
    int sender_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, sender_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int sender_id = sender_order[i]; 
        struct timeval curr_timeval;

        gettimeofday(&curr_timeval, NULL);

        Host* host = &glb_hosts_array[sender_id]; 

        // Check whether anything has arrived
        int input_cmd_length = ll_get_length(host->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(host->incoming_frames_head);
        struct timeval* next_timeout = host_get_next_expiring_timeval(host); 
        
        // Conditions to "wake up" the host:
        //    1) Acknowledgement or new command
        //    2) Timeout      
        int incoming_frames_cmds = (input_cmd_length != 0) | (inframe_queue_length != 0); 
        long reached_timeout = (next_timeout != NULL) && (timeval_usecdiff(&curr_timeval, next_timeout) <= 0);

        host->awaiting_ack = 0; 
        host->active = 0; 
        host->csv_out = 0; 

        if (incoming_frames_cmds || reached_timeout) {
            host->round_trip_num += 1; 
            host->csv_out = 1; 
            fprintf(stderr, "Round %d, Host %d start sending\n", host->round_trip_num, host->id);
            // Implement this
            handle_input_cmds(host, curr_timeval); 
            // Implement this
            handle_incoming_acks(host, curr_timeval);
            // Implement this
            handle_timedout_frames(host, curr_timeval);
            // Implement this
            handle_outgoing_frames(host, curr_timeval); 
        }

        //Check if we are waiting for acks
        for (int j = 0; j < glb_sysconfig.window_size*glb_num_hosts; j++) {
            if (host->send_window[j].frame != NULL) {
                host->awaiting_ack = 1; 
                break; 
            }
        }

        //Condition to indicate that the host is active 
        if (host->awaiting_ack || ll_get_length(host->buffered_outframes_head) > 0) {
            host->active = 1; 
        }
    }
}