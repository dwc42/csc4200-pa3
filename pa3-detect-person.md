# Program 3 — Detect a Person

> **CSC 4200 | Implementation language: C / C++**

---

## Objectives

The objective of this assignment is to design and implement a client-server system using Raspberry Pi devices. The server will control an LED, while the client will sense motion using a Passive Infrared Sensor (PIR) and communicate with the server to blink the LED. The assignment includes establishing a three-way handshake, sending blink duration and count information, acknowledging the data, and responding to motion detection by blinking the LED. The client and server communications **MUST use UDP (SOCK_DGRAM) and NOT TCP.**

The objectives are:

- Learn about physical computing
- Learn about protocol development

> **What this means:** You are building two programs that run on separate Raspberry Pis (or the same one for testing). One Pi runs a *server* with an LED wired up. The other runs a *client* with a PIR motion sensor. When the PIR sees someone walk by, the client tells the server over the network, and the server blinks the LED. All communication uses UDP — you build the handshake and reliability logic yourself, not TCP.

---

## Server Specifications

The server (we name it `lightserver`) takes two arguments:

```
$ lightserver -p <PORT> -s <LOG FILE LOCATION>
```

1. `PORT` — The port server listens on.
2. `Log File Location` — log file location

> **Expanded:** Parse the `-p` and `-s` flags using `getopt()` from `<getopt.h>`. Validate that the port number is between 1 and 65535 — if not, print an error and exit with a non-zero code. Pick any port above 1024 to avoid needing root privileges for binding.

---

## Server's Functional Requirements

1. The server must open a UDP socket on the specified port number
2. The server should gracefully process incorrect port number and exit with a non-zero error code
3. The server runs indefinitely — it does not exit.
4. The server accepts connections from multiple clients *(bonus points)*
5. Server works with any client developed by other teams *(bonus points)*

> **Expanded notes:**
> - **Req 1:** Use `AF_INET` and `SOCK_DGRAM` when creating the socket. Bind to `INADDR_ANY` so it accepts from any interface.
> - **Req 2:** Wrap the bind call in error checking and call `exit(EXIT_FAILURE)` on failure — this satisfies the non-zero exit requirement.
> - **Req 3:** Use an infinite loop around `recvfrom()`.
> - **Req 4 (bonus):** Track each client by its IP and port address — each is an independent session with its own sequence numbers and blink parameters.
> - **Req 5 (bonus):** Achievable if you follow the exact packet spec below. Coordinate with other teams to test.

---

## Client Specifications

The client (we name it `lightclient`) takes three arguments:

```
$ lightclient -s <SERVER-IP> -p <PORT> -l LOGFILE
```

The client takes three arguments:

1. `Server IP` — The IP address of the lightserver.
2. `PORT` — The port the server listens on.
3. `Log file location` — Where you will keep a record of packets you received.

```
For example:
$ lightclient -s 192.168.2.1 -p 6543 -l LOGFILE
```

> **Expanded:** Find your server Pi's IP by running `hostname -I` on it. The client does not need to call `bind()` — the OS automatically assigns an ephemeral source port on the first send. Both Pis must be on the same network (same Wi-Fi or Ethernet switch).

---

## Packet Specification

The payload of each UDP packet sent by server and client MUST start with the following 12-byte header. All fields are in network order (most significant bit first):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                     Sequence Number                           |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                     Acknowledgment Number                     |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                     Not Used                            |A|S|F|
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  |                                                               |
  |                        Payload                               |
  |                                                               |
  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Where:

1. **Sequence Number (32 bits):** If SYN is present (the S flag is set) the sequence number is the initial sequence number (randomly chosen).
2. **Acknowledgement Number (32 bits):** If the ACK bit is set, this field contains the value of the next sequence number the sender of the segment is expecting to receive. Once a connection is established this is always sent.
3. The acknowledgement number is given in the unit of bytes (how many bytes you have sent)
4. **Not Used (29 bits):** Must be zero.
5. **A (ACK, 1 bit):** Indicates that the value of Acknowledgment Number field is valid
6. **S (SYN, 1 bit):** Synchronize sequence numbers
7. **F (FIN, 1 bit):** Finish, No more data from sender

