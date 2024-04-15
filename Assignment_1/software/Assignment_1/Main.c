#include "Main.h"

#include "Task_1.h"
#include "Task_2.h"
#include "Task_3.h"
#include "Task_4.h"

// Define task priorities with respect to the idle priority.
#define task_1_PRIORITY       ( tskIDLE_PRIORITY + 4) // Frequency Analyser task has highest priority.
#define task_2_PRIORITY       ( tskIDLE_PRIORITY + 3)
#define task_3_PRIORITY       ( tskIDLE_PRIORITY + 2)
#define task_4_PRIORITY       ( tskIDLE_PRIORITY + 1) // VGA task has lowest priority.

// Task handles for task management.
TaskHandle_t t1Handle = NULL;
TaskHandle_t t2Handle = NULL;
TaskHandle_t t3Handle = NULL;
TaskHandle_t t4Handle = NULL;

int main(void) {
    // Create queues for inter-task communication. Each queue has a capacity for one item of the respective type.
    startTickQueue = xQueueCreate(1, sizeof(int)); // Queue for starting tick counts.
    finishTickQueue = xQueueCreate(1, sizeof(int)); // Queue for finishing tick counts.

    freqQueue = xQueueCreate(1, sizeof(freqOutput)); // Queue for frequency outputs.
    freqDataQueue = xQueueCreate(1, sizeof(freqDataOutput)); // Queue for frequency data outputs.
    threshQueue = xQueueCreate(1, sizeof(thresholdSendArray)); // Queue for threshold data.

    statsQueue = xQueueCreate(1, sizeof(statsMessage)); // Queue for statistical messages.
    stableStatusQueue = xQueueCreate(1, sizeof(stabilityOutput)); // Queue for stability status.

    startTickTime = xTaskGetTickCount(); // Get the current tick count as a reference for timing.

    // Initialize PS/2 device for input handling.
    alt_up_ps2_dev* ps2_device = alt_up_ps2_open_dev(PS2_NAME);
    if (ps2_device == NULL) {
        printf("can't find PS/2 device\n"); // Error message if PS/2 device not found.
        return 1;
    }
    alt_up_ps2_enable_read_interrupt(ps2_device); // Enable read interrupts for the PS/2 device.
    alt_irq_register(PS2_IRQ, ps2_device, ps2_isr); // Register the PS/2 interrupt service routine.

    // Register an interrupt service routine for the frequency analyser.
    alt_irq_register(FREQUENCY_ANALYSER_IRQ, 0, freq_relay_ISR);

    // Create and start a timer for refreshing the VGA display.
    refreshTimer = xTimerCreate("Refresh Timer", pdMS_TO_TICKS(vgaRefreshMs), pdTRUE, NULL, refreshTimerCallback);
    if (xTimerStart(refreshTimer, 0) != pdPASS) {
        printf("Cannot start timer"); // Error message if timer cannot be started.
    }

    // Create a management timer for managing tasks or events at 500 ms intervals.
    manageTimer = xTimerCreate("Management Timer", pdMS_TO_TICKS(500), pdTRUE, NULL, manageTimerCallback);

    // Create tasks for different functionalities of the system.
    xTaskCreate(task_1_Analyser, "Freq_Analyser", configMINIMAL_STACK_SIZE, NULL, task_1_PRIORITY, &t1Handle);
    xTaskCreate(task_2_Manager, "Load_Manager", configMINIMAL_STACK_SIZE, NULL, task_2_PRIORITY, &t2Handle);
    xTaskCreate(task_3_Tracker, "Stats_Tracker", configMINIMAL_STACK_SIZE, NULL, task_3_PRIORITY, &t3Handle);
    xTaskCreate(task_4_VGA_Controller, "VGA_Controller", configMINIMAL_STACK_SIZE, NULL, task_4_PRIORITY, &t4Handle);

    // Start the FreeRTOS scheduler to begin task execution.
    vTaskStartScheduler();

    // Infinite loop to keep the main function running. Should never be reached if the scheduler is running properly.
    for (;;);
}