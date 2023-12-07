/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    printf("*** -> Received packet of length %d \n",len);

    /* fill in code here */
    /*
    Types of incoming packets:
    1. ARP request for us
    2. ICMP echo request to us
    3. ARP reply to us
    4. TCP/UDP packet to us
    5. IP packets to forward (including ICMP)
    */
    fprintf(stderr, "received packet:\n");
    print_hdrs(packet, len);
    if (packet_is_not_arp_or_ip(packet, len)) {
        fprintf(stderr, "dropping non-arp and non-ip packet\n");
        return;
    }
    if (packet_is_arp_req(packet, len)) {
      sr_send_arp_response(sr, packet, len, interface);
      return;
    }
    if (packet_is_arp_reply(packet, len)) {
      sr_handle_arp_reply(sr, packet, len, interface);
      return;
    }
    if (sr_packet_is_ip_to_us(sr, packet, len)) {
      if (packet_is_icmp_echo_req(packet, len)) {
        sr_send_icmp_response(sr, packet, len, interface);
      } else if (packet_is_tcp_udp(packet, len)) {
        sr_send_icmp_port_unreachable(sr, packet, len, interface);
      } /* otherwise, ignore the packet */
      return;
    }
    sr_forward_packet(sr, packet, len, interface);

}/* end sr_ForwardPacket */

int packet_is_not_arp_or_ip(uint8_t *packet, unsigned int len) {
    struct sr_ethernet_hdr *e_hdr = (struct sr_ethernet_hdr*) packet;
    return e_hdr->ether_type != htons(ethertype_arp) && 
        e_hdr->ether_type != htons(ethertype_ip);
}

int packet_is_arp_req(uint8_t *packet, unsigned int len) {
    struct sr_ethernet_hdr *e_hdr = (struct sr_ethernet_hdr*) packet;
    struct sr_arp_hdr *a_hdr = (struct sr_arp_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    return e_hdr->ether_type == htons(ethertype_arp) && 
        a_hdr->ar_op == htons(arp_op_request);  /* todo: network order? */
}

int packet_is_arp_reply(uint8_t *packet, unsigned int len) {
    struct sr_ethernet_hdr *e_hdr = (struct sr_ethernet_hdr*) packet;
    struct sr_arp_hdr *a_hdr = (struct sr_arp_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    return e_hdr->ether_type == htons(ethertype_arp) && 
        a_hdr->ar_op == htons(arp_op_reply);  /* todo: network order? */
}

int packet_is_icmp_echo_req(uint8_t *packet, unsigned int len) {
    struct sr_ethernet_hdr *e_hdr = 0;
    struct sr_ip_hdr *ip_hdr = 0;
    struct sr_icmp_t08_hdr *icmp_hdr = 0;
    e_hdr = (struct sr_ethernet_hdr*) packet;
    if (e_hdr->ether_type != htons(ethertype_ip)) return 0;
    ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    if (ip_hdr->ip_p != ip_protocol_icmp) return 0;
    icmp_hdr = (struct sr_icmp_t08_hdr*)(packet + sizeof(struct sr_ethernet_hdr) + 
                                     sizeof(struct sr_ip_hdr));
    if (icmp_hdr->icmp_type != 8) return 0;
    return 1;
}

int packet_is_tcp_udp(uint8_t *packet, unsigned int len) {
    struct sr_ethernet_hdr *e_hdr = 0;
    struct sr_ip_hdr *ip_hdr = 0;
    e_hdr = (struct sr_ethernet_hdr*) packet;
    if (e_hdr->ether_type != htons(ethertype_ip)) return 0;
    ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    if (ip_hdr->ip_p == ip_protocol_tcp || ip_hdr->ip_p == ip_protocol_udp) return 1;
    return 0;
}

int sr_packet_is_ip_to_us(struct sr_instance *sr,
        uint8_t *packet, unsigned int len) {
    struct sr_ethernet_hdr* e_hdr = 0;
    struct sr_ip_hdr *ip_hdr = 0;
    e_hdr = (struct sr_ethernet_hdr*)packet;
    if (e_hdr->ether_type != htons(ethertype_ip)) return 0;
    ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));

    struct sr_if* if_walker = 0;
    assert(sr);

    if_walker = sr->if_list;

    while(if_walker)
    {
       if(if_walker->ip == ip_hdr->ip_dst)
        { return 1; }
        if_walker = if_walker->next;
    }

    return 0;
}