> **Expanded — field breakdown:**
>
> | Field | Size | C function to use | Notes |
> |---|---|---|---|
> | Sequence Number | 32 bits (4 bytes) | `htonl()` on send, `ntohl()` on recv | Random initial value when SYN is set; increments by payload byte count |
> | Acknowledgment Number | 32 bits (4 bytes) | `htonl()` / `ntohl()` | Only meaningful when ACK flag is set. Value = other side's seq + bytes received |
> | Flags word | 32 bits (4 bytes) | `htonl()` / `ntohl()` | Only lowest 3 bits used: bit 2 = ACK, bit 1 = SYN, bit 0 = FIN. All other bits zero. |
> | Payload | variable | raw bytes | Empty during handshake; blink params or `:MotionDetected` string after |
>
> **Flag values:** ACK only = 4, SYN only = 2, SYN+ACK = 6, FIN = 1. Total header = 12 bytes (three 4-byte unsigned ints packed big-endian).
>
> ⚠️ **Alignment warning:** Never cast a raw receive buffer directly to a struct and read integers on ARM (the Pi's CPU) — this can cause undefined behavior due to alignment. Always use `ntohl()` on each field individually after receiving.

---

## This is the protocol you will implement

### Step 1 — Three-Way Handshake

The client opens a UDP socket and initiates a 3-way handshake to the specified hostname/ip and port. Essentially, the client and server will exchange three packets with the following flags set: (1) SYN (2) SYN|ACK (3) ACK. At the end of the handshake, they will have learned each other's sequence number.

> **Expanded:** The client picks a random 32-bit initial sequence number (ISN) using `rand()` seeded with `time(NULL)`. It sends a SYN packet with that ISN and ack = 0. The server picks its own random ISN and replies with SYN+ACK, setting ack = client ISN + 1. The client completes the handshake with an ACK, setting ack = server ISN + 1. After this exchange, both sides know each other's starting sequence numbers and can track byte counts going forward.

### Step 2 — Client Sends Blink Parameters

The client then sends the duration and number of blinks as a *payload*.

> **Expanded:** Pack the blink duration (in milliseconds) and blink count as the packet payload — two unsigned 16-bit integers in network byte order is a clean approach. Include the 12-byte header with the ACK flag set and correct sequence/ack numbers. Advance your sequence number by the number of payload bytes sent (4 bytes in this case).

### Step 3 — Server Acknowledges Blink Parameters

The server acknowledges the duration and the number of blinks by sending back the duration and number of blinks. It then waits for the client to sense motion.

> **Expanded:** The server parses and stores the blink configuration, logs it, then echoes the same duration and count back in its own ACK packet as confirmation. After sending the echo, the server blocks on `recvfrom()` waiting for the `:MotionDetected` message.

### Step 4 — Client Senses Motion

The client senses motion using the PIR. See here for details on how to connect the PIR to the PI: https://projects.raspberrypi.org/en/projects/physical-computing/11

> **Expanded:** Using WiringPi or pigpio, configure the PIR output GPIO pin as an input and poll it in a loop. The PIR outputs HIGH (3.3V) on its OUT pin when motion is detected. Allow 30–60 seconds for the sensor to warm up before starting the polling loop — it emits spurious HIGH signals during this stabilization period. A poll interval of 100 ms is a good balance between responsiveness and CPU usage.

### Step 5 — Client Sends MotionDetected

When motion is detected, the client logs it, and sends a message with the following string as the payload *:MotionDetected*

> **Expanded:** Log the detection event first with a timestamp in `YYYY-MM-DD-HH-MM-SS` format using `strftime()` and `localtime()`. Then build a packet with the ACK flag set and the raw string `:MotionDetected` as the payload bytes. Advance your sequence number by the byte length of that string (15 bytes).

### Step 6 — Server Blinks LED

The server parses it, logs *:MotionDetected* to its log, and drives (blinks) the LED for the pre-determined amount of times.

> **Expanded:** Check that the payload matches `:MotionDetected`, write the event to the server log with a timestamp, then toggle the LED GPIO pin HIGH and LOW in a loop for the agreed count and duration. WiringPi's `delay()` function works in milliseconds and is the simplest way to time each blink.

### Step 7 — Client Sends FIN

Client sends a packet with the FIN bit set. The server logs ":Interaction with completed." This finishes the interaction.

> **Expanded:** Build a packet with only the FIN flag set and no payload. The server, upon receiving it, logs the completion message including the client's IP address and port number, then either exits (single-client mode) or loops back to `recvfrom()` to wait for the next handshake (multi-client bonus).

### Step 8 — Timestamp Format

Timestamp format is `"YYYY-MM-DD-HH-MM-SS"`.

> **Expanded:** Use `strftime()` with the format string `"%Y-%m-%d-%H-%M-%S"` applied to the result of `localtime()`.

---

## Hardware Components

- Raspberry Pi
- Passive Infrared Sensor (PIR)
- LED
- Resistor
- Jumper wires
- Breadboard

> **Wiring guide:**
>
> | Component | Pi Pin | Notes |
> |---|---|---|
> | PIR VCC | 5V (Pin 2) | Powers the sensor |
> | PIR GND | GND (Pin 6) | |
> | PIR OUT | GPIO 4 (Pin 7) | WiringPi pin 7 — update your pin define to match |
> | LED anode (+) | GPIO 17 (Pin 11) | In series with a 330Ω resistor |
> | LED cathode (−) | GND (Pin 9) | |

### Server Setup

- The server Raspberry Pi should be programmed to drive the LED.
- It should be capable of receiving data from the client and blinking the LED accordingly.

> Install WiringPi with `sudo apt install wiringpi`. Compile with `-lwiringPi`. Run with `sudo` — required for GPIO access on most Pi OS versions.

### Client Setup

- The client Raspberry Pi should be programmed to interface with the PIR sensor.
- It should be able to establish a connection with the server.
- The client should continuously sense motion using the PIR sensor.

> Compile and run with the same flags as the server. When testing locally, use `127.0.0.1` as the server IP.

**You may test both server and client on the same board.**

---

## Submission

1. Submit your code, packet capture in PCAP format, and your logs as a ZIP file.

> Capture packets during a run with `sudo tcpdump -i any udp port 6543 -w capture.pcap`, then stop it with Ctrl+C when the interaction is complete. Include both `server.log` and `client.log` in the ZIP alongside your source files and a `Makefile` so graders can build with a single `make`.

---

## Additional Requirements

1. Code must compile/run on the PIs.
2. For each packet received, log both at server and receiver in the following format:

```
"RECV" <Sequence Number> <Acknowledgement Number> ["ACK"] ["SYN"] ["FIN"]
"SEND" <Sequence Number> <Acknowledgement Number> ["ACK"] ["SYN"] ["FIN"]
```

> Only include flag labels that are actually set — omit the others. For example, a SYN-only packet logs as `SEND 1000 0 SYN`. A SYN+ACK logs as `RECV 5000 1001 SYN ACK`. Flush the log file after every write so nothing is lost if the program crashes. Write a dedicated logging helper function so the format stays consistent throughout both programs.

---

## Hints

```python
def create_packet(**kwargs):
    data = struct.pack('!I', s_n)   # pack the sequence number
    ....
    data += struct.pack("!c", ack)  # pack the ACK
    data += struct.pack("!c", syn)  # pack the SYN
    data += struct.pack("!c", fin)  # pack the FIN
    ....
    return data

send_data = create_packet(sequence_number=100, ack_number=0, ack='Y', syn='N', fin='N', payload=data)
```

> **In C:** Write a `build_packet()` function that accepts seq, ack, a flags integer, and an optional payload buffer. It packs the three 32-bit header fields using `htonl()`, appends the payload bytes, and returns the total byte length. Pair it with a `log_packet()` function so every send and receive is logged consistently in the required format.

---

## Rubric

- Protocol design points 1–7 — 10 points each.
- Successful reading from PIR — 10 points.
- Successful LED blinking — 10 points.
- Server can handle multiple clients — 10 points *(bonus)*
- Works with other teams' implementation — 10 points *(bonus)*
  - Do not collaborate on code but test with each other's code
  - If your code works with another team's code, you both get 10 points

> **Points summary:** Protocol steps 1–7 = 70 pts + PIR = 10 pts + LED = 10 pts → **90 pts base**. Both bonus items = up to 20 extra points. The easiest points to lose are incorrect logging format and wrong flag bit packing — test those early.

---

## PIR Sensor Sensitivity — How to Adjust It

Most HC-SR501 PIR modules (the standard one used with Pi projects) have **two orange potentiometers** and a **jumper** on the underside of the board. These let you tune how the sensor behaves without touching your code.

### The two potentiometers

| Knob | Label | Controls | Range |
|---|---|---|---|
| Left (closer to dome edge) | Sx / Sensitivity | Detection distance | ~3 m (full CCW) to ~7 m (full CW) |
| Right (closer to pins) | Tx / Time delay | How long OUT stays HIGH after motion stops | ~3 s (full CCW) to ~300 s (full CW) |

For this assignment, turn the **Tx knob all the way counter-clockwise** (minimum delay ~3 s). This means after motion stops, the sensor goes LOW again quickly — useful for repeated testing without waiting minutes. Turn **Sx to about the middle** to start; adjust if it isn't triggering or is triggering too easily.

### The trigger mode jumper

The small jumper between the two potentiometers selects one of two modes:

| Jumper position | Mode | Behavior |
|---|---|---|
| H (pins 1–2) | Repeatable trigger | OUT stays HIGH and resets its timer as long as motion continues. Best for this project. |
| L (pins 2–3) | Non-repeatable trigger | OUT goes HIGH once, then LOW after the time delay, regardless of ongoing motion. |

Set the jumper to **H** for this assignment. In L mode, if someone is standing in front of the sensor, OUT goes LOW after the delay and your client's polling loop would stop detecting — you'd miss continuous presence. H mode keeps OUT HIGH as long as motion is detected, which maps naturally to a polling loop.

### Warm-up time

The PIR takes **30–60 seconds after power-on** to stabilize. During this period it emits spurious HIGH pulses. Build in a delay before your polling loop starts — either as a `sleep()` call in your program, or simply power the Pi and wait before launching the client.

### Software debounce (fallback)

If you cannot physically adjust the potentiometers, add a simple debounce in your polling loop: require the pin to stay HIGH for several consecutive reads before treating it as confirmed motion. A poll interval of 100 ms and a threshold of 2–3 consecutive HIGHs (200–300 ms total) is a good starting point. Increase the threshold to reduce false triggers; decrease it if real motion is not being caught quickly enough.
