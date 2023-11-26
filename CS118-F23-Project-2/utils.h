#ifndef UTILS_H
#define UTILS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACROS
#define SERVER_IP "127.0.0.1"
#define LOCAL_HOST "127.0.0.1"
#define SERVER_PORT_TO 5002
#define CLIENT_PORT 6001
#define SERVER_PORT 6002
#define CLIENT_PORT_TO 5001
#define HEADER_SIZE 176
#define PAYLOAD_SIZE 1024
#define WINDOW_SIZE 5
#define TIMEOUT 2
#define MAX_SEQUENCE 1024



// Packet Layout
// You may change this if you want to
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char ack;
    char last;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Utility function to build a packet
void build_packet(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char last, char ack,unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->ack = ack;
    pkt->last = last;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// Utility function to print a packet
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", (pkt->ack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
}

void printPayload(struct packet* pkt) {
    printf("Value of packet payload: %.*s\n", pkt->length, pkt->payload);
}

//Packet Buffer
struct packet_buffer {
    struct packet *buf_ptr;
    int *seq_map_ptr;
    unsigned int max_pckts;
};

void create_buffer(struct packet_buffer* packet_buffer, unsigned int max_pckts) {
    packet_buffer->max_pckts = max_pckts;
    packet_buffer->buf_ptr = (struct packet *)calloc(max_pckts, sizeof(struct packet));
    packet_buffer->seq_map_ptr = (int *)calloc(max_pckts, sizeof(int));
}

void free_buffer(struct packet_buffer* packet_buffer) {
    free(packet_buffer->buf_ptr);
    free(packet_buffer->seq_map_ptr);
}

int pop_buffer(struct packet_buffer* packet_buffer, unsigned int index) {
    if (index >= 0 && index < packet_buffer->max_pckts) {
        packet_buffer->seq_map_ptr[index] = -1;
        return 0;
    }
    return -1;
}

int insert_buffer(struct packet_buffer* packet_buffer, unsigned int index, struct packet *new_packet) {
    if (index >= 0 && index < packet_buffer->max_pckts) {
        packet_buffer->buf_ptr[index] = *new_packet;
        packet_buffer->seq_map_ptr[index] = new_packet->seqnum;
        return 0;
    }
    return -1;
}

struct packet* get_packet(struct packet_buffer* packet_buffer, int index) {
    return &(packet_buffer->buf_ptr[index]);
}

//util function to create pcket from file of size length
void process_packets(struct packet* pkt, FILE* fp, unsigned int seq, unsigned int ack, unsigned int length) {
    char* payload = (char *)calloc(length, sizeof(char));
    fseek(fp, seq, SEEK_SET);
    fread(payload, length, 1, fp);
    build_packet(pkt, htonl(seq), htonl(ack), '0', '0', length, payload);
    fseek(fp, 0, SEEK_SET);
    free(payload);
}

void close_packets(struct packet* pkt) {
    char* payload = (char *)calloc(1, sizeof(char));
    build_packet(pkt, htonl(0), htonl(0), '0', '1', 1, payload);
    free(payload);
}

void ack_close_packets(struct packet* pkt) {
    char* payload = (char *)calloc(1, sizeof(char));
    build_packet(pkt, htonl(0), htonl(0), '1', '0', 1, payload);
    free(payload);
}


#endif