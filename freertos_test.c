// Standard includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "vga_task.h"

// Scheduler includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <unistd.h>
#include "freq_struct.h"
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
#define NO_OF_LOADS 8

// loads array
int loads[NO_OF_LOADS];
int controller_inputs[NO_OF_LOADS];
int user_inputs[NO_OF_LOADS];
int no_of_activated_loads = 0;

int loads_shed = 0;
int relay_leds = 0;
int red_led_out = 0;
int green_led_out = 0;

enum load_state{
    off=0,
    on,
    shed
};

enum load_state load_state_array[NO_OF_LOADS];

// Definition of Message Queue
#define   MSG_QUEUE_SIZE  30
QueueHandle_t msgqueue;

QueueHandle_t load_mngmnt_queue;

// used to delete a task
 TaskHandle_t xHandle = NULL;

 TaskHandle_t xFreqTask;

 TaskHandle_t xLoadMgnmntTask;

 TaskHandle_t xbuttonLEDs;

// user inputs


 SemaphoreHandle_t x_sem_loads;


// Definition of Semaphore
SemaphoreHandle_t shared_resource_sem;

// globals variables for interrupt functions

double frequency_value = 0;
int buttonValue = 0;
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

	BaseType_t xHigherPriorityTaskWoken;

	buttonValue = IORD_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE);
	// clears the edge capture register
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(PUSH_BUTTON_BASE, 0x7);


	xHigherPriorityTaskWoken = pdFALSE;

	xTaskNotifyFromISR( xbuttonLEDs,
			buttonValue,
	                        eSetValueWithOverwrite,
	                        &xHigherPriorityTaskWoken );

	//portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
 }

// Frequency Analyzer VHDL interrupt
void frequency_interrupt_function(void* context, alt_u32 id){

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	uint32_t value;

	configASSERT(xTaskToNotify != NULL);
	value = IORD_ALTERA_AVALON_PIO_DATA(FREQUENCY_ANALYSER_BASE);
	xTaskNotifyFromISR( xFreqTask, value, eSetBits, &xHigherPriorityTaskWoken );
	//printf("frequecny ISR %d\n", frequency_value);
	//printf("HANDLE: %d\n",(int)xTaskToNotify);
	portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
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
	uint32_t ulNotificationValue, frequency_previous;
	double frequency_delta, abs_delta;
	struct freq_struct freq;
	static int activate_load_management = 0;

	while(1){
		//printf("Current Frequecny TASK: %d \n", frequency_value);
//		ulNotificationValue = ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		xTaskNotifyWait(0x00,               /* Don't clear any bits on entry. */
					 ULONG_MAX,          /* Clear all bits on exit. */
					 &ulNotificationValue, /* Receives the notification value. */
					 portMAX_DELAY );    /* Block indefinitely. */
		frequency_previous = frequency_value;
		frequency_value = 16000.0 / ulNotificationValue;
		frequency_delta = (frequency_value - frequency_previous) * 2.0 * frequency_value * frequency_previous / (frequency_value + frequency_previous);
		freq.current = frequency_value;
		freq.delta = frequency_delta;
		abs_delta = frequency_delta < 0 ? frequency_delta * -1 : frequency_delta;
		//printf("activate_load_management %d  \n", activate_load_management);
		//printf("frequency_delta %f  \n", (frequency_delta));
		if (((frequency_value < thres_freq) || (abs_delta > thres_delta) ) && activate_load_management == 0){
			activate_load_management = 1;
			xQueueSendToBack( load_mngmnt_queue, (void *)&activate_load_management, (TickType_t)0 );
		}else if ((frequency_value >= thres_freq && abs_delta <= thres_delta )  && activate_load_management == 1){
			activate_load_management = 0;
			xQueueSendToBack( load_mngmnt_queue, (void *)&activate_load_management, (TickType_t)0 );
		}

		//printf("Frequency %.2f Hz \n", frequency_value);
		//printf("dF/dt %.2f \n", frequency_delta);

				xQueueSendToBack( Q_freq_data, (void *)&freq, (TickType_t)0 );


		//if (frequency_previous != frequency_value)
		//	printf("Frequency %.2f Hz \n", frequency_value);
	}

}

