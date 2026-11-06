/* Temporary S5 -> S1 telemetry bridge; controller routing remains pending.
 * Only the built-in network task may operate the radio or MQTT client.
 */
#include "subsystem1.h"
#include "../subsystem3/subsystem3.h"
#if TM_MQTT
#include "mqtt_utk.h" /* shared plain-C bridge prototype, no lwIP calls here */
#endif

#define S1_QUEUE_DEPTH 4
#define S1_FRESH_MS 2000U
/* Kernel MBFs charge a length header plus word-rounded message storage.
 * Account for padding so all four packed records still fit. */
#define S1_MBF_STORAGE(bytes) (S1_QUEUE_DEPTH * (sizeof(INT) + \
    (((bytes) + sizeof(INT) - 1U) / sizeof(INT)) * sizeof(INT)))
typedef struct {
    S1_TELEMETRY sample;
    UB sampled_ms[4];
} S1_INPUT;
typedef struct {
    S1_TELEMETRY sample;
    UB sampled_ms[4];
    UB line_sampled_ms[4];
} S1_OUTPUT;
_Static_assert(sizeof(S1_INPUT) == 10, "input queue must not add padding");
_Static_assert(sizeof(S1_OUTPUT) == 14, "output queue must not add padding");
static ID telemetry_mbf;
static ID network_tx_mbf;

static UW uptime_ms(void)
{
    SYSTIM now;
    tk_get_tim(&now);
    return now.lo;
}

static ER validate_sample(const S1_TELEMETRY *sample)
{
    return s1_validate_telemetry(sample) == S1_CODEC_OK ? E_OK : E_PAR;
}

EXPORT INT s1_encode_telemetry(const S1_TELEMETRY *sample,
                              const S1_LINE_TELEMETRY *line, UW now_ms,
                              UB *out, UINT capacity)
{
    INT len = s1_pack_telemetry(sample, line, now_ms, APP_IR_MAX_AGE_MS,
                               out, capacity);
    return len < 0 ? E_PAR : len;
}

EXPORT ER s1_submit_telemetry(const S1_TELEMETRY *sample)
{
    S1_INPUT msg = {0};
    ER er = validate_sample(sample);
    if (er != E_OK) return er;
    if (telemetry_mbf <= 0) return E_OBJ;
    msg.sample = *sample;
    s1_write_u32(msg.sampled_ms, uptime_ms());
    return tk_snd_mbf(telemetry_mbf, &msg, sizeof(msg), TMO_POL);
}

EXPORT INT s1_network_poll(unsigned char *out, unsigned int capacity)
{
    S1_OUTPUT msg, latest;
    S1_LINE_TELEMETRY line = {{0}};
    INT count, size;
    BOOL have_latest = FALSE;
    if (out == NULL || capacity < S1_PAYLOAD_CAPACITY) return E_PAR;
    if (network_tx_mbf <= 0) return E_OBJ;
    /* Bounded work in the radio owner; discard old samples, keep newest. */
    for (count = 0; count < S1_QUEUE_DEPTH; ++count) {
        size = tk_rcv_mbf(network_tx_mbf, &msg, TMO_POL);
        if (size == E_TMOUT) break;
        if (size < 0) return size;
        if (size != (INT)sizeof(msg)) continue;
        if ((UW)(uptime_ms() - s1_read_u32(msg.sampled_ms)) > S1_FRESH_MS) continue;
        latest = msg;
        have_latest = TRUE;
    }
    if (!have_latest) return E_TMOUT;
    s1_write_u32(line.bytes, s1_read_u32(latest.line_sampled_ms));
    line.bytes[4] = (UB)s1_line_bits(&latest.sample);
    return s1_encode_telemetry(&latest.sample, &line, uptime_ms(), out, capacity);
}

S1_COMMAND s1_parse_command(const unsigned char *payload, unsigned int length)
{
    if (payload == 0) {
        return S1_COMMAND_INVALID;
    }
    if (length == 4 && payload[0] == 'S' && payload[1] == 'T' &&
        payload[2] == 'O' && payload[3] == 'P') {
        return S1_COMMAND_STOP;
    }
    if (length == 6 && payload[0] == 'S' && payload[1] == 'T' &&
        payload[2] == 'A' && payload[3] == 'T' &&
        payload[4] == 'U' && payload[5] == 'S') {
        return S1_COMMAND_STATUS;
    }
    return S1_COMMAND_INVALID;
}

#if TM_WIFI_CYW43
#include "cyw43_utk.h"
#endif

