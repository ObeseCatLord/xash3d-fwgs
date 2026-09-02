# VR usercmd sidecar v1

`NET_EXT_VR_USERCMD` is negotiated through the existing current-protocol
connect `ext` bitmask. A client writes sidecars only after the server echoes
that bit, so an older server never sees extra bytes; a newer server does not
parse them for an older client. GoldSrc protocol packets are unchanged.

The extension is placed after every delta-compressed command in `clc_move` and
before that move's checksum. This is the narrowest packet-local placement: it
keeps the payload under the existing checksum and lets the exact backup/new
command order define the sample order. It avoids adding a `clc_*` command,
changing `usercmd_t`, or using a separate reliable stream (which cannot replay
the pose that belongs to a recovered command).

Version 1 writes an envelope byte pair (`version`, `bytes-per-sample`) followed
by one fixed 35-byte record for each command, oldest through newest. A record
contains flags, two controller ladder angles, and the 15-value weapon/offhand
pose. Positions and velocity are relative to the player eye at 1/8 unit
precision; angles use 1/128 degree precision. Unknown versions are skipped by
the advertised record size. Truncated records drop the client before movement.

The engine keeps a sample next to each client command-ring entry and retains
the last accepted sample for dropped-command replay. PM receives it only via
optional begin/end exports around one dispatch; the base game DLL clears that
transient state immediately afterwards. `LADDER_VALID` is the wire policy: if
it is clear both prediction and the server use normal view angles, and if it
is set both use the encoded controller angles. A local server cvar never
overrides that negotiated per-command decision.

The optional `vr_client_api_t::BuildUsercmdSidecar` v2 tail lets a current
client DLL fill the final gameplay pose after its own weapon policy. On i386
Linux, the v2 base table remains 16 bytes (`Frame` at 8, `Shutdown` at 12) and
the optional callback is at offset 16, making the extended table 20 bytes.
The engine sets `struct_size` to its writable table size before the export;
the DLL returns the size it actually filled. Thus a 16-byte older DLL remains
valid. The callback receives a 20-byte independent usercmd view (final
viewangles and frametime), never `usercmd_t`, and may set only pose fields;
the engine preserves ladder fields. With no tail callback, the engine sends
only ladder data and leaves `POSE_VALID` clear.

The server's optional pose export updates the owning player's authoritative
pose before `PlayerPreThink`/`ItemPostFrame`; `updatevr` remains the fallback
while no fresh usercmd pose is available.
