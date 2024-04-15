#include "Main.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Altera includes*/
#include <unistd.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "sys/alt_irq.h"
#include "io.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"

/* Scheduler includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "FreeRTOS/timers.h"
#include "FreeRTOS/semphr.h"


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




// ------------ TASK 1 ANALYSER ---------------

// Variable declarations for task 1
float RoCThreshold, FreqThreshold, RoCSaved, FreqSaved;
int tempFreqValue, curStabBool, startTickOutput;
double curFreqValue, lastFreqValue, rocValue;



// Interrupt service routine for reading data from the frequency relay and storing it in xQueueSendFromISR
void freq_relay_ISR() {
    long lHigherPriorityTaskWoken = pdFALSE;
    startTickOutput = xTaskGetTickCount();  // Capture the current tick count
    freqOutput = IORD(FREQUENCY_ANALYSER_BASE, 0);  // Read the frequency from hardware
    xQueueOverwriteFromISR(startTickQueue, &startTickOutput, &lHigherPriorityTaskWoken);  // Write tick to queue
    xQueueSendFromISR(freqQueue, &freqOutput, &lHigherPriorityTaskWoken);  // Send frequency data to queue
    portEND_SWITCHING_ISR(lHigherPriorityTaskWoken);  // End ISR and possibly switch context
}



// PS2 keyboard interrupt service routine
void ps2_isr(void* ps2_device, alt_u32 id) {
    unsigned char key = 0;
    decode_scancode(ps2_device, NULL, &key, NULL);  // Decode the key press

    // Adjust RoC and Frequency thresholds based on key presses
    switch (key) {
    case 0x2d: // Increase RoC Threshold
        RoCThreshold += 2.5;
        break;
    case 0x24: // Decrease RoC Threshold
        RoCThreshold -= 2.5;
        break;
    case 0x2b: // Increase Frequency Threshold
        FreqThreshold += 0.1;
        break;
    case 0x23: // Decrease Frequency Threshold
        FreqThreshold -= 0.1;
        break;
    }
}



// Primary task for frequency analysis
void task_1_Analyser(void* pvParameters) {

    // Initialize thresholds and frequency values
    RoCThreshold = 20;
    FreqThreshold = 48.5;
    lastFreqValue = 50;
    freqDataOutput[0] = 50;
    freqDataOutput[1] = 0;

    while (1) {
        xQueueReceive(freqQueue, &tempFreqValue, (TickType_t)30); // Wait for new frequency value

        // Save threshold values to avoid changes during task execution
        RoCSaved = RoCThreshold;
        FreqSaved = FreqThreshold;

        // Calculate the current frequency and rate of change
        curFreqValue = 16000.0 / (double)tempFreqValue;
        rocValue = (curFreqValue - lastFreqValue) * 2.0 * curFreqValue * lastFreqValue / (curFreqValue + lastFreqValue);

        // Update output data
        freqDataOutput[0] = curFreqValue;
        freqDataOutput[1] = rocValue;
        xQueueOverwrite(freqDataQueue, &freqDataOutput);

        // Update threshold data
        thresholdSendArray[0] = FreqSaved;
        thresholdSendArray[1] = RoCSaved;
        xQueueOverwrite(threshQueue, &thresholdSendArray);

        // Determine stability and update queue
        // If the current frequency is too low, or the rate of change is too high, it updates the value of curStabBool
        curStabBool = (curFreqValue < FreqSaved) || (rocValue > RoCSaved) ? 0 : 1;
        xQueueOverwrite(stableStatusQueue, &curStabBool);

        vTaskResume(t2Handle); // Resume the load manager task
        lastFreqValue = curFreqValue; // Update the last frequency value
    }
}


// ---------- TASK 2 LOAD MANAGEMENT ---------------


// Definitions and variable declarations
#define loadCount 8

#define mantState 3
#define waitState 1
#define manageState 2

#define actionShed 0 // Define action shed as the process of turning loads off.
#define actionLoad 1 // Define action load as the process of turning loads on.

#define unstable 0 // Define a constant for unstable system state.
#define stable 1 // Define a constant for stable system state.

int loadStatus[loadCount] = { 1, 1, 1, 1, 1, 1, 1, 1 }; // Array to store the status of each load (on or off).
int fsmState, newStability, currentStability, finishTickOutput, maintenanceState, timingFlag; // Variables to manage states and timers.
int switchInput, switchEffect; int greenLedOutput, redLedOutput, switchState[loadCount]; // Variables for switch inputs and LED outputs.

void manageLoads(int manageAction); // Function prototype to manage the loading or shedding of loads.
void manageTimerCallback(xTimerHandle manageTimer); // Timer callback function to manage load actions based on timer events.




void manageTimerCallback(xTimerHandle manageTimer) {
    // Timer callback function to manage loads based on system stability.
    if (currentStability == unstable) {
        manageLoads(actionShed); // Shed loads if system is unstable.
        xTimerReset(manageTimer, 0); // Restart the timer.
    } else if (currentStability == stable) {
        manageLoads(actionLoad); // Load more if system is stable.
        xTimerReset(manageTimer, 0); // Restart the timer.
    }
    if (loadStatus[0] == 1) { // Check if the lowest priority load is active.
        xTimerStop(manageTimer, 0); // Stop the timer.
        fsmState = waitState; // Change the state to waiting.
    }
}




void manageLoads(int manageAction) {
    // Function to manage loads either by shedding or loading based on the action passed.
    int start, end, step; // Variables to control the loop behavior based on the action.

    if (manageAction == actionShed) {
        // Set parameters for shedding loads: iterate from lowest to highest priority.
        start = 0;
        end = loadCount;
        step = 1;
    } else if (manageAction == actionLoad) {
        // Set parameters for loading loads: iterate from highest to lowest priority.
        start = loadCount - 1;
        end = -1;
        step = -1;
    }

    for (int i = start; manageAction == actionShed ? i < end : i > end; i += step) {
        // Check the load's current state and switch it if conditions are met.
        if ((manageAction == actionShed && loadStatus[i] == 1) || (manageAction == actionLoad && loadStatus[i] == 0)) {
            loadStatus[i] = 1 - loadStatus[i]; // Toggle the status.
            break; // Exit the loop after changing one load's status.
        }
    }
}




void button_interrupts_function(void* context, alt_u32 id) {
    // Function to handle button interrupts.
    int* temp = (int*)context;
    *temp = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE); // Read the button press event.
    maintenanceState = !maintenanceState; // Toggle maintenance state on button press.

    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7); // Reset the edge capture register.
}




void task_2_Manager(void* pvParameters) {
    // Main task function for managing loads.

    fsmState = waitState; // Initialize state to waiting.
    currentStability = stable; // Start with the system stable.
    maintenanceState = 0; // Start with maintenance mode off.

    int buttonValue = 0;
    IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x4); // Configure edge capture for button interrupts.
    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x4); // Enable interrupts for the push button.
    alt_irq_register(PUSH_BUTTON_IRQ, (void*)&buttonValue, button_interrupts_function); // Register the interrupt handler.

    while (1) {
        xQueuePeek(stableStatusQueue, &newStability, (TickType_t)0); // Check for stability updates.
        switchInput = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE); // Read the current switch positions.

        // Handling different states of the finite state machine (FSM).
        if (fsmState == waitState) {
            switchEffect = switchInput; // Allow switches to have effect when waiting.

            if (newStability == unstable) { // If stability changes to unstable,

                manageLoads(actionShed); // Shed one load to stabilize the system.
                timingFlag = 1;
                xTimerStart(manageTimer, 0); // Start a timer for load management.
                currentStability = unstable; // Update stability status.
                fsmState = manageState; // Change state to managing loads.
            }
        } else if (fsmState == manageState) {
            if (newStability != currentStability) { // If stability status changes,
                xTimerReset(manageTimer, 0); // Reset the timer.
                currentStability = newStability; // Update the current stability.
            }
            // Modify switch inputs based on current loads during load management.
            for (int i = 0; i < 8; i++) {
                if (!(switchInput & (1 << i))) { // If a switch is turned off,
                    switchEffect &= ~(1 << i); // Reflect the change in switchEffect.
                }
            }
        } else if (fsmState == mantState) {
            switchEffect = switchInput; // Allow switches to have effect during maintenance.
        }

        // Update LED outputs based on current load and switch statuses.
        redLedOutput = 0;
        greenLedOutput = 0;
        if (maintenanceState) { // If in maintenance mode,
            redLedOutput = switchInput & 0b11111111; // Show switch status directly on red LEDs.
        } else {
            for (int i = 0; i < loadCount; i++) {
                greenLedOutput += (!maintenanceState && (!loadStatus[i] << i)) << i; // Update green LEDs to show loads that are off.
                redLedOutput += ((1 && (switchEffect & (1 << i))) << i); // Update red LEDs based on switch effects.
            }
        }

        if (timingFlag) { // If timing is flagged,
            finishTickOutput = xTaskGetTickCount(); // Capture the tick count when action is completed.
            xQueueOverwrite(finishTickQueue, &finishTickOutput); // Store the tick count in a queue.
            timingFlag = 0; // Reset the timing flag.
        }

        alt_up_char_buffer_dev* char_buf;
        char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");

        // Display the current system status on the character buffer.
        switch (fsmState) {
        case waitState:
            alt_up_char_buffer_string(char_buf, "System Status: Waiting  ", 25, 4); // Display waiting status.
            break;
        case manageState:
            alt_up_char_buffer_string(char_buf, "System Status: Managing ", 25, 4); // Display managing status.
            break;
        case mantState:
            alt_up_char_buffer_string(char_buf, "System Status: Maintenance", 25, 4); // Display maintenance status.
            break;
        }

        // Display the status of each load on the character buffer.
        for (int i = 0; i < 8; i++) {
            if (redLedOutput & (1 << i)) {
                alt_up_char_buffer_string(char_buf, "X", 39 + i * 4, 6); // Mark the active loads.
            } else {
                alt_up_char_buffer_string(char_buf, " ", 39 + i * 4, 6); // Leave spaces for inactive loads.
            }
        }

        IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, greenLedOutput); // Update the green LEDs output.
        IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, redLedOutput); // Update the red LEDs output.

        vTaskSuspend(t2Handle); // Suspend the task to be awoken by external events.
    }
}



// ------------- TASK 3 ---------------


// Definitions and variable declarations
#define AverageTime 0
#define MaxTime 1
#define MinTime 2
#define MeasureOne 3
#define MeasureTwo 4
#define MeasureThree 5
#define MeasureFour 6
#define MeasureFive 7

// Variable declarations for timing calculations
int tickCountStart, tickCountEnd, tickTimeDiff, currentTime;
double threshReceiveArray[2];



// Main function for tracking and recording time statistics.
void task_3_Tracker(void* pvParameters) {
    // Initialize the statistics message array with default values.
    statsMessage[AverageTime] = 0;  // Initial average time is set to zero.
    statsMessage[MaxTime] = 0;  // Initial maximum time is set to zero.
    statsMessage[MinTime] = 723;  // Set a large initial minimum time for comparison.
    statsMessage[MeasureOne] = 723;  // Initialize all measure times with the same large value.
    statsMessage[MeasureTwo] = 723;
    statsMessage[MeasureThree] = 723;
    statsMessage[MeasureFour] = 723;
    statsMessage[MeasureFive] = 723;

    while (1) {  // Infinite loop to continuously process incoming data.
        xQueueReceive(finishTickQueue, &tickCountEnd, portMAX_DELAY);
        // Block until a tick count is received indicating the end of a time measurement.
        xQueueReceive(startTickQueue, &tickCountStart, 0);
        // Try to receive a start tick count; does not block if no data is available.

        // Calculate the duration between start and end tick counts.
        currentTime = (tickCountEnd - tickCountStart);  // Time difference calculation.

        // Update maximum recorded time if the current time is greater.
        if (currentTime > statsMessage[MaxTime]) {
            statsMessage[MaxTime] = currentTime;
        }
        // Update minimum recorded time if the current time is smaller.
        if (currentTime < statsMessage[MinTime]) {
            statsMessage[MinTime] = currentTime;
        }

        // Shift recorded times down and insert the current time at the beginning.
        statsMessage[MeasureFive] = statsMessage[MeasureFour];
        statsMessage[MeasureFour] = statsMessage[MeasureThree];
        statsMessage[MeasureThree] = statsMessage[MeasureTwo];
        statsMessage[MeasureTwo] = statsMessage[MeasureOne];
        statsMessage[MeasureOne] = currentTime;

        // Calculate the average time from the five most recent measurements.
        statsMessage[AverageTime] = (statsMessage[MeasureOne] + statsMessage[MeasureTwo] +
            statsMessage[MeasureThree] + statsMessage[MeasureFour] +
            statsMessage[MeasureFive]) / 5;

        // Overwrite the previous statistics message in the queue with the updated one.
        xQueueOverwrite(statsQueue, (void*)&statsMessage);
    }
}


// --------------- Task 4 -----------------


// Define color constants using bit shifting for a color depth where each color channel (Red, Green, Blue) has 10 bits.
#define ColorWhite ((0x3ff << 20) + (0x3ff << 10) + (0x3ff))
#define ColorBlack (0)
#define ColorGreen (0x3ff)
#define ColorRed (0x3ff << 10)
#define ColorBlue (0x3ff << 20)
#define ColorCyan ((0x3ff << 20) + (0x3ff))

// Define constants for graphical layout positions and dimensions.
#define FreqYStart (20+30) // Start y-coordinate for Frequency graph.
#define FreqYEnd (20+30+90) // End y-coordinate for Frequency graph.
#define FreqTextStart 4 // Starting row for Frequency graph labels.
#define GraphXStart 100 // Start x-coordinate for graphs.
#define GraphXEnd (640-20-30-48) // End x-coordinate for graphs.
#define RoCYStart (20+30+90+30+16) // Start y-coordinate for Rate of Change graph.
#define RocYEnd (20+30+90+30+90+16) // End y-coordinate for Rate of Change graph.
#define RoCTextStart 21 // Starting row for Rate of Change graph labels.

// Define character buffer positions for displaying various statistical data.
#define FirstColStart 5
#define SecondColStart 34
#define ThirdColStart 58
#define FirstRowHeight 40
#define SecRowHeight 43
#define ThirdRowHeight 46
#define FourthRowHeight 49
#define FifthRowHeight 52
#define SixthRowHeight 55

// Arrays for holding values to be graphed and inputs.
double valueArray[18] = { 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50 };
double rocArray[18], freqDataInput[2];
float thresholdReceiveArray[2];
char outputBuffer[4];
int tempTimeHours, tempTimeMin, tempROC, timeStatCount, stableStatsInput;
int statsReceiveArray[8] = { 723, 723, 723, 723, 723, 723, 723, 723 };

int tickCountStart, tickCountEnd;

void refreshTimerCallback(xTimerHandle refreshTimer) {
    vTaskResume(t4Handle); // Resume the task when the timer callback is called.
}

// Main task function for managing the VGA display.
void task_4_VGA_Controller(void* pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 250; // Refresh frequency for the task.

    // Initialize the Pixel Buffer for graphics.
    alt_up_pixel_buffer_dma_dev* pixel_buf;
    pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
    if (pixel_buf == NULL) {
        printf("Cannot find pixel buffer device\n"); // Error message if device not found.
    }

    // Initialize the character buffer for text.
    alt_up_char_buffer_dev* char_buf;
    char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
    if (char_buf == NULL) {
        printf("can't find char buffer device\n"); // Error message if device not found.
    }

    // Clear the screen and set up the initial graphics and text.
    alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0); // Clear the pixel buffer.
    alt_up_char_buffer_clear(char_buf); // Clear the character buffer.
    alt_up_pixel_buffer_dma_draw_box(pixel_buf, 0, 0, 640, 480, ColorBlack, 0); // Set the background to black.
    alt_up_pixel_buffer_dma_draw_rectangle(pixel_buf, 0, 0, 639, 479, ColorRed, 0); // Draw a red frame around the screen.

    // Draw and label the Frequency/Time Graph.
    alt_up_pixel_buffer_dma_draw_vline(pixel_buf, GraphXStart, FreqYStart, FreqYEnd, ColorWhite, 0); // Draw the Y-axis.
    alt_up_pixel_buffer_dma_draw_hline(pixel_buf, GraphXStart, GraphXEnd, FreqYEnd, ColorWhite, 0); // Draw the X-axis.
    for (int i = 0; i <= 9; i += 2) {
        alt_up_pixel_buffer_dma_draw_hline(pixel_buf, GraphXStart - 10, GraphXStart, FreqYStart + (i * 10), ColorWhite, 0);
    }
    // Add labels for the Frequency graph.
    alt_up_char_buffer_string(char_buf, " -4 Sec       -3 Sec       -2 Sec        -1 Sec       -0 Sec", 12, FreqTextStart + 14);
    alt_up_char_buffer_string(char_buf, "Frequency (Hz)", 3, FreqTextStart);
    alt_up_char_buffer_string(char_buf, "52Hz", 6, FreqTextStart + 2);
    alt_up_char_buffer_string(char_buf, "51Hz", 6, FreqTextStart + 4);
    alt_up_char_buffer_string(char_buf, "50Hz", 6, FreqTextStart + 7);
    alt_up_char_buffer_string(char_buf, "49Hz", 6, FreqTextStart + 9);
    alt_up_char_buffer_string(char_buf, "48Hz", 6, FreqTextStart + 12);

    // Draw and label the Rate of Change graph similarly to the Frequency graph.
    alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, RoCYStart, RocYEnd, ColorWhite, 0); // Draw the Y-axis for Rate of Change.
    alt_up_pixel_buffer_dma_draw_hline(pixel_buf, GraphXStart, GraphXEnd, RocYEnd, ColorWhite, 0); // Draw the X-axis for Rate of Change.
    for (int i = 0; i <= 9; i += 2) {
        alt_up_pixel_buffer_dma_draw_hline(pixel_buf, GraphXStart - 10, GraphXStart, RoCYStart + 5 + (i * 10), ColorWhite, 0);
    }
    // Add labels for the Rate of Change graph.
    alt_up_char_buffer_string(char_buf, " -4 Sec       -3 Sec       -2 Sec        -1 Sec        0 Sec", 12, RoCTextStart + 14);
    alt_up_char_buffer_string(char_buf, "Rate of Change (dF/dt Hz/S)", 3, RoCTextStart);
    alt_up_char_buffer_string(char_buf, "+100", 10 - 3, RoCTextStart + 3);
    alt_up_char_buffer_string(char_buf, "+50", 10 - 3, RoCTextStart + 5);
    alt_up_char_buffer_string(char_buf, "0", 10 - 1, RoCTextStart + 8);
    alt_up_char_buffer_string(char_buf, "-50", 10 - 3, RoCTextStart + 10);
    alt_up_char_buffer_string(char_buf, "-100", 10 - 3, RoCTextStart + 13);

    // Setup and display system status and threshold information on the character buffer.
    alt_up_char_buffer_string(char_buf, "System Status:", FirstColStart, FirstRowHeight);
    alt_up_char_buffer_string(char_buf, "Freq Threshold:", FirstColStart, SecRowHeight);
    alt_up_char_buffer_string(char_buf, "RoC Threshold:", FirstColStart, ThirdRowHeight);
    alt_up_char_buffer_string(char_buf, "Total Time Active:   :  :", FirstColStart, FourthRowHeight);
    alt_up_char_buffer_string(char_buf, "Average Time:", SecondColStart, FirstRowHeight);
    alt_up_char_buffer_string(char_buf, "Max Time:", SecondColStart, SecRowHeight);
    alt_up_char_buffer_string(char_buf, "Min Time:", SecondColStart, ThirdRowHeight);
    alt_up_char_buffer_string(char_buf, "Display Refreshed @ 4Hz", FirstColStart, SixthRowHeight);

    // Continuously update the display based on new data received.
    while (1) {
        xQueuePeek(statsQueue, &statsReceiveArray, (TickType_t)0); // Peek at statistics queue for updates.
        xQueuePeek(freqDataQueue, &freqDataInput, (TickType_t)0); // Peek at frequency data queue.
        xQueuePeek(threshQueue, &thresholdReceiveArray, (TickType_t)0); // Peek at threshold values queue.
        xQueuePeek(stableStatusQueue, &stableStatsInput, (TickType_t)0); // Peek at system stability status.

        timeStatCount = xTaskGetTickCount() / configTICK_RATE_HZ; // Calculate the total time the system has been active.

        // Update the frequency and Rate of Change arrays with the latest data.
        for (int i = 17; i > 0; i--) {
            valueArray[i] = valueArray[i - 1]; // Shift the values to make room for new data.
        }
        valueArray[0] = freqDataInput[0]; // Insert new frequency data.
        for (int i = 17; i > 0; i--) {
            rocArray[i] = rocArray[i - 1]; // Shift the ROC values.
        }
        // Calculate Rate of Change based on the frequency values.
        rocArray[0] = (valueArray[0] - valueArray[1]) * 2.0 * valueArray[0] * valueArray[1] / (valueArray[0] + valueArray[1]);

        // Update graphical representation of frequency and ROC on the VGA display.
        for (int i = 0; i < 17; i++) {
            int tempStart = GraphXEnd - 25 - i * 26; // Calculate start position for each bar in the graph.
            double tempY = 50 + 90 - (valueArray[i] - 48) * 20; // Calculate Y position based on value.
            double tempY2 = 50 + 90 - (valueArray[i + 1] - 48) * 20; // Calculate next Y position.
            alt_up_pixel_buffer_dma_draw_box(pixel_buf, tempStart, FreqYStart + 1, tempStart + 26, FreqYEnd - 1, ColorBlack, 0); // Clear the current bar.
            alt_up_pixel_buffer_dma_draw_line(pixel_buf, tempStart, tempY2, tempStart + 26, tempY, ColorBlue, 0); // Draw the line for current frequency value.
        }

        for (int i = 0; i < 17; i++) { // TODO: maybe some stuff about getting the largest roc in each refresh period
            int tempStart = GraphXEnd - 25 - i * 26; // Calculate start position for each bar in the ROC graph.
            double tempY = RoCYStart + 45 - (rocArray[i]) * 0.45; //0.75; // Calculate Y position based on ROC value.
            alt_up_pixel_buffer_dma_draw_box(pixel_buf, tempStart, RoCYStart + 1, tempStart + 26, RocYEnd - 1, ColorBlack, 0); // Clear the current bar.
            alt_up_pixel_buffer_dma_draw_box(pixel_buf, tempStart, RoCYStart + 45, tempStart + 26, tempY, ColorBlue, 0); // Draw the bar for current ROC value.
        }

        // Display current stability status using colors and text.
        if (stableStatsInput) {
            alt_up_char_buffer_string(char_buf, "Stable  ", FirstColStart + 15, FirstRowHeight);
            alt_up_pixel_buffer_dma_draw_box(pixel_buf, (FirstColStart + 15) * 8 - 5, FirstRowHeight * 8 - 5, (FirstColStart + 23) * 8 + 4, (FirstRowHeight + 1) * 8 + 4, ColorBlack, 0);
            alt_up_pixel_buffer_dma_draw_box(pixel_buf, (FirstColStart + 15) * 8 - 5, FirstRowHeight * 8 - 5, (FirstColStart + 21) * 8 + 4, (FirstRowHeight + 1) * 8 + 4, ColorGreen, 0);
        } else {
            alt_up_char_buffer_string(char_buf, "UnStable", FirstColStart + 15, FirstRowHeight);
            alt_up_pixel_buffer_dma_draw_box(pixel_buf, (FirstColStart + 15) * 8 - 5, FirstRowHeight * 8 - 5, (FirstColStart + 23) * 8 + 4, (FirstRowHeight + 1) * 8 + 4, ColorRed, 0);
        }

        // Display total time active in hours, minutes, and seconds.
        tempTimeHours = timeStatCount / 3600;
        sprintf(outputBuffer, "%02d", tempTimeHours);
        alt_up_char_buffer_string(char_buf, outputBuffer, FirstColStart + 19, FourthRowHeight);
        tempTimeMin = (timeStatCount - (3600 * tempTimeHours)) / 60;
        sprintf(outputBuffer, "%02d", tempTimeMin);
        alt_up_char_buffer_string(char_buf, outputBuffer, FirstColStart + 22, FourthRowHeight);
        sprintf(outputBuffer, "%02d", (timeStatCount - (3600 * tempTimeHours) - (60 * tempTimeMin)));
        alt_up_char_buffer_string(char_buf, outputBuffer, FirstColStart + 25, FourthRowHeight);

        // Display live frequency and Rate of Change data.
        sprintf(outputBuffer, "%6.3f Hz", freqDataInput[0]);
        alt_up_char_buffer_string(char_buf, outputBuffer, 69, 11);
        sprintf(outputBuffer, "%d Hz/Sec   ", (int)rocArray[0]);
        alt_up_char_buffer_string(char_buf, outputBuffer, 69, RoCTextStart + 7);
        // Display frequency and ROC thresholds.
        sprintf(outputBuffer, "%4.1f Hz", thresholdReceiveArray[0]);
        alt_up_char_buffer_string(char_buf, outputBuffer, FirstColStart + 16, SecRowHeight);
        sprintf(outputBuffer, "%3.1f Hz/Sec", thresholdReceiveArray[1]);
        alt_up_char_buffer_string(char_buf, outputBuffer, FirstColStart + 15, ThirdRowHeight);
        // Display time statistics (average, max, min).
        sprintf(outputBuffer, "%03d ms", statsReceiveArray[0]);
        alt_up_char_buffer_string(char_buf, outputBuffer, SecondColStart + 14, FirstRowHeight);
        sprintf(outputBuffer, "%03d ms", statsReceiveArray[1]);
        alt_up_char_buffer_string(char_buf, outputBuffer, SecondColStart + 10, SecRowHeight);
        sprintf(outputBuffer, "%03d ms", statsReceiveArray[2]);
        alt_up_char_buffer_string(char_buf, outputBuffer, SecondColStart + 10, ThirdRowHeight);

        sprintf(outputBuffer, "    %03d ms", statsReceiveArray[3]);
        alt_up_char_buffer_string(char_buf, outputBuffer, ThirdColStart, SecRowHeight);

        sprintf(outputBuffer, "    %03d ms", statsReceiveArray[4]);
        alt_up_char_buffer_string(char_buf, outputBuffer, ThirdColStart, ThirdRowHeight);

        sprintf(outputBuffer, "    %03d ms", statsReceiveArray[5]);
        alt_up_char_buffer_string(char_buf, outputBuffer, ThirdColStart, FourthRowHeight);

        sprintf(outputBuffer, "    %03d ms", statsReceiveArray[6]);
        alt_up_char_buffer_string(char_buf, outputBuffer, ThirdColStart, FifthRowHeight);

        sprintf(outputBuffer, "    %03d ms", statsReceiveArray[7]);
        alt_up_char_buffer_string(char_buf, outputBuffer, ThirdColStart, SixthRowHeight);

        vTaskSuspend(t4Handle); // Suspend the task until the next refresh cycle.
    }
}

