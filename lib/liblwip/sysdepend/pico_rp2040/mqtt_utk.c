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
/* SDK 2.2.0 lwIP client state: this port owns its TCP/request queue. Use the
 * existing queue for backpressure instead of introducing another packet queue.
 * Keep this SDK-specific access here, out of S1 and sensor/controller code. */
#include "lwip/apps/mqtt_priv.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include <string.h>
#include "wifi_secrets.h"
#include "mqtt_utk.h"
#include <tm/tmonitor.h>

#define MQTT_UTK_HEARTBEAT_MS   4000
#define MQTT_UTK_KEEPALIVE_S    5
#define MQTT_UTK_RETRY_MS       3000
#define MQTT_UTK_CONNECT_DEADLINE_MS 5000
#define MQTT_UTK_TX_RECHECK_MS   100
#ifndef MQTT_TELEMETRY_TOPIC
#define MQTT_TELEMETRY_TOPIC MQTT_CLIENT_ID "/telemetry"
#endif

#ifndef MQTT_COMMAND_TOPIC
#define MQTT_COMMAND_TOPIC MQTT_CLIENT_ID "/command"
#endif
#ifndef MQTT_STATUS_TOPIC
#define MQTT_STATUS_TOPIC MQTT_CLIENT_ID "/status"
#endif
/* One bounded union slot. Low four bits hold remaining bytes 0..13,
 * or 15 while WiFi is unavailable. Four flags fill the other half byte. */
#define CMD_LENGTH 15U
#define CMD_LINK_DOWN 15U
#define CMD_COLLECTING 16U
#define CMD_PENDING 32U
#define CMD_SUBSCRIBED 64U
#define CMD_CONNECTING 128U
#define STATUS_MIN_INTERVAL_MS 250U
static u32_t network_counters[3]; /* publish errors, rejected requests, connections */
static mqtt_client_t *client;
static ip_addr_t broker_addr;
static u8_t command_state = CMD_LINK_DOWN;
static u8_t command_bytes[S1_COMMAND_MAX_BYTES];
static u32_t last_status_ms,session_nonce,last_stop_id;
static u32_t last_arm_ms,last_resume_ms,last_resume_id;

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
    app_controller_cancel_resume();
    if(!link_available()) return;
    command_state = CMD_LINK_DOWN;
    sys_untimeout(retry_timeout, NULL);
    sys_untimeout(subscribe_commands, NULL);
    sys_untimeout(heartbeat_timeout, NULL);
    if(client != NULL) mqtt_disconnect(client);
}

static unsigned command_length(unsigned marker)
{
    return marker==0xa0U?S1_STATUS_REQUEST_BYTES:
        (marker==0xa2U||marker==0xa5U||marker==0xa7U)?S1_STOP_REQUEST_BYTES:0U;
}
static int parse_control_request(const UB *b,unsigned n,uint32_t *id,uint32_t *token,uint32_t *deadline)
{
    return b[0]==0xa2U?s1_parse_stop_request(b,n,id,token,deadline):
        s1_parse_resume_request(b,n,b[0],id,token,deadline);
}
static err_t publish_packet(mqtt_client_t *c,const char *topic,const void *payload,
                            u16_t length,u8_t qos,u8_t retain,mqtt_request_cb_t cb,void *arg)
{
    err_t er=mqtt_publish(c,topic,payload,length,qos,retain,cb,arg);
    if(er!=ERR_OK)++network_counters[0];
    return er;
}

static int periodic_tx_ready(void)
{
    const struct mqtt_request_t *request;
    unsigned pending=0;
    /* lwIP allocates a request even for QoS 0 until TCP acknowledges it.
     * Do not fill all four slots with old telemetry during a radio stall.
     * Allow two in flight so a peer using delayed ACK can receive its next
     * segment; stop-and-wait unnecessarily stalls the normal 400 ms stream.
     * Two remaining SDK slots are reserved for command/subscription work. */
    if(client == NULL)return 0;
    for(request=client->pend_req_queue;request!=NULL;request=request->next)
        if(++pending>=2U)return 0;
    return 1;
}

