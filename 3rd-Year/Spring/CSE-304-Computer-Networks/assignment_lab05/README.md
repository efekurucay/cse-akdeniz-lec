# CSE320 TCP Congestion Control Assignment

This project simulates sender-side TCP congestion window behavior for:

- TCP Tahoe
- TCP Reno
- TCP NewReno

It demonstrates slow start, congestion avoidance, timeout-based loss, triple duplicate ACK fast retransmit, and fast recovery where applicable.

## Files

- `tcp_cc_sim.c` — single C executable source code
- `events.conf` — configurable packet loss / ACK event pattern
- `A.conf` ... `F.conf` — six-node topology configuration files requested in the assignment

## Compile

```bash
gcc tcp_cc_sim.c -o tcp_cc_sim
```

## Run

```bash
./tcp_cc_sim tahoe 20 events.conf
./tcp_cc_sim reno 20 events.conf
./tcp_cc_sim newreno 20 events.conf
```

Arguments:

1. Algorithm: `tahoe`, `reno`, or `newreno`
2. Number of simulation rounds
3. Event configuration file

## Event Configuration

`events.conf` format:

```text
round event
```

Supported events:

- `timeout`
- `triple_dup_ack`
- `partial_ack`

Example:

```text
5 triple_dup_ack
8 partial_ack
12 timeout
17 triple_dup_ack
```

## Six-Node Topology Demonstration

Nodes and ports:

- A = 5001
- B = 5002
- C = 5003
- D = 5004
- E = 5005
- F = 5006

Expected route from A to D:

```text
[A] Destination D, next hop B
[B] Forwarding message from A to D, next hop D
[D] Received message from A: hello_from_A_to_D
```

A to D must use A -> B -> D because:

- A -> D direct cost = 13
- A -> B -> D cost = 4 + 8 = 12

Expected route from F to E:

```text
[F] Destination E, next hop A
[A] Forwarding message from F to E, next hop B
[B] Forwarding message from F to E, next hop E
[E] Received message from F: hello_from_F_to_E
```

## Experimental Analysis

All algorithms start with `cwnd = 1 MSS` and `ssthresh = 8 MSS`.

### Tahoe

Tahoe performs slow start and congestion avoidance. On packet loss, including triple duplicate ACK, it sets `ssthresh = cwnd / 2` and resets `cwnd = 1 MSS`. This causes slower recovery after loss.

### Reno

Reno improves Tahoe by using fast retransmit and fast recovery on triple duplicate ACK. Instead of restarting from `cwnd = 1`, it reduces the window and continues from around `ssthresh`, recovering faster than Tahoe.

### NewReno

NewReno improves Reno during fast recovery. When a partial ACK arrives, NewReno stays in fast recovery and continues retransmitting lost packets instead of leaving fast recovery too early. This gives better behavior when multiple packets are lost in one window.

## Demonstration Video Checklist

The video should show:

1. Compiling the program with `gcc tcp_cc_sim.c -o tcp_cc_sim`
2. Running at least two algorithms, for example:
   - `./tcp_cc_sim tahoe 20 events.conf`
   - `./tcp_cc_sim reno 20 events.conf`
   - `./tcp_cc_sim newreno 20 events.conf`
3. The printed congestion window evolution step by step
4. The difference in behavior after timeout and triple duplicate ACK events