static ID wifi_status_tid;

static void wifi_status_task(INT stacd, void *exinf)
{
    UW last_status_ms = uptime_ms();
    (void)stacd;
    (void)exinf;

    for (;;) {
        S1_INPUT input;
        INT size = tk_rcv_mbf(telemetry_mbf, &input, 100);
        if (size == (INT)sizeof(input) &&
            (UW)(uptime_ms() - s1_read_u32(input.sampled_ms)) <= S1_FRESH_MS) {
            S1_OUTPUT output = {0};
            S3_LINE_DATA line = {0};
            S1_LINE_TELEMETRY packed_line = {{0}};
            s1_write_u32(output.sampled_ms, s1_read_u32(input.sampled_ms));
            /* S3 retains GPIO ownership. Its snapshot read never waits. */
            if (subsystem3_get_line(&line) == E_OK && line.valid &&
                line.pattern <= 3U &&
                (UW)(uptime_ms() - line.timestamp_ms) <= APP_IR_MAX_AGE_MS) {
                (void)s1_make_line(&packed_line, line.pattern, 1,
                                   line.timestamp_ms);
            }
            if (s1_encode_telemetry(&input.sample, &packed_line, uptime_ms(),
                                    output.sample.bytes, sizeof(output.sample.bytes)) < 0)
                continue;
            s1_write_u32(output.line_sampled_ms, s1_read_u32(packed_line.bytes));
            /* Keep raw snapshots until the network poll so a queued IR
             * reading can expire without losing a fresh distance reading. */
            (void)tk_snd_mbf(network_tx_mbf, &output, sizeof(output), TMO_POL);
        } else if (size < 0 && size != E_TMOUT) {
            CHECK(size, "S1 telemetry receive");
            tk_dly_tsk(100);
        }
        if ((UW)(uptime_ms() - last_status_ms) < 2000U) continue;
        last_status_ms = uptime_ms();
#if TM_WIFI_CYW43 && S1_WIFI_LOG_ENABLED
        T_CYW43_UTK_STATUS st = {0};

        cyw43_utk_get_status(&st);

        tm_printf(
            (UB *)"[WIFI] init_done=%d init_result=%d ready=%d "
                   "link=%d polls=%u\n",
            (int)st.init_complete,
            (int)st.init_result,
            (int)st.ready,
            (int)st.link_status,
            (unsigned int)st.poll_count
        );

        /* ready means radio initialized, not MQTT connected.
         * ip_addr may retain an old address after a disconnect. */
        if (st.ip_addr != 0) {
            unsigned int ip = (unsigned int)st.ip_addr;

            tm_printf(
                (UB *)"[WIFI] last reported IP=%u.%u.%u.%u\n",
                ip & 0xffu,
                (ip >> 8) & 0xffu,
                (ip >> 16) & 0xffu,
                (ip >> 24) & 0xffu
            );
        }

#endif
    }
}

EXPORT ER subsystem1_init(void)
{
    T_CTSK task = {0};
    T_CMBF mbf = {0};

    mbf.mbfatr = TA_TFIFO;
    mbf.maxmsz = sizeof(S1_INPUT);
    mbf.bufsz = S1_MBF_STORAGE(sizeof(S1_INPUT));
    telemetry_mbf = tk_cre_mbf(&mbf);
    if (telemetry_mbf < E_OK) return telemetry_mbf;
    mbf.maxmsz = sizeof(S1_OUTPUT);
    mbf.bufsz = S1_MBF_STORAGE(sizeof(S1_OUTPUT));
    network_tx_mbf = tk_cre_mbf(&mbf);
    if (network_tx_mbf < E_OK) {
        tk_del_mbf(telemetry_mbf);
        telemetry_mbf = 0;
        return network_tx_mbf;
    }

    task.tskatr  = TA_HLNG | TA_RNG0;
    task.task    = (FP)wifi_status_task;
    task.itskpri = S1_TASK_PRIORITY;
    task.stksz   = TASK_STACK_SIZE;

    wifi_status_tid = tk_cre_tsk(&task);

    if (wifi_status_tid < E_OK) {
        tk_del_mbf(network_tx_mbf);
        tk_del_mbf(telemetry_mbf);
        network_tx_mbf = telemetry_mbf = 0;
        return (ER)wifi_status_tid;
    }

    return E_OK;
}

EXPORT ER subsystem1_start(void)
{
    if (wifi_status_tid <= 0) {
        return E_OBJ;
    }
    return tk_sta_tsk(wifi_status_tid, 0);
}