void sr_send_arp_response(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ethernet_hdr* in_e_hdr = 0;
    struct sr_arp_hdr*      in_a_hdr = 0;

    in_e_hdr = (struct sr_ethernet_hdr*)packet;
    in_a_hdr = (struct sr_arp_hdr*)(packet + sizeof(struct sr_ethernet_hdr));

    unsigned int out_len;
    uint8_t *buf = sr_prepare_arp_packet(
        sr,
        interface,
        in_e_hdr->ether_shost,
        arp_op_reply,
        in_a_hdr->ar_sha,
        in_a_hdr->ar_sip,
        &out_len
    );

    sr_send_packet(sr, buf, out_len, interface);

    fprintf(stderr, "========================================\n");
    fprintf(stderr, "sent ARP reply packet:\n");
    print_hdrs(buf, out_len);

    /* clean up */
    free(buf);
}

void sr_send_icmp_response(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ethernet_hdr* in_e_hdr = 0;
    struct sr_ip_hdr*       in_ip_hdr = 0;
    struct sr_icmp_t08_hdr* in_icmp_hdr = 0, *out_icmp_hdr = 0;

    in_e_hdr = (struct sr_ethernet_hdr*)packet;
    in_ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    in_icmp_hdr = (struct sr_icmp_t08_hdr*)(
        packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

    if (check_icmp_packet_corrupt(in_icmp_hdr, 
            len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr))) {
        fprintf(stderr, "dropped corrupted ICMP echo request packet\n");
        return;
    }

    unsigned int out_len;
    uint8_t *buf = sr_prepare_ip_packet(
        sr,
        sizeof(struct sr_icmp_t08_hdr),
        in_ip_hdr->ip_src,
        in_e_hdr->ether_shost,
        in_ip_hdr->ip_dst,
        ip_protocol_icmp,
        interface,
        &out_len
    );

    out_icmp_hdr = (struct sr_icmp_t08_hdr*)(
        buf + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

    out_icmp_hdr->icmp_type = 0;
    out_icmp_hdr->icmp_code = 0;
    out_icmp_hdr->icmp_sum = 0;
    out_icmp_hdr->icmp_id = in_icmp_hdr->icmp_id;
    out_icmp_hdr->icmp_seq = in_icmp_hdr->icmp_seq;
    out_icmp_hdr->icmp_sum = cksum(out_icmp_hdr, sizeof(struct sr_icmp_t08_hdr));

    sr_send_packet(sr, buf, out_len, interface);

    fprintf(stderr, "========================================\n");
    fprintf(stderr, "sent ICMP reply packet:\n");
    print_hdrs(buf, out_len);

    /* clean up */
    free(buf);
}

void sr_handle_arp_reply(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_arp_hdr*      in_a_hdr = 0;

    in_a_hdr = (struct sr_arp_hdr*)(packet + sizeof(struct sr_ethernet_hdr));

    unsigned char *dest_mac = in_a_hdr->ar_sha;
    uint32_t dest_ip = in_a_hdr->ar_sip;
    /* add arp to cache */
    struct sr_arpentry * entry = sr_arpcache_lookup(&sr->cache, dest_ip);
    struct sr_arpreq * req = NULL;
    if (entry == NULL) {
        fprintf(stderr, "adding mac address of ip to arp cache: ");
        print_addr_ip_int(ntohl(dest_ip));
        fprintf(stderr, "\n");
        req = sr_arpcache_insert(&sr->cache, dest_mac, dest_ip);
        if (req != NULL) {
            sr_send_arp_queued_packets(sr, req->packets, dest_mac);
        }
    } else {
        free(entry);
        /* send all packets targeted to this ip */ 
        sr_arp_send_packets(sr, dest_mac, dest_ip);
    }
}

void sr_forward_packet(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ip_hdr*       in_ip_hdr = 0, *out_ip_hdr = 0;
    struct sr_ethernet_hdr* out_e_hdr = 0;
    in_ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));

    uint8_t *buf = malloc(len);
    memcpy(buf, packet, len);
    out_e_hdr = (struct sr_ethernet_hdr*) buf;
    out_ip_hdr = (struct sr_ip_hdr*)(buf + sizeof(struct sr_ethernet_hdr));

    /* first, sanity check and decrement ttl, and re-checksum */
    if (check_ip_packet_corrupt(in_ip_hdr)) {
        fprintf(stderr, "dropping corrupted IP packet\n");
        free(buf);
        return;
    }
    out_ip_hdr->ip_ttl -= 1;
    if (out_ip_hdr->ip_ttl == 0) {
        fprintf(stderr, "packet dropped for ttl = 0\n");
        sr_send_icmp_time_exceeded(sr, packet, len, interface);
        free(buf);
        return;
    }
    out_ip_hdr->ip_sum = 0;
    out_ip_hdr->ip_sum = cksum(out_ip_hdr, 4*out_ip_hdr->ip_hl);

    /* then look up for ip address and interface */
    struct sr_rt *entry = 0;
    entry = sr_rt_lookup_ip(sr, out_ip_hdr->ip_dst);
    if (entry == 0) {
        /* send ICMP: Destination net unreachable (type 3, code 0) */
        sr_send_icmp_net_unreachable(sr, packet, len, interface);
        free(buf);
        return;
    }
    uint32_t next_hop_ip = entry->gw.s_addr;
    struct sr_if *iface = sr_get_interface(sr, entry->interface);
    assert(iface);
    memcpy(out_e_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);

    /* 
      queue the ARP request (handle retries and timeouts in another thread)
      forward the packet (the arp handling thread)
    */
    struct sr_arpentry *arpentry = sr_arpcache_lookup(&sr->cache, next_hop_ip);
    if (arpentry == NULL) {
        sr_arpcache_queuereq(&sr->cache, next_hop_ip, buf, len, entry->interface);
        sr_send_arp_request(sr, next_hop_ip, entry->interface);
    } else {
        memcpy(out_e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "find mac address in arp cache\n");
        fprintf(stderr, "forward packet:\n");
        print_hdrs(buf, len);
        sr_send_packet(sr, buf, len, entry->interface);
        free(arpentry);
    }
    free(buf);
}

