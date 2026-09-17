
#ifndef QUEUES_H
#define QUEUES_H

#include <rtems.h>
#include "../../pus/pus_packet.h"

// Fila global de telemetria ADCS -> task do enlace UART OBDH.
extern rtems_id queue_tx;

// init centralizado
void queues_init(void);

#endif
