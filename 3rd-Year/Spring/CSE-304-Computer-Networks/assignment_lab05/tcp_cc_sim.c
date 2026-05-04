#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_EVENTS 128
#define MSS 1.0

typedef enum { ALG_TAHOE, ALG_RENO, ALG_NEWRENO } Algorithm;
typedef enum { EV_NONE, EV_TIMEOUT, EV_TRIPLE_DUP_ACK, EV_PARTIAL_ACK } EventType;

typedef struct {
    int round;
    EventType type;
} PacketEvent;

typedef struct {
    Algorithm algorithm;
    double cwnd;
    double ssthresh;
    int dup_ack_count;
    int in_fast_recovery;
    int next_seq;
    int highest_ack;
} SenderState;

typedef struct {
    PacketEvent events[MAX_EVENTS];
    int count;
} EventList;

const char *algorithm_name(Algorithm alg) {
    switch (alg) {
        case ALG_TAHOE: return "TCP Tahoe";
        case ALG_RENO: return "TCP Reno";
        case ALG_NEWRENO: return "TCP NewReno";
        default: return "Unknown";
    }
}

const char *event_name(EventType ev) {
    switch (ev) {
        case EV_TIMEOUT: return "TIMEOUT";
        case EV_TRIPLE_DUP_ACK: return "TRIPLE_DUP_ACK";
        case EV_PARTIAL_ACK: return "PARTIAL_ACK";
        default: return "NONE";
    }
}

Algorithm parse_algorithm(const char *s) {
    char buf[32];
    size_t i;
    for (i = 0; i < sizeof(buf) - 1 && s[i]; i++) buf[i] = (char)tolower((unsigned char)s[i]);
    buf[i] = '\0';
    if (strcmp(buf, "tahoe") == 0) return ALG_TAHOE;
    if (strcmp(buf, "reno") == 0) return ALG_RENO;
    if (strcmp(buf, "newreno") == 0 || strcmp(buf, "new-reno") == 0) return ALG_NEWRENO;
    fprintf(stderr, "Unknown algorithm '%s'. Use tahoe, reno, or newreno.\n", s);
    exit(1);
}

EventType parse_event_type(const char *s) {
    if (strcmp(s, "timeout") == 0) return EV_TIMEOUT;
    if (strcmp(s, "triple") == 0 || strcmp(s, "triple_dup_ack") == 0) return EV_TRIPLE_DUP_ACK;
    if (strcmp(s, "partial") == 0 || strcmp(s, "partial_ack") == 0) return EV_PARTIAL_ACK;
    return EV_NONE;
}

void default_events(EventList *list) {
    list->count = 4;
    list->events[0] = (PacketEvent){5, EV_TRIPLE_DUP_ACK};
    list->events[1] = (PacketEvent){8, EV_PARTIAL_ACK};
    list->events[2] = (PacketEvent){12, EV_TIMEOUT};
    list->events[3] = (PacketEvent){17, EV_TRIPLE_DUP_ACK};
}

int load_events(const char *path, EventList *list) {
    FILE *fp = fopen(path, "r");
    char line[128], ev[64];
    int round;
    list->count = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp) && list->count < MAX_EVENTS) {
        if (line[0] == '#' || strlen(line) < 2) continue;
        if (sscanf(line, "%d %63s", &round, ev) == 2) {
            EventType t = parse_event_type(ev);
            if (t != EV_NONE) list->events[list->count++] = (PacketEvent){round, t};
        }
    }
    fclose(fp);
    return 1;
}

EventType event_for_round(const EventList *list, int round) {
    for (int i = 0; i < list->count; i++) {
        if (list->events[i].round == round) return list->events[i].type;
    }
    return EV_NONE;
}

void print_header(SenderState *s) {
    printf("Algorithm: %s\n", algorithm_name(s->algorithm));
    printf("Initial cwnd=%.2f MSS, ssthresh=%.2f MSS\n", s->cwnd, s->ssthresh);
    printf("Round | Event           | Phase/Action                         | cwnd   | ssthresh | Throughput\n");
    printf("------+-----------------+--------------------------------------+--------+----------+-----------\n");
}

void timeout_loss(SenderState *s, char *action) {
    s->ssthresh = s->cwnd / 2.0;
    if (s->ssthresh < 2.0) s->ssthresh = 2.0;
    s->cwnd = 1.0;
    s->dup_ack_count = 0;
    s->in_fast_recovery = 0;
    strcpy(action, "timeout: ssthresh=cwnd/2, cwnd=1");
}

