#ifndef HOST_ALTCP_H
#define HOST_ALTCP_H
struct altcp_pcb { unsigned no_delay; };
void altcp_nagle_disable(struct altcp_pcb *conn);
#endif