struct sr_rt *sr_rt_lookup_ip(struct sr_instance *sr, uint32_t ip) {
    struct sr_rt *rt_walker = sr->routing_table;
    int match_bits = 0;
    struct sr_rt *entry = NULL;
    while (rt_walker) {
        if ((rt_walker->dest.s_addr & rt_walker->mask.s_addr) == 
                (ip & rt_walker->mask.s_addr)) {
            int bits = __builtin_popcount(rt_walker->mask.s_addr);
            if (match_bits < bits) {
                match_bits = bits;
                entry = rt_walker;
            }
        }
        rt_walker = rt_walker->next;
    }
    return entry;
}

void sr_send_icmp_net_unreachable(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ethernet_hdr* in_e_hdr = 0;
    struct sr_ip_hdr*       in_ip_hdr = 0;
    struct sr_icmp_t11_hdr *out_icmp_hdr = 0;

    in_e_hdr = (struct sr_ethernet_hdr*)packet;
    in_ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    uint8_t *data = packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr);

    unsigned int out_len;
    uint8_t *buf = sr_prepare_ip_packet(
        sr,
        sizeof(struct sr_icmp_t11_hdr),
        in_ip_hdr->ip_src,
        in_e_hdr->ether_shost,
        0,
        ip_protocol_icmp,
        interface,
        &out_len
    );

    out_icmp_hdr = (struct sr_icmp_t11_hdr*)(
        buf + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

    out_icmp_hdr->icmp_type = 3;
    out_icmp_hdr->icmp_code = 0;
    out_icmp_hdr->icmp_sum = 0;
    out_icmp_hdr->unused = 0;
    memcpy(out_icmp_hdr->data, in_ip_hdr, 20);
    memcpy(out_icmp_hdr->data+20, data, 8);
    out_icmp_hdr->icmp_sum = cksum(out_icmp_hdr, sizeof(struct sr_icmp_t11_hdr));

    sr_send_packet(sr, buf, out_len, interface);

    fprintf(stderr, "========================================\n");
    fprintf(stderr, "sent ICMP net unreachable packet:\n");
    print_hdrs(buf, out_len);

    free(buf);
}

