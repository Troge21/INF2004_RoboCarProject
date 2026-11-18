/* Minimal lwIP interfaces for offline tests of the actual MQTT port logic.
 * The ARM build separately checks against the real SDK headers. */
#ifndef HOST_MQTT_H
#define HOST_MQTT_H
#include <stdint.h>
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int err_t;
#define ERR_OK 0
#define ERR_MEM (-1)
#define MQTT_DATA_FLAG_LAST 1U
typedef int mqtt_connection_status_t;
#define MQTT_CONNECT_ACCEPTED 0
struct altcp_pcb;
struct mqtt_request_t;
typedef struct {
    int connected;
    struct altcp_pcb *conn;
    struct mqtt_request_t *pend_req_queue;
} mqtt_client_t;
typedef void (*mqtt_request_cb_t)(void *, err_t);
typedef void (*mqtt_connection_cb_t)(mqtt_client_t *, void *, mqtt_connection_status_t);
typedef void (*mqtt_incoming_publish_cb_t)(void *, const char *, u32_t);
typedef void (*mqtt_incoming_data_cb_t)(void *, const u8_t *, u16_t, u8_t);
struct mqtt_connect_client_info_t { const char *client_id; u16_t keep_alive; };
#endif
