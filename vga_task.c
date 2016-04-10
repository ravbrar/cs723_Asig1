#include <stdio.h>
#include <stdlib.h>
#include "sys/alt_irq.h"
#include "system.h"
#include "io.h"


#include "freq_struct.h"
#include "vga_task.h"

//For frequency plot
#define FREQPLT_ORI_X 101		//x axis pixel position at the plot origin
#define FREQPLT_GRID_SIZE_X 5	//pixel separation in the x axis between two data points
#define FREQPLT_ORI_Y 199.0		//y axis pixel position at the plot origin
#define FREQPLT_FREQ_RES 20.0	//number of pixels per Hz (y axis scale)

#define ROCPLT_ORI_X 101
#define ROCPLT_GRID_SIZE_X 5
#define ROCPLT_ORI_Y 259.0
#define ROCPLT_ROC_RES 0.5		//number of pixels per Hz/s (y axis scale)

#define MIN_FREQ 45.0 //minimum frequency to draw

#define PRVGADraw_Task_P      (tskIDLE_PRIORITY+1)



double freq[100], dfreq[100];

char *freq_info;
char *delta_info;
char *char_disp;

typedef struct{
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
}Line;

/****** VGA display ******/

void PRVGADraw_Task(void *pvParameters ){

	int index;
	int offset;
	int display_done;
	int value;
	int current_time;

	//Set up plot axes
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_hline(pixel_buf, 100, 590, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 50, 200, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);
	alt_up_pixel_buffer_dma_draw_vline(pixel_buf, 100, 220, 300, ((0x3ff << 20) + (0x3ff << 10) + (0x3ff)), 0);

	alt_up_char_buffer_string(char_buf, "Frequency(Hz)", 4, 4);
	alt_up_char_buffer_string(char_buf, "52", 10, 7);
	alt_up_char_buffer_string(char_buf, "50", 10, 12);
	alt_up_char_buffer_string(char_buf, "48", 10, 17);
	alt_up_char_buffer_string(char_buf, "46", 10, 22);

	alt_up_char_buffer_string(char_buf, "df/dt(Hz/s)", 4, 26);
	alt_up_char_buffer_string(char_buf, "60", 10, 28);
	alt_up_char_buffer_string(char_buf, "30", 10, 30);
	alt_up_char_buffer_string(char_buf, "0", 10, 32);
	alt_up_char_buffer_string(char_buf, "-30", 9, 34);
	alt_up_char_buffer_string(char_buf, "-60", 9, 36);



	int i = 99, j = 0;
	Line line_freq, line_roc;

	struct freq_struct my_freq_struct;

	while(1){

		//receive frequency data from queue
		while(uxQueueMessagesWaiting( Q_freq_data ) != 0){
			xQueueReceive( Q_freq_data, &my_freq_struct, 0 );

			freq[i] = my_freq_struct.current;
			dfreq[i] = my_freq_struct.delta;


			i =	++i%100; //point to the next data (oldest) to be overwritten

		}

		//clear old graph to draw new graph
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 0, 639, 199, 0, 0);
		alt_up_pixel_buffer_dma_draw_box(pixel_buf, 101, 201, 639, 299, 0, 0);

		for(j=0;j<99;++j){ //i here points to the oldest data, j loops through all the data to be drawn on VGA
			if (((int)(freq[(i+j)%100]) > MIN_FREQ) && ((int)(freq[(i+j+1)%100]) > MIN_FREQ)){
				//Calculate coordinates of the two data points to draw a line in between
				//Frequency plot
				line_freq.x1 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * j;
				line_freq.y1 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j)%100] - MIN_FREQ));

				line_freq.x2 = FREQPLT_ORI_X + FREQPLT_GRID_SIZE_X * (j + 1);
				line_freq.y2 = (int)(FREQPLT_ORI_Y - FREQPLT_FREQ_RES * (freq[(i+j+1)%100] - MIN_FREQ));

				//Frequency RoC plot
				line_roc.x1 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * j;
				line_roc.y1 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j)%100]);

				line_roc.x2 = ROCPLT_ORI_X + ROCPLT_GRID_SIZE_X * (j + 1);
				line_roc.y2 = (int)(ROCPLT_ORI_Y - ROCPLT_ROC_RES * dfreq[(i+j+1)%100]);

				//Draw
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_freq.x1, line_freq.y1, line_freq.x2, line_freq.y2, 0x3ff << 0, 0);
				alt_up_pixel_buffer_dma_draw_line(pixel_buf, line_roc.x1, line_roc.y1, line_roc.x2, line_roc.y2, 0x3ff << 0, 0);
			}
		}
		sprintf(freq_info, "Freq Threshold  %.1f", thres_freq);
		alt_up_char_buffer_string(char_buf, freq_info, 4, 40);
		sprintf(delta_info, "Delta Threshold  %.1f", thres_delta);
		alt_up_char_buffer_string(char_buf, delta_info, 4, 41);


		if (no_of_time_measurements > 0) {
			alt_up_char_buffer_string(char_buf, "Time taken", 50, 40);
			sprintf(char_disp, current_max_ticks == 0 ? "Max <1ms   " : "Max %dms   ", current_max_ticks);
			alt_up_char_buffer_string(char_buf, char_disp, 56, 41);
			sprintf(char_disp, current_min_ticks == 0 ? "Min <1ms   " : "Min %dms   ", current_min_ticks);
			alt_up_char_buffer_string(char_buf, char_disp, 56, 42);
			sprintf(char_disp, average_ticks < 1.0 ? "Avg <1ms   ": "Avg %.1fms   ", average_ticks);
			alt_up_char_buffer_string(char_buf, char_disp, 56, 43);

			display_done = 0;
			offset = 0;
			alt_up_char_buffer_string(char_buf, "History: ", 50, 45);
			while (display_done == 0){
				index = no_of_time_measurements - 1 - offset;
				if (offset++ == 5 || index < 0 ) {
					display_done = 1;
					break;
				}
				value = time_measurements[index % 5];
				sprintf(char_disp, value == 0 ? "<1ms   " : "%dms   ", value);
				alt_up_char_buffer_string(char_buf, char_disp, 60, 44 + offset);
			}
		}
		if(user_mode == 1){
			alt_up_char_buffer_string(char_buf,     "User Managed",33, 4);
		}
		else{
			if(load_stability_flag == 1){
				alt_up_char_buffer_string(char_buf, "  Unstable  ", 33, 4);
			}else{
				alt_up_char_buffer_string(char_buf, "   Stable   ", 33, 4);
			}
		}

		current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

		sprintf(char_disp, "System online %2dm %2ds   ", current_time / 60, current_time % 60);
		alt_up_char_buffer_string(char_buf, char_disp, 50, 54);

		vTaskDelay(10);



	}
}

void create_vga_task()
{

	xTaskCreate( PRVGADraw_Task, "DrawTsk", configMINIMAL_STACK_SIZE, NULL, PRVGADraw_Task_P, &PRVGADraw );



}

