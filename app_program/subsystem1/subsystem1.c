/* S1 status monitor. MQTT/controller queue integration remains pending.
 * Only the built-in network task may operate the radio or MQTT client.
 */
#include "subsystem1.h"

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

static ID wifi_status_tid;

static void wifi_status_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    for (;;) {
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

        tk_dly_tsk(2000);
    }
}

EXPORT ER subsystem1_init(void)
{
    T_CTSK task = {0};

    task.tskatr  = TA_HLNG | TA_RNG0;
    task.task    = (FP)wifi_status_task;
    task.itskpri = S1_TASK_PRIORITY;
    task.stksz   = TASK_STACK_SIZE;

    wifi_status_tid = tk_cre_tsk(&task);

    if (wifi_status_tid < E_OK) {
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
#else
EXPORT ER subsystem1_init(void)
{
    return E_OK;
}

EXPORT ER subsystem1_start(void)
{
    tm_printf((UB *)"[WIFI] disabled in this build\n");
    return E_OK;
}
#endif
