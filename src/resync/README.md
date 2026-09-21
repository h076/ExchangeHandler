# Resync Layer (Layer Three)

This layer will detect sequence gaps that might occur on the wire. 

On a gap it should fetch the snapshot, and buffer + reconile incrementals against snapshot seq.
