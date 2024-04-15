#include "Task_4.h"

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