void sr_send_icmp_host_unreachable(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ethernet_hdr* out_e_hdr = 0;
    struct sr_ip_hdr*       ip_hdr = 0;
    struct sr_icmp_t11_hdr* out_icmp_hdr = 0;

    ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    uint8_t *data = packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr);

    struct sr_rt *entry = 0;
    entry = sr_rt_lookup_ip(sr, ip_hdr->ip_src);
    uint32_t next_hop_ip = entry->gw.s_addr;
    struct sr_if *iface = sr_get_interface(sr, entry->interface);
    unsigned int out_len;
    uint8_t *buf = sr_prepare_ip_packet(
        sr,
        sizeof(struct sr_icmp_t11_hdr),
        ip_hdr->ip_src,
        iface->addr,  /* will be replaced by ARP reply */
        0,
        ip_protocol_icmp,
        entry->interface,
        &out_len
    );

    out_e_hdr = (struct sr_ethernet_hdr*) buf;
    out_icmp_hdr = (struct sr_icmp_t11_hdr*)(
        buf + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

    out_icmp_hdr->icmp_type = 3;
    out_icmp_hdr->icmp_code = 1;
    out_icmp_hdr->icmp_sum = 0;
    out_icmp_hdr->unused = 0;
    memcpy(out_icmp_hdr->data, ip_hdr, 20);
    memcpy(out_icmp_hdr->data+20, data, 8);
    out_icmp_hdr->icmp_sum = cksum(out_icmp_hdr, sizeof(struct sr_icmp_t11_hdr));

    struct sr_arpentry *arpentry = sr_arpcache_lookup_nolock(&sr->cache, next_hop_ip);
    if (arpentry == NULL) {
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "sent ARP request packet:\n");
        sr_arpcache_queuereq_nolock(&sr->cache, next_hop_ip, buf, out_len, entry->interface);
        sr_send_arp_request(sr, next_hop_ip, entry->interface);

        fprintf(stderr, "========================================\n");
        fprintf(stderr, "add ICMP host unreachable packet to queue:\n");
        print_hdrs(buf, out_len);
    } else {
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "found mac in arp cache\n");
        memcpy(out_e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
        sr_send_packet(sr, buf, out_len, entry->interface);
        fprintf(stderr, "sent ICMP host unreachable packet:\n");
        print_hdrs(buf, out_len);
        free(arpentry);
    }

    free(buf);
}

void sr_send_icmp_time_exceeded(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ethernet_hdr* in_e_hdr = 0;
    struct sr_ip_hdr*       in_ip_hdr = 0;
    struct sr_icmp_t11_hdr* out_icmp_hdr = 0;

    in_e_hdr = (struct sr_ethernet_hdr*)packet;
    in_ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    uint8_t *data = packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr);

    unsigned int out_len;
    uint8_t *buf = sr_prepare_ip_packet(
        sr,
        sizeof(struct sr_icmp_t11_hdr),
        in_ip_hdr->ip_src,
        in_e_hdr->ether_shost,
        0,
        ip_protocol_icmp,
        interface,
        &out_len
    );

    out_icmp_hdr = (struct sr_icmp_t11_hdr*)(
        buf + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

    out_icmp_hdr->icmp_type = 11;
    out_icmp_hdr->icmp_code = 0;
    out_icmp_hdr->icmp_sum = 0;
    out_icmp_hdr->unused = 0;
    memcpy(out_icmp_hdr->data, in_ip_hdr, 20);
    memcpy(out_icmp_hdr->data+20, data, 8);
    out_icmp_hdr->icmp_sum = cksum(out_icmp_hdr, sizeof(struct sr_icmp_t11_hdr));

    sr_send_packet(sr, buf, out_len, interface);

    fprintf(stderr, "========================================\n");
    fprintf(stderr, "sent ICMP time exceeded packet:\n");
    print_hdrs(buf, out_len);

    free(buf);
}