void load_user_inputs(void){

	int user_input = IORD_ALTERA_AVALON_PIO_DATA(SLIDE_SWITCH_BASE);
	int i;
	for( i = 0; i<NO_OF_LOADS; i++){
		loads[i] = user_input & 1<<i ? 1 : 0;
	}

}
void load_user_mgmnt(void* pvParameters){
	int i;
	int initiate_load_shed = 0;
		while(1){


			load_user_inputs();

			 if( xQueueReceive( load_mngmnt_queue, &( load_stability_flag ), ( TickType_t ) pdMS_TO_TICKS(500) ) )
			        {
			            // pcRxedMessage now points to the struct AMessage variable posted
			            // by vATask.
				 	 //
				 no_of_activated_loads = 0;
					for( i = 0; i<NO_OF_LOADS; i++){
						no_of_activated_loads += loads[i];
						}
				 	 if (load_stability_flag == 1 && loads_shed == 0 && (loads_shed < no_of_activated_loads)){ // needs to be within 200ms
				 		 shed_load();
				 	 }


			        }
			 else if(load_stability_flag == 1 && (loads_shed < no_of_activated_loads)){
				 shed_load();
				 //printf("drop another load \n");

			 }
			 else if(load_stability_flag == 0){
				 if(loads_shed > 0){

					 reenable_load();


				 }else{
						for( i = 0; i<NO_OF_LOADS; i++){
								load_state_array[i] = loads[i] ? on : off;
							}
				 }


			 }
			 red_led_out = 0;
			 green_led_out = 0;
				for( i = 0; i<NO_OF_LOADS; i++){
						red_led_out += load_state_array[i] == on ? 1 << i : 0;
						green_led_out += load_state_array[i] == shed ? 1 << i : 0;
					}


				IOWR_ALTERA_AVALON_PIO_DATA(RED_LEDS_BASE, red_led_out);
				IOWR_ALTERA_AVALON_PIO_DATA(GREEN_LEDS_BASE, green_led_out);

		}


}

void reenable_load(void){

	int i;
	for( i = NO_OF_LOADS; i >= 0; i--){
		if (load_state_array[i] == shed) {
			load_state_array[i] = on;
			loads_shed--;
			printf("load reenabled: %d \n", i);
			break;
		}
	}
}
void shed_load(void){

	int i;
	for( i = 0; i<NO_OF_LOADS; i++){
		if (load_state_array[i] == on) {
			load_state_array[i] = shed;
			loads_shed++;
			printf("load shed: %d \n", i);
			break;
		}
	}


}



int main(int argc, char* argv[], char* envp[])
{
	printf("Started\n");
	initOSDataStructs();
	printf("Init structs\n");
	initInterrupts();
	printf("Init interrupts\n");
	initVGA();
	printf("Init vga\n");
	initCreateTasks();
	printf("Init tasks\n");
	vTaskStartScheduler();
	printf("Init scheduler\n");
	for (;;);
	return 0;
}
//

void initVGA(void){
	//reset the display

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
		alt_up_char_buffer_clear(char_buf);
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
//
//
	  	 // frequency analyzer
	  alt_irq_register(FREQUENCY_ANALYSER_IRQ,(void*)&frequency_value, frequency_interrupt_function);
}
// This function simply creates a message queue and a semaphore
int initOSDataStructs(void)
{
	thres_freq = 49.00;
	thres_delta = 25.00;
	load_stability_flag = 0;
	x_sem_loads = xSemaphoreCreateBinary();
	Q_freq_data = xQueueCreate( 100, sizeof( struct freq_struct) );
	load_mngmnt_queue = xQueueCreate( MSG_QUEUE_SIZE, sizeof( int) );
	msgqueue = xQueueCreate( MSG_QUEUE_SIZE, sizeof( void* ) );
	shared_resource_sem = xSemaphoreCreateCounting( 9999, 1 );
	return 0;
}

// This function creates the tasks used in this example
int initCreateTasks(void)
{



	//create_vga_task();
	xTaskCreate(PRVGADraw_Task, "VGA_Task", TASK_STACKSIZE, NULL, VGA_TASK_PRIORITY, &PRVGADraw);
//	xTaskCreate(Led_Out, "Led_Out", TASK_STACKSIZE, NULL, LED_OUT_PRIORITY, &xbuttonLEDs);
	xTaskCreate(load_user_mgmnt, "load_user_mgmnt", TASK_STACKSIZE, NULL, LOAD_USER_MNGMNT_PRIORITY, &xLoadMgnmntTask);
	xTaskCreate(frequency_calculator, "frequency_calculator", TASK_STACKSIZE, NULL, FREQUENCY_CALCULATOR_PRIORITY, &xFreqTask);


	return 0;
}
