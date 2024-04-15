#include "Task_1.h"

// Global variables for thresholds and saved values
float RoCThreshold, FreqThreshold, RoCSaved, FreqSaved;
int tempFreqValue, curStabBool, startTickOutput;
double curFreqValue, lastFreqValue, rocValue;

// Interrupt service routine for frequency relay
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

// Main task function for frequency analysis
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
        curStabBool = (curFreqValue < FreqSaved) || (rocValue > RoCSaved) ? 0 : 1;
        xQueueOverwrite(stableStatusQueue, &curStabBool);

        vTaskResume(t2Handle); // Resume the load manager task
        lastFreqValue = curFreqValue; // Update the last frequency value
    }
}