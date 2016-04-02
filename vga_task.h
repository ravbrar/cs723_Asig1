/*
 * vga_task.h
 *
 *  Created on: 25/03/2016
 *      Author: Rav
 */

#ifndef VGA_TASK_H_
#define VGA_TASK_H_



#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/queue.h"
#include "alt_types.h"

#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"
QueueHandle_t Q_freq_data;

TaskHandle_t PRVGADraw;
alt_up_pixel_buffer_dma_dev *pixel_buf;
alt_up_char_buffer_dev *char_buf;

void create_vga_task(void);
void PRVGADraw_Task(void *pvParameters);
float thres_freq;
float thres_delta;
int load_stability_flag;

#endif /* VGA_TASK_H_ */
