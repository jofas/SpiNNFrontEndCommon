Static version:
Relies on static window sliding. Always retransmit all the window in case of ACK not received.

Dynamic version:
Window sliding is dynamic(not based on the window sliding size defined a priori) if some part of the window is received. In some cases retransmits less packets. Adds some overhead on board side while receiving each ACK.

Performances:
Equal in most cases. In a lossy network the dynamic could give slightly better performances.