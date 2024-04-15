#include "Task_2.h" 
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
                redLedOutput += ((1 && (switchEffect & (1 << i))) << i); // Update red LEDs based on switch effects.
                greenLedOutput += (!maintenanceState && (!loadStatus[i] << i)) << i; // Update green LEDs to show loads that are off.
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