#include "Task_3.h"


#define AverageTime 0
#define MaxTime 1
#define MinTime 2
#define MeasureOne 3
#define MeasureTwo 4
#define MeasureThree 5
#define MeasureFour 6
#define MeasureFive 7

// Variable declarations for timing calculations.
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
    }
}