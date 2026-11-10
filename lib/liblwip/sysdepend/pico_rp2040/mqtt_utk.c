/*
 *	mqtt_utk.c
 *	MQTT client wiring for micro T-Kernel (RP2040, NO_SYS=1)
 *
 *	Runs entirely on the single processor-1-owned task that already owns
 *	the CYW43439 and the rest of lwIP (docs/RELEASE.md safety rule 6):
 *	cyw43_utk_task() (cyw43_utk.c) reports IP availability each poll and
 *	calls the guarded mqtt_utk_start() while up. Keepalive/ping/retry
 *	timers, and the heartbeat below, both ride the same sys_check_timeouts()
 *	polling that task already does for DHCP and TCP -- nothing extra runs
 *	on any other task or core.
 *
 *	This file does not include tk/tkernel.h or any pico-sdk/cyw43 header
 *	(unlike cyw43_utk.c and sys_arch.c), so it isn't subject to either
 *	file's size_t workaround -- tm/tmonitor.h alone is namespace-safe here.
 */

#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include <string.h>
#include "wifi_secrets.h"
#include "mqtt_utk.h"
#include <tm/tmonitor.h>

#define MQTT_UTK_HEARTBEAT_MS   5000
#define MQTT_UTK_KEEPALIVE_S    5
#define MQTT_UTK_RETRY_MS       3000
#define MQTT_UTK_CONNECT_DEADLINE_MS 5000
#ifndef MQTT_TELEMETRY_TOPIC
#define MQTT_TELEMETRY_TOPIC MQTT_CLIENT_ID "/telemetry"
#endif

#ifndef MQTT_COMMAND_TOPIC
#define MQTT_COMMAND_TOPIC MQTT_CLIENT_ID "/command"
#endif
#ifndef MQTT_STATUS_TOPIC
#define MQTT_STATUS_TOPIC MQTT_CLIENT_ID "/status"
#endif
/* One five-byte slot, owned exclusively by the network task. Low three
 * bits hold receive length 0..5, or 7 while WiFi is unavailable; the
 * remaining flags share the same byte. */
#define CMD_LENGTH 7U
#define CMD_LINK_DOWN 7U /* Reserved receive-length code; no extra flag. */
#define CMD_COLLECTING 8U
#define CMD_PENDING 16U
#define CMD_SUBSCRIBED 32U
#define CMD_RATE_SEEN 64U
#define CMD_CONNECTING 128U
#define STATUS_MIN_INTERVAL_MS 250U
static mqtt_client_t *client;
static ip_addr_t broker_addr;
static u8_t command_state = CMD_LINK_DOWN;
static u8_t command_bytes[S1_STATUS_REQUEST_BYTES];
static u32_t last_status_ms;

static void heartbeat_timeout(void *arg);
static void publish_done_cb(void *arg, err_t result);
static void subscribe_commands(void *arg);
static void retry_timeout(void *arg);

static int link_available(void)
{
    return (command_state & CMD_LENGTH) != CMD_LINK_DOWN;
}
static void retry_later(void)
{
    if(!link_available()) return;
    /* CONNECTING also guards the backoff period, preventing a 2 ms loop
     * from hammering connect after a synchronous error. One retry timer. */
    command_state |= CMD_CONNECTING;
    sys_untimeout(retry_timeout, NULL);
    sys_timeout(MQTT_UTK_RETRY_MS, retry_timeout, NULL);
}
void mqtt_utk_set_link(unsigned int available)
{
    if(available) {
        if(!link_available()) command_state = 0;
        return;
    }
    if(!link_available()) return;
    command_state = CMD_LINK_DOWN;
    sys_untimeout(retry_timeout, NULL);
    sys_untimeout(subscribe_commands, NULL);
    sys_untimeout(heartbeat_timeout, NULL);
    if(client != NULL) mqtt_disconnect(client);
}