void sr_send_icmp_port_unreachable(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */) {
    struct sr_ethernet_hdr* in_e_hdr = 0;
    struct sr_ip_hdr*       in_ip_hdr = 0;
    struct sr_icmp_t11_hdr *out_icmp_hdr = 0;

    in_e_hdr = (struct sr_ethernet_hdr*)packet;
    in_ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
    uint8_t *data = packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr);

    unsigned int out_len;
    uint8_t *buf = sr_prepare_ip_packet(
        sr,
        sizeof(struct sr_icmp_t11_hdr),
        in_ip_hdr->ip_src,
        in_e_hdr->ether_shost,
        0,
        ip_protocol_icmp,
        interface,
        &out_len
    );

    out_icmp_hdr = (struct sr_icmp_t11_hdr*)(
        buf + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

    out_icmp_hdr->icmp_type = 3;
    out_icmp_hdr->icmp_code = 3;
    out_icmp_hdr->icmp_sum = 0;
    out_icmp_hdr->unused = 0;
    memcpy(out_icmp_hdr->data, in_ip_hdr, 20);
    memcpy(out_icmp_hdr->data+20, data, 8);
    out_icmp_hdr->icmp_sum = cksum(out_icmp_hdr, sizeof(struct sr_icmp_t11_hdr));

    sr_send_packet(sr, buf, out_len, interface);

    fprintf(stderr, "========================================\n");
    fprintf(stderr, "sent ICMP port unreachable packet:\n");
    print_hdrs(buf, out_len);

    free(buf);
}

void sr_send_arp_request(struct sr_instance *sr,
        uint32_t ip, char *interface) {
    unsigned int out_len;
    unsigned char mac[ETHER_ADDR_LEN], arp_ha[ETHER_ADDR_LEN];
    memset(mac, 0xff, ETHER_ADDR_LEN);
    memset(arp_ha, 0, ETHER_ADDR_LEN);
    uint8_t *buf = sr_prepare_arp_packet(
        sr,
        interface,
        mac,
        arp_op_request,
        arp_ha,
        ip,
        &out_len
    );
    
    sr_send_packet(sr, buf, out_len, interface);

    fprintf(stderr, "========================================\n");
    fprintf(stderr, "sent ARP request packet:\n");
    print_hdrs(buf, out_len);
    
    /* clean up */
    free(buf);
}

uint8_t *sr_prepare_ip_packet(struct sr_instance *sr,
        unsigned int payload_len,
        uint32_t dest_ip,
        unsigned char *dest_mac,
        uint32_t src_ip,  /* this should be 0 if it is equal to interface */
        uint8_t protocol,
        char *interface,
        uint32_t *packet_len) {
    *packet_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr)
              + payload_len;
    uint8_t *buf = malloc(*packet_len);
    struct sr_if* iface = sr_get_interface(sr, interface);
    struct sr_ethernet_hdr* e_hdr = 0;
    struct sr_ip_hdr*       ip_hdr = 0;

    assert(iface);

    e_hdr = (struct sr_ethernet_hdr*)buf;
    ip_hdr = (struct sr_ip_hdr*)(buf + sizeof(struct sr_ethernet_hdr));
    
    memcpy(e_hdr->ether_dhost, dest_mac, ETHER_ADDR_LEN);
    memcpy(e_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);
    e_hdr->ether_type = htons(ethertype_ip);

    ip_hdr->ip_v = 4;
    assert(sizeof(struct sr_ip_hdr) == 20);
    ip_hdr->ip_hl = sizeof(struct sr_ip_hdr) / 4;
    ip_hdr->ip_tos = 0;
    ip_hdr->ip_len = htons(*packet_len - sizeof(struct sr_ethernet_hdr));
    ip_hdr->ip_id = 0;
    ip_hdr->ip_off = 0;
    ip_hdr->ip_ttl = INIT_TTL;
    ip_hdr->ip_p = protocol;
    ip_hdr->ip_sum = 0;
    ip_hdr->ip_src = src_ip == 0 ? iface->ip : src_ip;
    ip_hdr->ip_dst = dest_ip;
    ip_hdr->ip_sum = cksum(ip_hdr, sizeof(struct sr_ip_hdr));

    return buf;
}

