#include "queues.h"

// Fila global de telemetria.
rtems_id queue_tx;

// construtor genérico
static rtems_id create_queue(
    char a, char b, char c, char d,
    uint32_t depth,
    size_t msg_size
) {
    rtems_id id;
    rtems_status_code status;

    status = rtems_message_queue_create(
        rtems_build_name(a, b, c, d),
        depth,
        msg_size,
        RTEMS_DEFAULT_ATTRIBUTES,
        &id
    );

    if (status != RTEMS_SUCCESSFUL) {
        return RTEMS_ID_NONE;
    }

    return id;
}

// inicialização centralizada
void queues_init(void) {

    // fila de pacotes (struct)
    queue_tx = create_queue(
        'A','D','C','Q',
        10,
        sizeof(pus_packet_t)
    );

    if (queue_tx == RTEMS_ID_NONE) {
        /* No task can operate safely without the downlink queue. */
        while (1);
    }
}
