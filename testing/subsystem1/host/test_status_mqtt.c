/* Actual mqtt_utk.c + codec, mocked SDK transport/timers. No sockets, drivers
 * or live publications. Exercises fragmentation, one-slot/rate limits,
 * subscription/reconnect failures and strict read-only dispatch. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
typedef uint8_t UB;
#include "../../../app_program/subsystem1/s1_protocol.c"
static mqtt_client_t fake_client;
static u32_t clock_ms=1000;
static int publications,status_calls,subscriptions,failures,checks,telemetry_available;
static err_t subscribe_result,connect_result;
static int connect_calls,disconnect_calls,allocation_fails,ip_valid=1;
static unsigned requested_keepalive;
typedef struct { sys_timeout_handler fn; void *arg; u32_t delay; } TestTimer;
static TestTimer timers[8];
static char last_topic[64],subscribed_topic[64];
static unsigned char last_payload[32];
static unsigned last_length;
static mqtt_request_cb_t subscription_callback;
static mqtt_client_t *mqtt_client_new(void) { return allocation_fails?NULL:&fake_client; }
static int mqtt_client_is_connected(mqtt_client_t *c) { return c->connected; }
static void mqtt_disconnect(mqtt_client_t *c) { c->connected=0;++disconnect_calls; }
static int ip4addr_aton(const char *text,ip_addr_t *addr) { (void)text;addr->addr=1;return ip_valid; }
static u32_t sys_now(void) { return clock_ms; }
static void sys_timeout(u32_t delay,sys_timeout_handler fn,void *arg) {
    unsigned i;
    for(i=0;i<8;++i)if(!timers[i].fn){timers[i]=(TestTimer){fn,arg,delay};return;}
    abort();
}
static void sys_untimeout(sys_timeout_handler fn,void *arg) {
    unsigned i;for(i=0;i<8;++i)if(timers[i].fn==fn&&timers[i].arg==arg)timers[i].fn=NULL;
}
static unsigned timer_count(sys_timeout_handler fn) {
    unsigned i,n=0;for(i=0;i<8;++i)if(timers[i].fn==fn)++n;return n;
}
static void fire_timer(sys_timeout_handler fn) {
    unsigned i;
    for(i=0;i<8;++i)if(timers[i].fn==fn){
        TestTimer t=timers[i];timers[i].fn=NULL;clock_ms+=t.delay;t.fn(t.arg);return;
    }
    abort();
}
static int tm_printf(UB *format,...) { (void)format;return 0; }
static err_t mqtt_publish(mqtt_client_t *c,const char *topic,const void *payload,u16_t length,
                          u8_t qos,u8_t retain,mqtt_request_cb_t fn,void *arg) {
    (void)c;(void)fn;(void)arg;
    if(qos||retain||length>sizeof(last_payload))abort();
    ++publications;snprintf(last_topic,sizeof(last_topic),"%s",topic);
    memcpy(last_payload,payload,length);last_length=length;return ERR_OK;
}
static err_t mqtt_subscribe(mqtt_client_t *c,const char *topic,u8_t qos,mqtt_request_cb_t fn,void *arg) {
    (void)c;(void)arg;if(qos)abort();++subscriptions;subscription_callback=fn;
    snprintf(subscribed_topic,sizeof(subscribed_topic),"%s",topic);return subscribe_result;
}
static void mqtt_set_inpub_callback(mqtt_client_t *c,mqtt_incoming_publish_cb_t pub,
                                    mqtt_incoming_data_cb_t data,void *arg) {
    (void)c;(void)pub;(void)data;(void)arg;
}
static err_t mqtt_client_connect(mqtt_client_t *c,ip_addr_t *addr,u16_t port,
                                  mqtt_connection_cb_t fn,void *arg,
                                  const struct mqtt_connect_client_info_t *info) {
    (void)c;(void)addr;(void)port;(void)fn;(void)arg;
    requested_keepalive=info->keep_alive;++connect_calls;return connect_result;
}
int s1_network_poll(unsigned char *out,unsigned capacity) {
    S1_TELEMETRY sample;
    if(!telemetry_available)return -50;
    telemetry_available=0;(void)s1_make_telemetry(&sample,42,451,1);
    return s1_pack_frame(&sample,NULL,NULL,clock_ms,100,out,capacity);
}
int s1_network_status(const unsigned char *request,unsigned length,unsigned char *out,unsigned capacity) {
    uint32_t id;
    if(s1_parse_status_request(request,length,&id))return -17;
    ++status_calls;return s1_pack_status_reply(id,clock_ms,S1_STATUS_NO_SAMPLE,1,NULL,out,capacity);
}
#include "../../../lib/liblwip/sysdepend/pico_rp2040/mqtt_utk.c"
static const UB request_a[5]={0xa0,12,0,0,0},request_b[5]={0xa0,13,0,0,0};
static void expect(int condition,const char *message) {
    ++checks;printf("[MQTT STATUS] %s %s\n",condition?"PASS":"FAIL",message);if(!condition)++failures;
}
static void reset(int acknowledge) {
    memset(timers,0,sizeof(timers));
    memset(&fake_client,0,sizeof(fake_client));fake_client.connected=1;
    client=&fake_client;command_state=0;last_status_ms=0;clock_ms=1000;
    publications=status_calls=subscriptions=telemetry_available=0;subscribe_result=ERR_OK;
    connect_calls=disconnect_calls=allocation_fails=0;connect_result=ERR_OK;ip_valid=1;requested_keepalive=0;
    connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);
    if(acknowledge)subscription_callback(NULL,ERR_OK);
}
static void deliver(const char *topic,const UB *bytes,unsigned declared,unsigned count) {
    incoming_publish_cb(NULL,topic,declared);incoming_data_cb(NULL,bytes,(u16_t)count,MQTT_DATA_FLAG_LAST);
}
int main(void) {
    reset(1);
    expect(subscriptions==1 && !strcmp(subscribed_topic,MQTT_COMMAND_TOPIC),
        "firmware subscribes to real command topic, replacing heartbeat loopback");
    incoming_publish_cb(NULL,MQTT_COMMAND_TOPIC,5);
    incoming_data_cb(NULL,request_a,2,0);
    expect(!(command_state&CMD_PENDING),"partial command cannot trigger a reply");
    incoming_data_cb(NULL,request_a+2,3,MQTT_DATA_FLAG_LAST);mqtt_utk_poll();
    expect(status_calls==1 && last_length==11 && !strcmp(last_topic,MQTT_STATUS_TOPIC) &&
        s1_read_u32(last_payload+1)==12,"fragmented STATUS completes once and echoes exact correlation ID");
    mqtt_utk_poll();expect(status_calls==1,"completed request is consumed exactly once");
    reset(1);deliver(MQTT_TOPIC,request_a,5,5);mqtt_utk_poll();
    expect(status_calls==0,"heartbeat and unrelated topics cannot dispatch commands");
    reset(1);deliver(MQTT_COMMAND_TOPIC,(const UB *)"STATUS",6,6);mqtt_utk_poll();
    expect(status_calls==0,"ASCII and oversized declared requests are ignored");
    reset(1);deliver(MQTT_COMMAND_TOPIC,request_a,5,2);mqtt_utk_poll();
    expect(status_calls==0,"early LAST fragment cannot execute a partial request");
    reset(1);incoming_publish_cb(NULL,MQTT_COMMAND_TOPIC,5);
    incoming_data_cb(NULL,request_a,5,0);incoming_data_cb(NULL,request_a,1,MQTT_DATA_FLAG_LAST);mqtt_utk_poll();
    expect(status_calls==0,"extra fragment bytes invalidate the whole request");
    reset(1);deliver(MQTT_COMMAND_TOPIC,request_a,5,5);deliver(MQTT_COMMAND_TOPIC,request_b,5,5);mqtt_utk_poll();
    expect(status_calls==1 && s1_read_u32(last_payload+1)==12,"single occupied slot drops new requests without overwriting pending ID");
    clock_ms=1249;deliver(MQTT_COMMAND_TOPIC,request_b,5,5);mqtt_utk_poll();
    expect(status_calls==1,"firmware limits accepted STATUS requests to one per 250 ms");
    clock_ms=1250;deliver(MQTT_COMMAND_TOPIC,request_b,5,5);mqtt_utk_poll();
    expect(status_calls==2 && s1_read_u32(last_payload+1)==13,"rate limit accepts exact boundary with fresh ID");
    reset(1);clock_ms=0xfffffff0U;deliver(MQTT_COMMAND_TOPIC,request_a,5,5);mqtt_utk_poll();
    clock_ms=16;deliver(MQTT_COMMAND_TOPIC,request_b,5,5);mqtt_utk_poll();
    expect(status_calls==1,"rate limiting remains correct over uint32 uptime wrap");
    reset(1);deliver(MQTT_COMMAND_TOPIC,request_a,5,5);fake_client.connected=0;mqtt_utk_poll();
    fake_client.connected=1;connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);subscription_callback(NULL,ERR_OK);mqtt_utk_poll();
    expect(status_calls==0,"disconnect cancels pending requests instead of replaying after reconnect");
    reset(0);deliver(MQTT_COMMAND_TOPIC,request_a,5,5);mqtt_utk_poll();
    expect(status_calls==0,"command handling waits for its subscription acknowledgment");
    subscription_callback(NULL,ERR_MEM);fire_timer(subscribe_commands);subscription_callback(NULL,ERR_OK);
    deliver(MQTT_COMMAND_TOPIC,request_a,5,5);mqtt_utk_poll();
    expect(subscriptions==2 && status_calls==1,"failed command subscription can retry and recover");
    reset(1);telemetry_available=1;mqtt_utk_poll();
    expect(last_length==11 && !strcmp(last_topic,MQTT_TELEMETRY_TOPIC),"normal telemetry is still published independently");
    heartbeat_timeout(NULL);
    expect(last_length==5 && last_payload[0]==0x80 && !strcmp(last_topic,MQTT_TOPIC),"five-byte heartbeat is unchanged");

    reset(1);deliver(MQTT_COMMAND_TOPIC,request_a,5,5);
    sys_timeout(3000,retry_timeout,NULL);sys_timeout(3000,subscribe_commands,NULL);
    mqtt_utk_set_link(0);mqtt_utk_set_link(0);
    expect(disconnect_calls==1 && command_state==CMD_LINK_DOWN &&
        !timer_count(retry_timeout)&&!timer_count(subscribe_commands)&&!timer_count(heartbeat_timeout),
        "WiFi loss cancels pending work and all port timers, closes TCP once");
    telemetry_available=1;mqtt_utk_start();mqtt_utk_poll();heartbeat_timeout(NULL);
    subscribe_done_cb(NULL,ERR_OK);subscribe_commands(NULL);retry_timeout(NULL);
    deliver(MQTT_COMMAND_TOPIC,request_a,5,5);
    expect(connect_calls==0 && publications==0 && status_calls==0 && command_state==CMD_LINK_DOWN &&
        !timer_count(heartbeat_timeout),"offline polling and late callbacks cannot publish, reconnect or rearm heartbeat");
    fake_client.connected=1;connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);
    expect(disconnect_calls==2 && !fake_client.connected,"late MQTT acceptance while WiFi is down closes the stale session");
    mqtt_utk_set_link(1);mqtt_utk_start();
    fake_client.connected=1;connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);subscription_callback(NULL,ERR_OK);
    mqtt_utk_poll();
    expect(connect_calls==1 && subscriptions==2 && status_calls==0 && timer_count(heartbeat_timeout)==1,
        "restored WiFi creates a clean command subscription without replaying old requests");
    deliver(MQTT_COMMAND_TOPIC,request_b,5,5);mqtt_utk_poll();
    expect(status_calls==1 && s1_read_u32(last_payload+1)==13,"fresh STATUS works after WiFi recovery");

    reset(1);mqtt_utk_set_link(0);mqtt_utk_set_link(1);connect_result=ERR_MEM;
    for(unsigned i=0;i<1000;++i)mqtt_utk_start();
    expect(connect_calls==1 && timer_count(retry_timeout)==1,"synchronous connect error backs off instead of retrying every poll");
    fire_timer(retry_timeout);
    expect(connect_calls==2 && timer_count(retry_timeout)==1,"failed timer retry schedules exactly one further retry");
    connection_cb(client,NULL,1);connection_cb(client,NULL,1);
    expect(timer_count(retry_timeout)==1,"repeated failure callbacks do not accumulate retry timers");
    connect_result=ERR_OK;fire_timer(retry_timeout);
    fake_client.connected=1;connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);subscription_callback(NULL,ERR_OK);
    expect(!timer_count(retry_timeout) && timer_count(heartbeat_timeout)==1,
        "successful recovery removes stale retry and arms one heartbeat");
    for(unsigned i=0;i<20;++i){
        fake_client.connected=0;connection_cb(client,NULL,1);fire_timer(retry_timeout);
        fake_client.connected=1;connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);subscription_callback(NULL,ERR_OK);
    }
    expect(timer_count(heartbeat_timeout)==1 && !timer_count(retry_timeout),
        "twenty MQTT reconnects retain one heartbeat chain and no stale retry");

    reset(1);mqtt_utk_set_link(0);mqtt_utk_set_link(1);client=NULL;allocation_fails=1;
    for(unsigned i=0;i<100;++i)mqtt_utk_start();
    expect(timer_count(retry_timeout)==1 && connect_calls==0,"client allocation failure also uses bounded backoff");
    allocation_fails=0;fire_timer(retry_timeout);
    expect(client==&fake_client && connect_calls==1,"allocation retry can resume normal connection setup");
    reset(1);mqtt_utk_set_link(0);mqtt_utk_set_link(1);ip_valid=0;
    for(unsigned i=0;i<100;++i)mqtt_utk_start();
    expect(timer_count(retry_timeout)==1 && connect_calls==0,"invalid broker address does not hammer the poll loop");

    reset(1);mqtt_utk_set_link(0);mqtt_utk_set_link(1);mqtt_utk_start();
    expect(connect_calls==1 && requested_keepalive==5 && timer_count(retry_timeout)==1,
        "connection advertises five-second MQTT keepalive and arms one connection deadline");
    int disconnects_before=disconnect_calls;
    u32_t before=clock_ms;
    fire_timer(retry_timeout);
    expect(clock_ms-before==5000 && disconnect_calls==disconnects_before+1 && connect_calls==2 &&
        timer_count(retry_timeout)==1,"stalled TCP/MQTT connect is closed and replaced after five seconds");
    for(unsigned i=0;i<1000;++i)mqtt_utk_start();
    expect(connect_calls==2,"network polls cannot bypass an in-flight connection deadline");
    fake_client.connected=1;connection_cb(client,NULL,MQTT_CONNECT_ACCEPTED);subscription_callback(NULL,ERR_OK);
    expect(!timer_count(retry_timeout)&&timer_count(heartbeat_timeout)==1,
        "successful CONNACK cancels the pending connection deadline");
    disconnects_before=disconnect_calls;retry_timeout(NULL);
    expect(disconnect_calls==disconnects_before && fake_client.connected && timer_count(heartbeat_timeout)==1,
        "a late deadline callback never closes a healthy accepted session");
    reset(1);mqtt_utk_set_link(0);mqtt_utk_set_link(1);mqtt_utk_start();mqtt_utk_set_link(0);
    int connections_before=connect_calls;retry_timeout(NULL);
    expect(!timer_count(retry_timeout)&&connect_calls==connections_before&&command_state==CMD_LINK_DOWN,
        "WiFi loss cancels an in-flight deadline and its stale callback cannot reconnect");
    printf("[MQTT STATUS] SUMMARY %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