void mqtt_utk_poll(void)
{
    UB payload[S1_FRAME_BYTES];
    int len = s1_network_poll(payload, sizeof(payload));
    err_t err;
    /* Drain old telemetry while disconnected; STATUS also uses this fresh
     * network-owned cache, without calling drivers or waiting on sensors. */
    if(!link_available() || client == NULL || !mqtt_client_is_connected(client)) {
        command_state &= (u8_t)~(CMD_COLLECTING | CMD_PENDING | CMD_SUBSCRIBED);
        return;
    }
    if(len > 0) {
        err = mqtt_publish(client, MQTT_TELEMETRY_TOPIC, payload, (u16_t)len,
                           0, 0, publish_done_cb, NULL);
        if(err != ERR_OK) tm_printf((UB*)"mqtt: telemetry dropped, err=%d\n", (int)err);
    }
    if(command_state & CMD_PENDING) {
        UB reply[S1_STATUS_MAX_BYTES];
        command_state &= (u8_t)~CMD_PENDING;
        len = s1_network_status(command_bytes, sizeof(command_bytes), reply, sizeof(reply));
        if(len > 0) {
            err = mqtt_publish(client, MQTT_STATUS_TOPIC, reply, (u16_t)len,
                               0, 0, publish_done_cb, NULL);
            tm_printf((UB*)"mqtt: STATUS reply %u bytes, result=%d\n",
                      (unsigned)len, (int)err);
        }
    }
}

static void incoming_publish_cb(void *arg, const char *topic, u32_t total_len)
{
    (void)arg;
    if(!link_available()) return;
    command_state &= (u8_t)~(CMD_LENGTH | CMD_COLLECTING);
    if((command_state & CMD_SUBSCRIBED) && !(command_state & CMD_PENDING) &&
        total_len == S1_STATUS_REQUEST_BYTES && strcmp(topic, MQTT_COMMAND_TOPIC) == 0)
        command_state |= CMD_COLLECTING;
}
static void incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    unsigned count = command_state & CMD_LENGTH, i;
    uint32_t id;
    u32_t now;
    (void)arg;
    if(!link_available() || !(command_state & CMD_COLLECTING)) return;
    if(len > S1_STATUS_REQUEST_BYTES - count) {
        command_state &= (u8_t)~CMD_COLLECTING;
        return;
    }
    for(i = 0; i < len; ++i) command_bytes[count++] = data[i];
    command_state = (u8_t)((command_state & (u8_t)~CMD_LENGTH) | count);
    if(!(flags & MQTT_DATA_FLAG_LAST)) return;
    command_state &= (u8_t)~CMD_COLLECTING;
    if(s1_parse_status_request(command_bytes, count, &id) != S1_CODEC_OK) return;
    now = sys_now();
    if((command_state & CMD_RATE_SEEN) && (u32_t)(now - last_status_ms) < STATUS_MIN_INTERVAL_MS)
        return;
    last_status_ms = now;
    command_state |= CMD_PENDING | CMD_RATE_SEEN;
}
static void subscribe_done_cb(void *arg, err_t result)
{
    (void)arg;
    if(!link_available()) return;
    if(result == ERR_OK) {
        command_state |= CMD_SUBSCRIBED;
        tm_printf((UB*)"mqtt: subscribed to " MQTT_COMMAND_TOPIC " (read-only STATUS)\n");
    } else {
        command_state &= (u8_t)~CMD_SUBSCRIBED;
        sys_untimeout(subscribe_commands, NULL);
        sys_timeout(MQTT_UTK_RETRY_MS, subscribe_commands, NULL);
    }
}
static void subscribe_commands(void *arg)
{
    err_t err;
    (void)arg;
    if(!link_available() || client == NULL || !mqtt_client_is_connected(client) || (command_state & CMD_SUBSCRIBED)) return;
    err = mqtt_subscribe(client, MQTT_COMMAND_TOPIC, 0, subscribe_done_cb, NULL);
    if(err != ERR_OK) {
        sys_untimeout(subscribe_commands, NULL);
        sys_timeout(MQTT_UTK_RETRY_MS, subscribe_commands, NULL);
    }
}

static void publish_done_cb(void *arg, err_t result)
{
	(void)arg;
	if(result != ERR_OK) {
		tm_printf((UB*)"mqtt: publish failed, err=%d\n", (int)result);
	}
}

static void heartbeat_timeout(void *arg)
{
	static uint32_t		count = 0;
	UB			payload[S1_HEARTBEAT_BYTES];
	int			len;

	(void)arg;
	if(!link_available()) return;

	if(mqtt_client_is_connected(client)) {
		len = s1_pack_heartbeat(count++, payload, sizeof(payload));
		tm_printf((UB*)"mqtt: TX [" MQTT_TOPIC "] %u binary bytes\n",
		          (unsigned int)len);
		(void)mqtt_publish(client, MQTT_TOPIC, payload, (u16_t)len,
				   0 /* qos */, 0 /* retain */,
				   publish_done_cb, NULL);
	}

	/* Re-arms itself every call -- mqtt.c's connection callback (below)
	 * only starts this chain once, on the transition into MQTT_CONNECT_
	 * ACCEPTED, so there is exactly one heartbeat chain alive at a time. */
	sys_timeout(MQTT_UTK_HEARTBEAT_MS, heartbeat_timeout, NULL);
}

