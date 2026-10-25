# WebSocket heartbeat correction

The native `paper_transport_probe_2` capture records a completed repeated-listen
send followed by 5,023 ms, twenty empty polls and zero messages. Initial account
access, authorization, subscription and exact reconciliation succeed. The timeout
is therefore not explained by the earlier premature-empty-poll defect. No order
was submitted or canceled, and routing closed as required.

The previous design used subscription configuration as a liveness challenge.
Although Alpaca documents acknowledgments for listen messages, the native path
supplied no second acknowledgment. The capture does not identify whether the
cause lies at the provider or the native WebSocket implementation. Repeating that
same request or extending its timeout does not establish a reliable heartbeat.

## Transport and evidence

`PaperWebSocket` owns a pinned libcurl 8.23.0 WebSocket connection. This dependency
is needed because the previous WinHTTP WebSocket API does not expose application
PING/PONG payloads. WinHTTP continues to own REST. Windows WebSocket TLS uses
Schannel; Linux uses OpenSSL. Certificate/hostname verification stays enabled,
redirects stay disabled, and the production connection is fixed to the Alpaca
paper stream. Authentication remains inside the native wire; credentials are
excluded from captures, including credential echoes in control payloads.

A health check drains queued input, sends an RFC6455 PING containing a
connection-scoped sequence/monotonic-clock correlation token, and requires an
identical PONG payload within the configured five-second budget. Tokens are
correlation identifiers, not authentication secrets; TLS authenticates the peer.
Old/unmatched PONGs cannot satisfy a challenge. A process/session reconnect closes
the old socket and clears its queues. The active subscription is not changed.

Trade/account JSON messages arriving during the check are captured and applied
through the same schema-3 bridge. PING, matching/unmatched PONG, send completion,
phase and receipt clocks are retained separately. A matching PONG is followed
by the existing exact snapshot reconciliation. It proves a bounded transport
round trip to the peer, not complete execution-event delivery, atomic snapshots,
market-feed freshness or profitability. Failed/missing/late PONGs close routing.

The socket owner handles partial sends, fragmented/chunked binary or text data,
interleaved server PINGs, PONGs and peer closure. Server PINGs receive bounded
PONG replies. The existing one-MiB/4,096-chunk assembly bounds remain, with
125-byte control bounds and a sixteen-PONG queue bound. No state is reset to
make reconciliation pass, and no order retry is introduced.

## Versioning and validation

New control journals use schema 2, `paper_control_stop_deadline_v2`, with an
explicit `websocket_ping_pong_v1` health model. Stop/deadline command semantics
remain unchanged. Schema-1 control journals can still be replayed and recovered
stopped; they are not silently relabeled or rearmed. Financial journal schemas
1-3 and the authoritative accounting engine are unchanged.

The same production socket owner is exercised through real loopback RFC6455
connections: matching/wrong PONGs, server PING response, fragmented data around
control frames, quiet timeout, peer close, oversized data and redirect refusal.
A private test access class permits only a loopback URL, carries no credentials
and is not used by the native wire. These tests exercise the actual library and
framing path, supplementing the constructed session/policy cases.

When Python is available, CTest registers `paper_websocket_loopback` as optional
`integration`. It uses only Python's standard library as a local peer and never
contacts a broker. Full routine/quick selections remain 26/22; the optional wire
test runs separately. Windows Build All compiles its C++ client without requiring
Python. The first configure/build fetches and builds the pinned static library;
subsequent builds reuse it. No separate curl installation is required.

The complete Windows `paper_transport_probe_3` archive accepts the read-only
heartbeat path for `03515a6`. Its recorded challenge and matching PONG have
identical payloads, with 340 ms between their local monotonic receipts. This
includes local capture/processing time and is not a network-only latency measure.
All thirteen REST requests are GETs with HTTP 200; two snapshot collections
before the challenge and two afterward match the exact flat-account ledger.
The connection closes normally after the accepted checkpoint.

Independent verification checks artifact/capture hashes, financial inputs and
source identities. The native quick-test transcript reports 22/22 groups passing.
The [ledger](progress.md#native-pingpong-acceptance) records counts, archive
identities and remaining operational dependencies.

This is one observed native round trip, not sustained-connection or observed
order-lifecycle evidence. Both the heartbeat mechanism and WebSocket owner
changed, so the successful run does not isolate the cause of the old missing
subscription acknowledgment. Failed captures remain part of the evidence.

## Primary references

- [RFC6455 Ping/Pong](https://www.rfc-editor.org/rfc/rfc6455#section-5.5.2).
- [libcurl WebSocket interface](https://curl.se/libcurl/c/libcurl-ws.html).
- [libcurl send semantics](https://curl.se/libcurl/c/curl_ws_send.html).
- [libcurl receive semantics](https://curl.se/libcurl/c/curl_ws_recv.html).
- [Alpaca streaming](https://docs.alpaca.markets/us/docs/websocket-streaming).
