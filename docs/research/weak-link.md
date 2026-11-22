# The page on a weak link

Why the page took a while to answer again after the owner had been far from the console, where requests were slow or didn't complete (the owner, 2026-11-22), and what could shorten it. For release 1.1.2. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Short answer:** there's no thread that stays busy. After the injection the firmware runs one loop on one core, and lwIP and its web server handle each connection through callbacks (D18). What blocks new requests is that abandoned connections keep the Pico's few TCP slots and send buffers for minutes:
- the page keeps polling while its earlier requests are still stuck;
- lwIP keeps resending their data with ever longer pauses before giving up;
- meanwhile new connections can't get a slot or a buffer [inf].

## 1. The mechanism

**The page** (`src/web/index.html`):
- `setInterval` calls `loadStatus()` every 5 s while the Status or Payloads tab is visible. It doesn't check whether the previous request has finished.
- `api()` aborts a request after 8 s.
- So on a weak link, requests overlap, and each aborted one leaves a connection behind on the Pico.

**The web server** (lwIP httpd, in the SDK's lwIP):
- `http_poll()` runs every `HTTPD_POLL_INTERVAL` (4 ticks of 500 ms, so 2 s). After `HTTPD_MAX_RETRIES` (4) polls without progress, about 8 s, it calls `http_close_conn()`.
- That's a graceful close: data the client hasn't acknowledged stays queued and is resent, so the connection isn't freed.

**lwIP's TCP** (`core/tcp.c`, `opt.h`):
- `TCP_MAXRTX` is 12: a connection is dropped after 12 retransmissions without an answer.
- Each retransmission doubles the time-out: `rto = base << tcp_backoff[nrtx]`, with shifts of 1, 2, 3 … 7 (×2 to ×128), capped at 0x7FFF ticks.
- With a base of 0.5 s the 12 tries take about 1 + 2 + 4 + 8 + 16 + 32 + 6 × 64 s, roughly 7 minutes, and longer when the measured round trip was slow [inf: the base comes from the connection's measured round trip].

**The limits** (`src/firmware/net/lwipopts.h`):
- 8 TCP connections (`MEMP_NUM_TCP_PCB`);
- 32 TCP segments for all of them (`MEMP_NUM_TCP_SEG`), with each connection's send buffer holding up to 8 (`TCP_SND_BUF` of 8 × 1460 bytes);
- 24 receive buffers (`PBUF_POOL_SIZE`), which out-of-order segments also hold (`TCP_QUEUE_OOSEQ`).

**No room for a new connection** (`tcp_alloc()`):
- A new connection finds room only by ending an old one in TIME-WAIT, LAST-ACK or CLOSING, or one of a lower priority.
- The web server gives every connection the same priority (`HTTPD_TCP_PRIO`, `TCP_PRIO_MIN`), so it can't push out a stuck one.

**So [inf]:** a few abandoned requests fill the segments, and more of them fill the 8 connections. When the link is good again, new requests are refused or starved until lwIP drops the stuck connections, which can take minutes.

## 2. The options

| | Fix | Where | Effect | Cost |
|---|---|---|---|---|
| A | One status request at a time, and back off after a failure: 10, 20, then 30 s, and back to 5 s after a success | the page | Far fewer abandoned connections; recovery limited by the few left [inf] | The status refreshes less often while the link is bad |
| B | Give up sooner: `TCP_MAXRTX` 12 → about 6 | lwIP option | A stuck connection frees its slot in about a minute instead of several [inf] | An upload over a weak link also gives up sooner; a network re-test with uploads |
| C | Reset instead of close a connection the web server gives up on | a patch to lwIP's httpd, or a small reaper in the main loop | Frees the slot at once | Changes lwIP's behaviour; needs care with uploads and downloads |
| D | More connections and segments (8 → 16) | lwIP options | More room before it fills | RAM; doesn't stop the pile-up |

**The owner's choice (2026-11-22): A, in release 1.1.2.** B to D stay open, if A isn't enough.

## 3. Testing it

Distance isn't repeatable, but lost packets are. The Mac can drop a share of its packets on purpose with Apple's Network Link Conditioner, part of "Additional Tools for Xcode", with a custom profile for packet loss. The test:
1. With loss on, for example 50%, the page stalls.
2. With loss off, time how long the page takes to answer again.
3. Do it on 1.1.1, and on 1.1.2. On 1.1.2 it should answer again sooner [inf].

**Sources:** the SDK's lwIP (`src/apps/http/httpd.c`: `http_poll()`; `src/core/tcp.c`: `tcp_backoff`, `tcp_slowtmr()`, `tcp_alloc()`, `tcp_kill_prio()`; `src/include/lwip/opt.h` and `apps/httpd_opts.h`), the firmware's `lwipopts.h`, and the page's `index.html`.
