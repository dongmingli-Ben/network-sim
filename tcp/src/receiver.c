#include "host.h"
#include <assert.h>
#include "switch.h"

// Try to reassemble a message from received frames.
// If unsuccessful, nothing is changed; otherwise,
// frames are removed and freed.
char* reassemble_message(LLnode **head_ptr) {
    LLnode *node = *head_ptr;
    LLnode *buffer = NULL;
    int n_bytes = 0;
    for (int i = 0; i < ll_get_length(*head_ptr); i++) {
        Frame *frame = (Frame *) node->value;
        n_bytes += frame->length;
        ll_append_node(&buffer, frame);
        node = node->next;
        if (frame->remaining_msg_bytes == 0) {
            char * msg = malloc(n_bytes);
            int start = 0;
            while (ll_get_length(buffer) > 0) {
                LLnode *buffer_node = ll_pop_node(&buffer);
                node = ll_pop_node(head_ptr);
                frame = (Frame *) node->value;
                strncpy(msg + start, frame->data, frame->length);
                start += frame->length;
                free(node->value);
                free(node);
                free(buffer_node);
            }
            return msg;
        }
    }
    // clean up
    while (ll_get_length(buffer) > 0) {
        LLnode *buffer_node = ll_pop_node(&buffer);
        free(buffer_node);
    }
    return NULL;
}

void handle_incoming_frames(Host* host) {
    // TODO: Suggested steps for handling incoming frames
    //    1) Dequeue the Frame from the host->incoming_frames_head
    //    2) Compute CRC of incoming frame to know whether it is corrupted
    //    3) If frame is corrupted, drop it and move on.
    //    4) Implement logic to check if the expected frame has come
    //    5) Implement logic to combine payload received from all frames belonging to a message
    //       and print the final message when all frames belonging to a message have been received.
    //    6) Implement the cumulative acknowledgement part of the sliding window protocol
    //    7) Append acknowledgement frames to the outgoing_frames_head queue
    int incoming_frames_length = ll_get_length(host->incoming_frames_head);
    while (incoming_frames_length > 0) {
        // Pop a node off the front of the link list and update the count
        LLnode* ll_inmsg_node = ll_pop_node(&host->incoming_frames_head);
        incoming_frames_length = ll_get_length(host->incoming_frames_head);

        Frame* inframe = ll_inmsg_node->value; 
        if (!frame_valid(inframe)) {
            fprintf(stderr, "Host %d: dropping a corrupted frame in handle_incoming_frames\n", host->id);
            goto cleanup;
        }
        int src_id = inframe->src_id;
        uint8_t ack_seq_num = host->recv_lfr[src_id];
        fprintf(stderr, "Host %d: received frame %d from host %d.\n", host->id, inframe->seq_num, src_id);
        if (!recv_frame_in_window(inframe->seq_num, src_id, host)) {  // todo: handle overflow
            fprintf(stderr, "Host %d: dropping a out-of-window frame in handle_incoming_frames\n", host->id);
            goto send_ack;
        }

        int buffer_idx = seq_num_diff(host->recv_lfr[src_id], inframe->seq_num) - 1;
        Frame *buffer_frame = malloc(sizeof(Frame));
        memcpy(buffer_frame, inframe, sizeof(Frame));
        cq_set_item_recv(src_id, buffer_idx, buffer_frame, host);
        // try to advance the window and send ACK
        for (int i = 0; i < glb_sysconfig.window_size; i++) {
            Frame **frame = cq_get_item_recv(src_id, 0, host);
            if (*frame == NULL) {
                break;
            }
            ll_append_node(&host->recv_msg_head[src_id], *frame);
            cq_ndequeue_recv(src_id, 1, host);
            ack_seq_num++;
            host->recv_lfr[src_id]++;
        }
        fprintf(stderr, "Host %d: advance receive window %d to %d\n", host->id, src_id, host->recv_lfr[src_id]);
send_ack:
        // send ACK
        Frame *ack_frame = malloc(sizeof(Frame));
        ack_frame->src_id = host->id;
        ack_frame->dst_id = src_id;
        ack_frame->seq_num = ack_seq_num;
        ack_frame->is_ack = 1;
        set_checksum(ack_frame);
        ll_append_node(&host->outgoing_frames_head, ack_frame);
        fprintf(stderr, "ACK %d: %d -> %d\n", ack_seq_num, host->id, src_id);

        // try to reassembly the message
        char *msg;
        do {
            msg = reassemble_message(&host->recv_msg_head[src_id]);
            if (msg != NULL) {
                printf("<RECV_%d>:[%s]\n", host->id, msg);
                free(msg);
            }
        } while (msg != NULL);
cleanup:
        free(inframe);
        free(ll_inmsg_node);
    }
}

void run_receivers() {
    int recv_order[glb_num_hosts]; 
    get_rand_seq(glb_num_hosts, recv_order); 

    for (int i = 0; i < glb_num_hosts; i++) {
        int recv_id = recv_order[i]; 
        handle_incoming_frames(&glb_hosts_array[recv_id]); 
    }
}