static void retry_timeout(void *arg)
{
	(void)arg;
	if(!link_available()) return;
	/* Also serves as the pending TCP/MQTT connection deadline. Link status
	 * can remain UP during an AP outage; do not wait for TCP's long SYN/RTO
	 * backoff before another clean attempt. Explicit disconnect has no
	 * connection callback in this SDK. Never close an accepted session. */
	if(client != NULL && mqtt_client_is_connected(client)) {
		command_state &= (u8_t)~CMD_CONNECTING;
		return;
	}
	if(client != NULL) mqtt_disconnect(client);
	command_state &= (u8_t)~(CMD_PENDING | CMD_COLLECTING | CMD_SUBSCRIBED);
	command_state &= (u8_t)~CMD_CONNECTING;
	mqtt_utk_start();
}

static void connection_cb(mqtt_client_t *c, void *arg, mqtt_connection_status_t status)
{
	(void)c;
	(void)arg;

	if(status == MQTT_CONNECT_ACCEPTED) {
        if(!link_available()) { mqtt_disconnect(client); return; }
        sys_untimeout(retry_timeout, NULL);
		/* No longer *connecting* -- clear the guard so that if this
		 * connection later drops, the poll loop's mqtt_utk_start() call
		 * is allowed to reconnect. Leaving it set would wedge the client
		 * permanently after the first disconnect. */
		command_state &= (u8_t)~CMD_CONNECTING;

		tm_printf((UB*)"mqtt: connected to " MQTT_BROKER_IP "\n");

        command_state = 0;
        mqtt_set_inpub_callback(client, incoming_publish_cb, incoming_data_cb, NULL);
        sys_untimeout(subscribe_commands, NULL);
        subscribe_commands(NULL);

		/* Drop any chain left over from a previous connection before
		 * arming a new one. Each reconnect would otherwise add another
		 * self-perpetuating heartbeat, and every one of them holds a
		 * MEMP_SYS_TIMEOUT slot until the pool is exhausted. */
		sys_untimeout(heartbeat_timeout, NULL);
		sys_timeout(MQTT_UTK_HEARTBEAT_MS, heartbeat_timeout, NULL);
	} else {
        command_state &= (u8_t)~(CMD_PENDING | CMD_COLLECTING | CMD_SUBSCRIBED);
        sys_untimeout(subscribe_commands, NULL);
        /* Retry after a dropped or rejected connection -- on a delay, so a broker
		 * that keeps rejecting (e.g. bad client ID) can't turn this
		 * into a tight reconnect loop. */
		tm_printf((UB*)"mqtt: connection status %d, retrying in %ds\n",
			  (int)status, MQTT_UTK_RETRY_MS / 1000);
        retry_later();
	}
}

void mqtt_utk_start(void)
{
	struct mqtt_connect_client_info_t	client_info = {0};
	err_t					err;

	if(!link_available() || (command_state & CMD_CONNECTING) || (client != NULL && mqtt_client_is_connected(client))) {
		return;
	}

	if(client == NULL) {
		client = mqtt_client_new();
		if(client == NULL) {
			tm_printf((UB*)"mqtt: mqtt_client_new() failed\n");
            retry_later();
			return;
		}
	}

	if(!ip4addr_aton(MQTT_BROKER_IP, ip_2_ip4(&broker_addr))) {
		tm_printf((UB*)"mqtt: bad MQTT_BROKER_IP\n");
        retry_later();
		return;
	}
	IP_SET_TYPE(&broker_addr, IPADDR_TYPE_V4);

	client_info.client_id = MQTT_CLIENT_ID;
	client_info.keep_alive = MQTT_UTK_KEEPALIVE_S;

	command_state |= CMD_CONNECTING;
	err = mqtt_client_connect(client, &broker_addr, MQTT_BROKER_PORT,
				  connection_cb, NULL, &client_info);
	if(err != ERR_OK) {
		tm_printf((UB*)"mqtt: mqtt_client_connect() failed, err=%d\n", (int)err);
        retry_later();
	} else {
		/* One timer is reused for error backoff and an in-flight deadline.
		 * Success/failure/link-loss callbacks cancel or replace it. */
		sys_untimeout(retry_timeout, NULL);
		sys_timeout(MQTT_UTK_CONNECT_DEADLINE_MS, retry_timeout, NULL);
	}
}