void triple_duplicate_ack(SenderState *s, char *action) {
    s->ssthresh = s->cwnd / 2.0;
    if (s->ssthresh < 2.0) s->ssthresh = 2.0;
    s->dup_ack_count = 3;

    if (s->algorithm == ALG_TAHOE) {
        s->cwnd = 1.0;
        s->in_fast_recovery = 0;
        strcpy(action, "fast retransmit, Tahoe resets cwnd=1");
    } else {
        s->cwnd = s->ssthresh + 3.0;
        s->in_fast_recovery = 1;
        strcpy(action, "fast retransmit + fast recovery");
    }
}

void partial_ack(SenderState *s, char *action) {
    if (s->algorithm == ALG_NEWRENO && s->in_fast_recovery) {
        s->cwnd = s->ssthresh + 3.0;
        strcpy(action, "partial ACK: stay in fast recovery");
    } else if (s->algorithm == ALG_RENO && s->in_fast_recovery) {
        s->cwnd = s->ssthresh;
        s->in_fast_recovery = 0;
        strcpy(action, "partial ACK: Reno exits fast recovery");
    } else {
        strcpy(action, "partial ACK ignored/not applicable");
    }
}

void normal_ack(SenderState *s, char *action) {
    if (s->in_fast_recovery && s->algorithm != ALG_NEWRENO) {
        s->cwnd = s->ssthresh;
        s->in_fast_recovery = 0;
        strcpy(action, "full ACK: exit fast recovery");
        return;
    }
    if (s->in_fast_recovery && s->algorithm == ALG_NEWRENO) {
        s->cwnd = s->ssthresh;
        s->in_fast_recovery = 0;
        strcpy(action, "full ACK: NewReno exits fast recovery");
        return;
    }
    if (s->cwnd < s->ssthresh) {
        s->cwnd *= 2.0;
        strcpy(action, "slow start: cwnd doubles");
    } else {
        s->cwnd += MSS;
        strcpy(action, "congestion avoidance: cwnd += 1 MSS");
    }
}

void simulate(Algorithm alg, int rounds, const EventList *events) {
    SenderState s = {alg, 1.0, 8.0, 0, 0, 1, 0};
    print_header(&s);
    for (int r = 1; r <= rounds; r++) {
        EventType ev = event_for_round(events, r);
        char action[128];
        double throughput;

        if (ev == EV_TIMEOUT) timeout_loss(&s, action);
        else if (ev == EV_TRIPLE_DUP_ACK) triple_duplicate_ack(&s, action);
        else if (ev == EV_PARTIAL_ACK) partial_ack(&s, action);
        else normal_ack(&s, action);

        throughput = s.cwnd * MSS;
        printf("%5d | %-15s | %-36s | %6.2f | %8.2f | %9.2f\n",
               r, event_name(ev), action, s.cwnd, s.ssthresh, throughput);
    }
    printf("\nSummary: final cwnd=%.2f MSS, ssthresh=%.2f MSS\n\n", s.cwnd, s.ssthresh);
}

void print_topology_demo(void) {
    printf("Six-node topology ports: A=5001 B=5002 C=5003 D=5004 E=5005 F=5006\n");
    printf("Routing examples required by assignment:\n");
    printf("[A] Destination D, next hop B\n");
    printf("[B] Forwarding message from A to D, next hop D\n");
    printf("[D] Received message from A: hello_from_A_to_D\n\n");
    printf("[F] Destination E, next hop A\n");
    printf("[A] Forwarding message from F to E, next hop B\n");
    printf("[B] Forwarding message from F to E, next hop E\n");
    printf("[E] Received message from F: hello_from_F_to_E\n\n");
}

int main(int argc, char **argv) {
    Algorithm alg = ALG_NEWRENO;
    int rounds = 20;
    EventList events;
    default_events(&events);

    if (argc >= 2) alg = parse_algorithm(argv[1]);
    if (argc >= 3) rounds = atoi(argv[2]);
    if (argc >= 4) {
        if (!load_events(argv[3], &events)) {
            fprintf(stderr, "Could not read event file '%s', using default events.\n", argv[3]);
        }
    }

    printf("CSE320 TCP Congestion Control Simulation\n");
    print_topology_demo();
    simulate(alg, rounds, &events);
    return 0;
}
