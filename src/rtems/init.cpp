#include <rtems.h>
#include <bsp.h>
#include <rtems/bspIo.h> /* Adicionado para resolver o erro do printk */

/* 
 * Cabeçalho onde queues_init() está declarado. 
 * Ajuste o nome da pasta ou arquivo se for diferente no seu projeto.
 */
#include "queues/queues.h"
#include "../drivers/spi_commons.h"
#include "../drivers/i3g4250d/i3g4250d.h"

/* ========================================================= */
/* Declarações de Tarefas                                    */
/* ========================================================= */

/* Protótipo da tarefa do algoritmo B-dot de detumbling. */
#include "tasks/bdot_task.h"
#include "tasks/housekeeping_task.h"
#include "tasks/sendreports_task.h"


/* ========================================================= */
/* Tarefa Init - Ponto de Entrada da Aplicação RTEMS         */
/* ========================================================= */

extern "C" rtems_task Init(rtems_task_argument argument) {
    (void) argument;

    rtems_id bdot_task_id;
    rtems_id housekeeping_task_id;
    rtems_id send_reports_task_id;
    rtems_name bdot_task_name = rtems_build_name('B', 'D', 'O', 'T');
    rtems_name housekeeping_task_name = rtems_build_name('H', 'K', 'E', 'P');
    rtems_name send_reports_task_name = rtems_build_name('T', 'M', 'T', 'X');
    rtems_status_code status;

    printk("\n--- Iniciando Computador de Bordo (STM32F4) ---\n");

    queues_init();

    init_sensor_spi();
    if (gyro_i3g4250d_init() != 0) {
        printk("Aviso: I3G4250D indisponível; telemetria e estado B-dot indicarão erro.\n");
    }
    
    /* 1. Criação da tarefa do loop de controle B-dot */
    status = rtems_task_create(
        bdot_task_name,
        10,                                 /* Prioridade (ex: 10, sendo 1 a mais alta) */
        RTEMS_MINIMUM_STACK_SIZE * 2,       /* Tamanho da pilha em RAM */
        RTEMS_DEFAULT_MODES,                /* Preempção habilitada por padrão */
        RTEMS_DEFAULT_ATTRIBUTES,           /* Tarefa local clássica */
        &bdot_task_id
    );

    if (status != RTEMS_SUCCESSFUL) {
        printk("Erro Crítico: Falha ao criar a tarefa B-dot (%s)\n", rtems_status_text(status));
        rtems_task_delete(RTEMS_SELF);
    }

    /* 2. Inicialização da tarefa */
    status = rtems_task_start(bdot_task_id, bdot_task, 0);

    if (status != RTEMS_SUCCESSFUL) {
        printk("Erro Crítico: Falha ao iniciar a tarefa B-dot (%s)\n", rtems_status_text(status));
    }

    /* 3. Criação e início da task que gera housekeepings periódicos. */
    status = rtems_task_create(
        housekeeping_task_name,
        20,
        RTEMS_MINIMUM_STACK_SIZE,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &housekeeping_task_id
    );

    if (status == RTEMS_SUCCESSFUL) {
        status = rtems_task_start(housekeeping_task_id, housekeeping_task, 0);
    }
    if (status != RTEMS_SUCCESSFUL) {
        printk("Erro: Falha ao iniciar a tarefa housekeeping (%s)\n", rtems_status_text(status));
    }

    /* 4. Criação e início da task de downlink I2C de telemetria. */
    status = rtems_task_create(
        send_reports_task_name,
        15,
        RTEMS_MINIMUM_STACK_SIZE * 2,
        RTEMS_DEFAULT_MODES,
        RTEMS_DEFAULT_ATTRIBUTES,
        &send_reports_task_id
    );

    if (status == RTEMS_SUCCESSFUL) {
        status = rtems_task_start(send_reports_task_id, send_reports_task, 0);
    }
    if (status != RTEMS_SUCCESSFUL) {
        printk("Erro: Falha ao iniciar a tarefa de telemetria (%s)\n", rtems_status_text(status));
    }

    /* 5. Limpeza: A tarefa Init finalizou o escalonamento inicial,
          podemos deletá-la para liberar recursos do sistema. */
    printk("Setup concluído. Deletando tarefa Init.\n");
    rtems_task_delete(RTEMS_SELF);
}


#include "rtems_config.h"