uint8_t *sr_prepare_arp_packet(
    struct sr_instance *sr,
    char *interface,
    unsigned char *dest_mac,
    unsigned short arp_op,
    unsigned char *arp_tha,
    uint32_t arp_tip,
    unsigned int *out_len
) {
    *out_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arp_hdr);
    uint8_t *buf = malloc(*out_len);
    struct sr_if* iface = sr_get_interface(sr, interface);
    struct sr_ethernet_hdr* e_hdr = 0;
    struct sr_arp_hdr*      a_hdr = 0;

    assert(iface);

    e_hdr = (struct sr_ethernet_hdr*)buf;
    a_hdr = (struct sr_arp_hdr*)(buf + sizeof(struct sr_ethernet_hdr));
    
    memcpy(e_hdr->ether_dhost, dest_mac, ETHER_ADDR_LEN);
    memcpy(e_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);
    e_hdr->ether_type = htons(ethertype_arp);

    a_hdr->ar_hrd = htons(arp_hrd_ethernet);
    a_hdr->ar_pro = htons(ethertype_ip);  /* happens to be 0x800 */
    a_hdr->ar_hln = ETHER_ADDR_LEN;
    a_hdr->ar_pln = 4;
    a_hdr->ar_op = htons(arp_op);
    memcpy(a_hdr->ar_sha, iface->addr, ETHER_ADDR_LEN);
    a_hdr->ar_sip = iface->ip;
    memcpy(a_hdr->ar_tha, arp_tha, ETHER_ADDR_LEN);
    a_hdr->ar_tip = arp_tip;

    return buf;
}

void sr_arp_send_packets(struct sr_instance *sr,
        unsigned char *mac, uint32_t ip) {
    pthread_mutex_lock(&(sr->cache.lock));
    
    struct sr_arpreq *req, *prev = NULL, *next = NULL; 
    for (req = sr->cache.requests; req != NULL; req = req->next) {
        if (req->ip == ip) {                
            if (prev) {
                next = req->next;
                prev->next = next;
            } 
            else {
                next = req->next;
                sr->cache.requests = next;
            }
            
            break;
        }
        prev = req;
    }

    if (req == NULL) {
        fprintf(stderr, "fail to find oustanding ARP requests for ip ");
        print_addr_ip_int(ntohl(ip));
        fprintf(stderr, "\n");
        pthread_mutex_unlock(&(sr->cache.lock));
        return;
    }

    sr_send_arp_queued_packets(sr, req->packets, mac);
    
    free(req);
    
    pthread_mutex_unlock(&(sr->cache.lock));
}

void sr_send_arp_queued_packets(struct sr_instance *sr,
        struct sr_packet *packets, unsigned char *mac) {
    struct sr_packet *pkt, *nxt;
    
    for (pkt = packets; pkt; pkt = nxt) {
        /* update dest mac and send the packet */
        struct sr_ethernet_hdr *e_hdr = (struct sr_ethernet_hdr*) pkt->buf;
        memcpy(e_hdr->ether_dhost, mac, ETHER_ADDR_LEN);
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "forward packet:\n");
        print_hdrs(pkt->buf, pkt->len);
        sr_send_packet(sr, pkt->buf, pkt->len, pkt->iface);

        nxt = pkt->next;
        if (pkt->buf)
            free(pkt->buf);
        if (pkt->iface)
            free(pkt->iface);
        free(pkt);
    }
}

int check_ip_packet_corrupt(void *hdr) {
    struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr*) hdr;
    void *temp_hdr = malloc(4*ip_hdr->ip_hl);
    memcpy(temp_hdr, hdr, 4*ip_hdr->ip_hl);
    ip_hdr = (struct sr_ip_hdr*) temp_hdr;
    unsigned short sum = ip_hdr->ip_sum;
    ip_hdr->ip_sum = 0;
    int corrupted = cksum(ip_hdr, 4*ip_hdr->ip_hl) != sum;
    free(temp_hdr);
    return corrupted;
}

int check_icmp_packet_corrupt(void *hdr, unsigned int len) {
    struct sr_icmp_t11_hdr *icmp_hdr = (struct sr_icmp_t11_hdr *) hdr;
    void *temp_hdr = malloc(len);
    memcpy(temp_hdr, hdr, len);
    icmp_hdr = (struct sr_icmp_t11_hdr*) temp_hdr;
    unsigned short sum = icmp_hdr->icmp_sum;
    icmp_hdr->icmp_sum = 0;
    int corrupted = cksum(icmp_hdr, len) != sum;
    free(temp_hdr);
    return corrupted;
}