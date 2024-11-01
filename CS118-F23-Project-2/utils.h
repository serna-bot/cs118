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
#define MAX_PKT_QUEUE 100



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

struct pck_node {
    struct packet* curr;
    struct pck_node* next;
};

struct pck_node* create_pck_node(struct packet* pkt) {
    struct pck_node* new_pkt_node = (struct pck_node*)malloc(sizeof(struct pck_node));
    struct packet* new_packet = (struct packet*)malloc(sizeof(struct packet));
    new_pkt_node->curr = new_packet;
    if (new_packet != NULL) {
        memcpy(new_packet, pkt, sizeof(struct packet));
    }
    new_pkt_node->next = NULL;
    return new_pkt_node;
}

void delete_pck_node(struct pck_node* node) {
    free(node->curr);
    free(node);
}

//FIFO QUEUE
struct packet_queue {
    struct pck_node* front;
    struct pck_node* rear;
    int count;
};

void init_packet_queue(struct packet_queue *pkt_queue) {
    pkt_queue->front = NULL;
    pkt_queue->rear = NULL;
    pkt_queue->count = 0;
}

int queue_empty(struct packet_queue *pkt_queue) {
    return (pkt_queue->count == 0);
}

int queue_full(struct packet_queue *pkt_queue) {
    return (pkt_queue->count == MAX_PKT_QUEUE);
}

void print_queue(struct packet_queue *pkt_queue) {
    struct pck_node* curr = pkt_queue->front;
    while(curr) {
        printf("Node seqnum: %u\n", curr->curr->seqnum);
        printf("Node address: %d\n", curr);
        curr = curr->next;
    }
}

int enqueue(struct packet_queue *pkt_queue, struct packet *pkt, int in_order) {
    if (!queue_full(pkt_queue)) {
        struct pck_node* new_pkt_node = create_pck_node(pkt);
        struct pck_node* curr = pkt_queue->front;
        struct pck_node* prev = NULL;

        if (!pkt_queue->front) {
            pkt_queue->front = new_pkt_node;
            pkt_queue->rear = new_pkt_node;
            pkt_queue->count++;
            return 1;
        }

        while(curr) {
            if (curr->curr->seqnum > pkt->seqnum) {
                if (!prev) {
                    pkt_queue->front = new_pkt_node;
                    new_pkt_node->next = curr;
                }
                else {
                    // oacket found is not the first value
                    prev->next = new_pkt_node;
                    new_pkt_node->next = curr;
                }
                pkt_queue->count++;
                return 1;
            }
            prev = curr;
            curr = curr->next;
            if (!curr) {
                prev->next = new_pkt_node;
                pkt_queue->rear = new_pkt_node;
            }
        }
        pkt_queue->count++;
    }
    // }
    else {
        return 0;
    }
    return 1;
}

int find_queue(struct packet_queue* pkt_queue, unsigned short rcv_seq_num) {
    if (!queue_empty(pkt_queue)) {
        struct pck_node* curr = pkt_queue->front;
        struct pck_node* prev = NULL;

        while(curr) {
            if (curr->curr->seqnum == rcv_seq_num) {
                // printf("found, dequeuing %s\n", copy->payload);
                if (prev == NULL) {
                    // found as the first item
                    pkt_queue->front = curr->next;
                    if (pkt_queue->front == NULL) {
                        // queue is empty now
                        pkt_queue->rear = NULL;
                    }
                }
                else {
                    // oacket found is not the first value
                    prev->next = curr->next;
                    if (prev->next == NULL) {
                        // If the found packet is the last in the queue, update the rear pointer
                        pkt_queue->rear = prev;
                    }
                }
                // printf("done searching queue size: %d\n", pkt_queue->count);
                // if (pkt_queue->front) printf("front: %d\n", pkt_queue->front->curr.seqnum);
                return 1;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    return 0;
}

struct packet* dequeue(struct packet_queue *pkt_queue, struct packet* rcv_pkt, int all_before_flag) {
    if (!queue_empty(pkt_queue)) {
        struct packet* copy = (struct packet*)malloc(sizeof(struct packet));
        if (!rcv_pkt) {
            struct pck_node* front_pkt_node = pkt_queue->front;
            struct packet* frontPacket = front_pkt_node->curr;
            memcpy(copy, frontPacket, sizeof(struct packet));

            pkt_queue->front = front_pkt_node->next;
            delete_pck_node(front_pkt_node);

            if (pkt_queue->front == NULL)
                pkt_queue->rear = NULL;

            pkt_queue->count--;
            return copy;
        }
        else {
            struct pck_node* curr = pkt_queue->front;
            struct pck_node* prev = NULL;

            // printf("trying to find %u\n", rcv_pkt->acknum);

            while(curr && curr->curr->seqnum < rcv_pkt->acknum) {
                // int found = all_before_flag ?  : 
                //     curr->curr->seqnum + curr->curr->length == rcv_pkt->acknum;
                // printf("current %u\n", curr->curr->seqnum);
                if (curr->curr->seqnum + 1 == rcv_pkt->acknum) {
                    if (!all_before_flag) {
                        memcpy(copy, curr->curr, sizeof(struct packet));
                        pkt_queue->front = curr->next;
                        if (pkt_queue->front == NULL) {
                            // queue is empty now
                            pkt_queue->rear = NULL;
                        }
                        // printf("dequeuing %u\n", curr->curr->seqnum);
                        delete_pck_node(curr);
                        
                        pkt_queue->count--;
                        // printf("found, dequeuing %s\n", copy->payload);
                        
                        // printf("done searching queue size: %d\n", pkt_queue->count);
                        // if (pkt_queue->front) printf("front: %d\n", pkt_queue->front->curr.seqnum);
                        return copy;
                    }
                    else {
                        free(copy);
                        return curr;
                    }
                }
                
                pkt_queue->front = curr->next;
                if (pkt_queue->front == NULL) {
                    // queue is empty now
                    pkt_queue->rear = NULL;
                }
                prev = curr;
                curr = curr->next;
                // printf("dequeuing %u\n", curr->curr->seqnum);
                delete_pck_node(prev);
                
                pkt_queue->count--;
            }
        }
    }
    return NULL;
}

//util function to create pcket from file of size length
void process_input_packets(struct packet* pkt, FILE* fp, unsigned int fp_pos, unsigned int seq, unsigned int ack, unsigned int length) {
    char* payload = (char *)calloc(length, sizeof(char));
    fseek(fp, fp_pos, SEEK_SET);
    fread(payload, length, 1, fp);
    build_packet(pkt, seq, ack, '\0', '\0', length, payload);
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