static void stop_result(uint32_t id,unsigned result)
{
    UB bytes[S1_STOP_REPLY_BYTES];
    if(result!=APP_STOP_LATCHED)++network_counters[1];
    int len=s1_pack_stop_reply(id,session_nonce,result,s1_network_controller_word(),bytes,sizeof(bytes));
    if(len>0)(void)publish_packet(client,MQTT_STATUS_TOPIC,bytes,(u16_t)len,0,0,publish_done_cb,NULL);
}
static void resume_result(uint32_t id,unsigned result)
{
    UB bytes[S1_STOP_REPLY_BYTES];int n;
    if(result)++network_counters[1];
    n=s1_pack_resume_reply(id,session_nonce,result,s1_network_controller_word(),bytes,sizeof(bytes));
    if(n>0)(void)publish_packet(client,MQTT_STATUS_TOPIC,bytes,(u16_t)n,0,0,publish_done_cb,NULL);
}
void mqtt_utk_poll(void)
{
    UB payload[S1_TX_CAPACITY];
    UB completed[10]={0};
    int reply_len=s1_network_command_reply(completed);
    int len;
    err_t err;
    /* No offline queue to replay. Snapshot/clock reads are nonblocking;
     * the network owner never calls a sensor driver. */
    if(!link_available() || client == NULL || !mqtt_client_is_connected(client)) {
        command_state &= (u8_t)~(CMD_COLLECTING | CMD_PENDING | CMD_SUBSCRIBED);
        return;
    }
    if(command_state & CMD_PENDING) {
        command_state &= (u8_t)~CMD_PENDING;
        if(command_bytes[0]==0xa0U) {
            UB reply[S1_STATUS_MAX_BYTES];
            int size=s1_network_status(command_bytes,S1_STATUS_REQUEST_BYTES,reply,sizeof(reply),session_nonce);
            if(size>0)(void)publish_packet(client,MQTT_STATUS_TOPIC,reply,(u16_t)size,0,0,publish_done_cb,NULL);
        } else if(command_bytes[0]==0xa5U) {
            int size=s1_network_arm_resume(command_bytes,13,payload,sizeof(payload),session_nonce);
            if(size>0) {
                if(payload[0]==0xa8U)++network_counters[1];
                (void)publish_packet(client,MQTT_STATUS_TOPIC,payload,(u16_t)size,0,0,publish_done_cb,NULL);
            }
        } else if(command_bytes[0]==0xa7U) {
            uint32_t id,token,deadline,now;
            if(s1_parse_resume_request(command_bytes,13,0xa7,&id,&token,&deadline)==0) {
                if(token!=session_nonce)resume_result(id,APP_RESUME_SESSION);
                else if(id==last_resume_id)resume_result(id,APP_RESUME_REPLAY);
                else if(s1_network_uptime(&now)!=0||(uint32_t)(deadline-now)==0U||
                        (uint32_t)(deadline-now)>APP_CTRL_STOP_LEASE_MS)resume_result(id,APP_RESUME_EXPIRED);
                else {
                    int result=s1_network_submit_resume(id,token,deadline);
                    if(!result)last_resume_id=id;
                    else resume_result(id,result<0?APP_RESUME_FAULT:(unsigned)result);
                }
            }
        } else {
            uint32_t id,token,deadline,now;
            if(s1_parse_stop_request(command_bytes,S1_STOP_REQUEST_BYTES,&id,&token,&deadline)==S1_CODEC_OK) {
                unsigned word=s1_network_controller_word();
                if(token!=session_nonce)stop_result(id,APP_STOP_SESSION);
                else if(id==last_stop_id)stop_result(id,APP_STOP_REPLAY);
                else if(s1_network_uptime(&now)!=0||(uint32_t)(deadline-now)==0U||
                        (uint32_t)(deadline-now)>APP_CTRL_STOP_LEASE_MS)stop_result(id,APP_STOP_EXPIRED);
                else if(!(word&APP_CTRL_STATE_MASK))stop_result(id,APP_STOP_FAULT);
                else if((word&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT)stop_result(id,APP_STOP_FAULT);
                else {
                    int result=s1_network_submit_stop(id,token,deadline);
                    if(result==0)last_stop_id=id; /* Controller sends the final result later. */
                    else stop_result(id,result==-50?APP_STOP_BUSY:APP_STOP_FAULT);
                }
            }
        }
    }
    if(reply_len==10&&s1_read_u32(completed+4)==session_nonce) {
        UB reply[S1_STOP_REPLY_BYTES];
        unsigned word=completed[8]|((unsigned)completed[9]<<8);
        uint32_t id=s1_read_u32(completed);
        int size=-1;
        if((word&APP_REPLY_RESUME)&&id==last_resume_id)
            size=s1_pack_resume_reply(id,session_nonce,(word>>6)&7U,word&APP_CTRL_WORD_MASK,reply,sizeof(reply));
        else if(!(word&APP_REPLY_RESUME)&&id==last_stop_id)
            size=s1_pack_stop_reply(id,session_nonce,(word>>6)&7U,word&APP_CTRL_WORD_MASK,reply,sizeof(reply));
        if(size>0)(void)publish_packet(client,MQTT_STATUS_TOPIC,reply,(u16_t)size,0,0,publish_done_cb,NULL);
    }
    /* Wait without copying/generating a sensor frame. On recovery the next
     * poll serializes the latest publication, rather than queued old samples. */
    if(!periodic_tx_ready()) return;
    len = s1_network_poll(payload, sizeof(payload), session_nonce);
    if(len > 0) {
        err = publish_packet(client, MQTT_TELEMETRY_TOPIC, payload, (u16_t)len,0,0,publish_done_cb,NULL);
        if(err != ERR_OK)tm_printf((UB*)"mqtt: telemetry dropped, err=%d\n",(int)err);
    }
    if(len<=0) {
        int size=s1_network_aux(payload,sizeof(payload),session_nonce,network_counters);
        if(size>0) {
            err=publish_packet(client,MQTT_TELEMETRY_TOPIC,payload,(u16_t)size,0,0,publish_done_cb,NULL);
            s1_network_aux_result(payload[0],err==ERR_OK);
        }
    }
}

static void incoming_publish_cb(void *arg, const char *topic, u32_t total_len)
{
    (void)arg;
    if(!link_available()) return;
    unsigned stop_only=0;
    if(command_state&CMD_PENDING) {
        if(strcmp(topic,MQTT_COMMAND_TOPIC)==0&&total_len==13U&&
           (command_bytes[0]==0xa5U||command_bytes[0]==0xa7U)) {
            command_state&=(u8_t)~CMD_PENDING;stop_only=1;
        } else {if(strcmp(topic,MQTT_COMMAND_TOPIC)==0)++network_counters[1];return;}
    }
    command_state &= (u8_t)~(CMD_LENGTH | CMD_COLLECTING);
    if((command_state & CMD_SUBSCRIBED) && !(command_state & CMD_PENDING) &&
        (total_len==S1_STATUS_REQUEST_BYTES||total_len==S1_STOP_REQUEST_BYTES)&&strcmp(topic,MQTT_COMMAND_TOPIC)==0) {
        command_bytes[0]=(u8_t)stop_only; /* 1 means the replacement must be STOP. */
        command_state|=(u8_t)(CMD_COLLECTING|total_len);
    } else if(strcmp(topic,MQTT_COMMAND_TOPIC)==0)++network_counters[1];
}
static void incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    unsigned remaining=command_state&CMD_LENGTH,total,i;
    uint32_t id,token,deadline;
    u32_t now;
    (void)arg;
    if(!link_available() || !(command_state & CMD_COLLECTING)) return;
    if(command_bytes[0]<=1U&&len) {
        if((command_bytes[0]==1U&&data[0]!=0xa2U)||command_length(data[0])!=remaining){command_state&=(u8_t)~CMD_COLLECTING;++network_counters[1];return;}
        command_bytes[0]=data[0];
    }
    total=command_length(command_bytes[0]);
    if(!total||remaining>total||len>remaining) {
        command_state &= (u8_t)~CMD_COLLECTING;
        ++network_counters[1];return;
    }
    for(i=0;i<len;++i)command_bytes[total-remaining+i]=data[i];
    remaining-=len;
    command_state=(u8_t)((command_state&(u8_t)~CMD_LENGTH)|remaining);
    if(!(flags & MQTT_DATA_FLAG_LAST)) return;
    command_state &= (u8_t)~CMD_COLLECTING;
    if(remaining){++network_counters[1];return;}
    now = sys_now();
    if(total==S1_STATUS_REQUEST_BYTES) {
        if(s1_parse_status_request(command_bytes,total,&id)!=S1_CODEC_OK||
           (u32_t)(now-last_status_ms)<STATUS_MIN_INTERVAL_MS){++network_counters[1];return;}
        last_status_ms=now;
    } else {
        u32_t *last=command_bytes[0]==0xa5U?&last_arm_ms:&last_resume_ms;
        if(parse_control_request(command_bytes,total,&id,&token,&deadline)!=S1_CODEC_OK||
           (command_bytes[0]!=0xa2U&&(u32_t)(now-*last)<STATUS_MIN_INTERVAL_MS)){++network_counters[1];return;}
        /* STOP must not be throttled behind a quick stop/resume/stop sequence. */
        if(command_bytes[0]!=0xa2U)*last=now;
    }
    command_state|=CMD_PENDING;
}
static void subscribe_done_cb(void *arg, err_t result)
{
    (void)arg;
    if(!link_available()) return;
    if(result == ERR_OK) {
        command_state |= CMD_SUBSCRIBED;
        tm_printf((UB*)"mqtt: subscribed to " MQTT_COMMAND_TOPIC " (STATUS / STOP / Resume)\n");
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
        ++network_counters[0];
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
		if(!periodic_tx_ready()) {
            sys_timeout(MQTT_UTK_TX_RECHECK_MS, heartbeat_timeout, NULL);
            return;
        }
		len = s1_pack_heartbeat(count, payload, sizeof(payload));
		if(publish_packet(client, MQTT_TOPIC, payload, (u16_t)len,
				   0 /* qos */, 0 /* retain */,
				   publish_done_cb, NULL) != ERR_OK) {
            sys_timeout(MQTT_UTK_TX_RECHECK_MS, heartbeat_timeout, NULL);
            return;
        }
        ++count;
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
        /* Tiny MQTT measurements/replies must not wait for Nagle coalescing.
         * The owner task is the only caller into this non-TLS TCP connection. */
        if(c->conn != NULL) altcp_nagle_disable(c->conn);
        sys_untimeout(retry_timeout, NULL);
		/* No longer *connecting* -- clear the guard so that if this
		 * connection later drops, the poll loop's mqtt_utk_start() call
		 * is allowed to reconnect. Leaving it set would wedge the client
		 * permanently after the first disconnect. */
		command_state &= (u8_t)~CMD_CONNECTING;

		tm_printf((UB*)"mqtt: connected to " MQTT_BROKER_IP "\n");

        command_state = 0;
        {
            u32_t next=mqtt_utk_session_entropy();
            if(!next||next==session_nonce){next=session_nonce+1U;if(!next)next=1U;}
            session_nonce=next;last_stop_id=last_resume_id=0;
            app_controller_cancel_resume();
            last_status_ms=sys_now()-STATUS_MIN_INTERVAL_MS;
            last_arm_ms=last_resume_ms=last_status_ms;
            ++network_counters[2];
        }
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
