// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Scheduler includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <unistd.h>
#include "altera_up_avalon_video_character_buffer_with_dma.h"
#include "altera_up_avalon_video_pixel_buffer_dma.h"

#include "alt_types.h"                 	// alt_u32 is a kind of alt_types
#include "sys/alt_irq.h"              	// to register interrupts
#include <altera_avalon_pio_regs.h>
#include "system.h"                     // to use the symbolic names

#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"
#include "io.h"
// Definition of Task Stacks
#define   TASK_STACKSIZE       2048


// Freq relay Assignment priority
#define VGA_TASK_PRIORITY 1
#define LED_OUT_PRIORITY 2
#define LOAD_USER_MNGMNT_PRIORITY 3
#define FREQUENCY_CALCULATOR_PRIORITY 4



// Definition of Message Queue
#define   MSG_QUEUE_SIZE  30
QueueHandle_t msgqueue;

// used to delete a task
 TaskHandle_t xHandle = NULL;

 TaskHandle_t xTaskToNotify = NULL;


// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;

// globals variables for interrupt functions

int frequency_value = 0;

// Local Function Prototypes
int initOSDataStructs(void);
int initCreateTasks(void);


xSemaphoreHandle shared_sem;


// VGA
alt_up_char_buffer_dev *char_buf;
// For exercise in lab

void ps2_isr (void* context, alt_u32 id)
{
  char ascii;
  int status = 0;
  unsigned char key = 0;
  KB_CODE_TYPE decode_mode;
  status = decode_scancode (context, &decode_mode , &key , &ascii) ;
  if ( status == 0 ) //success
  {
    // print out the result
    switch ( decode_mode )
    {
      case KB_ASCII_MAKE_CODE :
        printf ( "ASCII   : %x\n", key ) ;
        break ;
      case KB_LONG_BINARY_MAKE_CODE :
        // do nothing
      case KB_BINARY_MAKE_CODE :
        printf ( "MAKE CODE : %x\n", key ) ;
        break ;
      case KB_BREAK_CODE :
        // do nothing
      default :
        printf ( "DEFAULT   : %x\n", key ) ;
        break ;
    }
    IOWR(SEVEN_SEG_BASE,0 ,key);
  }
}

void button_interrupts_function(void* context, alt_u32 id)
{
  // need to cast the context first before using it
	printf("button pressed \n");
	int buttonValue = 0;
	BaseType_t xHigherPriorityTaskWoken;

	buttonValue = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);
	// clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);


	xHigherPriorityTaskWoken = pdFALSE;

	xTaskNotifyFromISR( xHandle,
			buttonValue,
	                        eSetValueWithOverwrite,
	                        &xHigherPriorityTaskWoken );

	//portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
 }

// Frequency Analyzer VHDL interrupt
void frequency_interrupt_function(void* context, alt_u32 id){

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	configASSERT(xTaskToNotify != NULL);
	frequency_value = 16000 / IORD_ALTERA_AVALON_PIO_DATA(FREQUENCY_ANALYSER_BASE);
	vTaskNotifyGiveFromISR( xTaskToNotify, &xHigherPriorityTaskWoken );
	printf("frequecny ISR %d\n", frequency_value);
	//printf("HANDLE: %d\n",(int)xTaskToNotify);
	//portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}


void VGA_task(void* pvParameters){

	while(1){
		alt_up_char_buffer_string(char_buf, "Hello World", 40, 30);
		//usleep(1000000);
		alt_up_char_buffer_draw(char_buf, '!', 51, 30);
		//usleep(1000000);
		alt_up_char_buffer_clear(char_buf);
		//usleep(1000000);
	}
}

void frequency_calculator(void* pvParameters){

	xTaskToNotify = xTaskGetCurrentTaskHandle();
	printf("HANDLE TASK: %d\n",xTaskToNotify);
	while(1){
		ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

		        /* The transmission ended as expected. */
				printf("Current Frequecny: %d \n", frequency_value);
				//usleep(1000000);
	}

}

void load_user_mgmnt(void* pvParameters){

	while(1){

	}
}

void Led_Out(void* pvParameters){
	uint32_t buttonValue;
	xHandle = xTaskGetCurrentTaskHandle();
	while(1){

		 xTaskNotifyWait( 0x00,
		                         ULONG_MAX,
		                         &buttonValue,
		                         portMAX_DELAY );
		IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, buttonValue);
		printf("LED TASK\n");
	}
}



int main(int argc, char* argv[], char* envp[])
{
	initOSDataStructs();
	initInterrupts();
	initVGA();
	initCreateTasks();
	vTaskStartScheduler();
	for (;;);
	return 0;
}
//

void initVGA(void){
	//reset the display
		alt_up_pixel_buffer_dma_dev *pixel_buf;
		pixel_buf = alt_up_pixel_buffer_dma_open_dev(VIDEO_PIXEL_BUFFER_DMA_NAME);
		if(pixel_buf == NULL){
			printf("Cannot find pixel buffer device\n");
		}
		alt_up_pixel_buffer_dma_clear_screen(pixel_buf, 0);

		//initialize character buffer

		char_buf = alt_up_char_buffer_open_dev("/dev/video_character_buffer_with_dma");
		if(char_buf == NULL){
			printf("can't find char buffer device\n");
		}
}
void initInterrupts(void){

	// PS2
	 alt_up_ps2_dev * ps2_device = alt_up_ps2_open_dev(PS2_NAME);

	  if(ps2_device == NULL){
	    printf("can't find PS/2 device\n");
	    return 1;
	  }

	  alt_up_ps2_clear_fifo (ps2_device) ;

	  alt_irq_register(PS2_IRQ, ps2_device, ps2_isr);
	  // register the PS/2 interrupt
	  IOWR_8DIRECT(PS2_BASE,4,1);


	  // button

	  IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);

	    // enable interrupts for all buttons
	    IOWR_ALTERA_AVALON_PIO_IRQ_MASK(PUSH_BUTTON_BASE, 0x7);
	  alt_irq_register(PUSH_BUTTON_IRQ, NULL, button_interrupts_function);


	  	 // frequency analyzer
	  alt_irq_register(FREQUENCY_ANALYSER_IRQ,(void*)&frequency_value, frequency_interrupt_function);
}
// This function simply creates a message queue and a semaphore
int initOSDataStructs(void)
{

	msgqueue = xQueueCreate( MSG_QUEUE_SIZE, sizeof( void* ) );
	shared_resource_sem = xSemaphoreCreateCounting( 9999, 1 );
	return 0;
}

// This function creates the tasks used in this example
int initCreateTasks(void)
{


	//xTaskCreate(VGA_task, "VGA_task", TASK_STACKSIZE, NULL, VGA_TASK_PRIORITY, NULL);
	//xTaskCreate(Led_Out, "Led_Out", TASK_STACKSIZE, NULL, LED_OUT_PRIORITY, NULL);
	//xTaskCreate(load_user_mgmnt, "load_user_mgmnt", TASK_STACKSIZE, NULL, LOAD_USER_MNGMNT_PRIORITY, NULL);
	//xTaskCreate(frequency_calculator, "frequency_calculator", TASK_STACKSIZE, NULL, FREQUENCY_CALCULATOR_PRIORITY, NULL);


	return 0;
}
