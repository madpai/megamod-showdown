#!/usr/bin/env python3
"""udprelay.py LISTEN_PORT TARGET_PORT: a UDP relay on 127.0.0.1.
The Android emulator's UDP port forward answers from a random source port;
the game's client (rightly) drops packets not from the server's address.
Join 127.0.0.1:LISTEN_PORT; this forwards to TARGET_PORT and hands every
reply back from LISTEN_PORT. Each client gets its own upstream socket, so
the host sees several joiners as several peers (X2's two-joiner test)."""
import select, socket, sys
lp, tp = int(sys.argv[1]), int(sys.argv[2])
front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); front.bind(("127.0.0.1", lp))
up, client_of = {}, {}           # client address -> upstream socket, and back
while True:
    for s in select.select([front, *client_of], [], [])[0]:
        data, addr = s.recvfrom(65536)
        if s is front:
            back = up.get(addr)
            if back is None:
                back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); back.bind(("127.0.0.1", 0))
                up[addr], client_of[back] = back, addr
            back.sendto(data, ("127.0.0.1", tp))
        else:
            front.sendto(data, client_of[s])
