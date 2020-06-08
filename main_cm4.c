/**
* @file main_cm4.c
*
* @brief The main UI loop caller
*
* Hardware Dependency: CY8CKIT-028-TFT TFT Display Shield
*					   CY8CKIT-062-BLE PSoC6 BLE Pioneer Kit
*
* @version 0.2
* @author Enrico Miglino <balearicdynamics@gmai.com>
* @date May, 30 2020
*
 *******************************************************************************/

//#include "project.h"
#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cycfg_capsense.h"
#include "GUI.h"
#include "stdlib.h"
#include "bitmaps.h"
#include "noise_level.h"

/*******************************************************************************
* Page numbers corresponding to a specific screen view
*******************************************************************************/
#define PAGE_LOGO 0			///< Super Smart Home Logo
#define PAGE_NOISE 1		///< Sound noise level gauge
#define PAGE_LIGHT 2		///< Environment light level
#define PAGE_AWS 3			///< AWS IoT Console events log
#define PAGE_CENTER 4		///< Control Center events log
#define NUMBER_OF_PAGES 5	///< Total number of pages managed

/*******************************************************************************
* Macros
*******************************************************************************/
#define CSD_COMM_HW             (SCB3)
#define CSD_COMM_IRQ            (scb_3_interrupt_IRQn)
#define CSD_COMM_PCLK           (PCLK_SCB3_CLOCK)
#define CSD_COMM_CLK_DIV_HW     (CY_SYSCLK_DIV_8_BIT)
#define CSD_COMM_CLK_DIV_NUM    (1U)
#define CSD_COMM_CLK_DIV_VAL    (3U)
#define CSD_COMM_SCL_PORT       (GPIO_PRT6)
#define CSD_COMM_SCL_PIN        (0u)
#define CSD_COMM_SDA_PORT       (GPIO_PRT6)
#define CSD_COMM_SDA_PIN        (1u)
#define CSD_COMM_SCL_HSIOM_SEL  (P6_0_SCB3_I2C_SCL)
#define CSD_COMM_SDA_HSIOM_SEL  (P6_1_SCB3_I2C_SDA)
#define CAPSENSE_INTR_PRIORITY  (7u)

//! EZI2C interrupt priority must be higher than CapSense interrupt.
//! Lower number mean higher priority.
#define EZI2C_INTR_PRIORITY     (6u)

//! Flag of the microphone status
volatile bool pdm_pcm_flag = true;
//! Last read volume level
uint32_t volume = 0;

/** HAL Configuration parameters
*/
const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg =
{
    .sample_rate     = SAMPLE_RATE_HZ,
    .decimation_rate = DECIMATION_RATE,
    .mode            = CYHAL_PDM_PCM_MODE_STEREO,
    .word_length     = 16,  /* bits */
    .left_gain       = 0,   /* dB */
    .right_gain      = 0,   /* dB */
};

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static cy_status initialize_capsense(void);
static int process_touch(void);
static void initialize_capsense_tuner(void);
static void capsense_isr(void);
void ShowAWS(void);
void ControlCenterLog(void);
void ShowBitmap(void);
void clock_init(void);
void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event);

/*******************************************************************************
* Global Variables
*******************************************************************************/
cy_stc_scb_ezi2c_context_t ezi2c_context;

/**
 * ShowStartupScreen(void) displays the startup screen.
 * In our case, it is the same of the main logo page but can be changed in the future
*/

void ShowStartupScreen(void)
{
    GUI_Clear();

    GUI_DrawBitmap(&bmPSoC6Image, 0, 0);
}

// Show the AWS IoT MQTT Connection status
// Not in use
void ShowAWS(void)
{
    /* Set font size, background color and text mode */
    GUI_SetFont(GUI_FONT_16B_1);
    GUI_SetBkColor(GUI_BLACK);
    GUI_SetColor(GUI_GRAY);
    GUI_SetTextMode(GUI_TM_NORMAL);

    GUI_Clear();

    GUI_SetTextStyle(GUI_TS_NORMAL);
    GUI_SetBkColor(GUI_BLACK);
    GUI_SetColor(GUI_GRAY);
    GUI_SetTextMode(GUI_TM_NORMAL);
    GUI_SetFont(GUI_FONT_20_1);

    /* Display page title */
    GUI_DispStringAt("AWS MQTT Status", 50, 10);

}


// Show the Control Center log stream
// Not in use
void ControlCenterLog(void)
{
    /* Set font size, background color and text mode */
    GUI_SetFont(GUI_FONT_16B_1);
    GUI_SetBkColor(GUI_BLACK);
    GUI_SetColor(GUI_GRAY);
    GUI_SetTextMode(GUI_TM_NORMAL);

    GUI_Clear();

    GUI_SetTextStyle(GUI_TS_NORMAL);
    GUI_SetBkColor(GUI_BLACK);
    GUI_SetColor(GUI_GRAY);
    GUI_SetTextMode(GUI_TM_NORMAL);
    GUI_SetFont(GUI_FONT_20_1);

    /* Display page title */
    GUI_DispStringAt("Control Center Log", 50, 10);

}

/**
* ShowBitmap(void) displays a bitmap image with an overlay text
*/
void ShowBitmap(void)
{
    /* Set background color to black and clear screen */
    GUI_SetBkColor(GUI_BLACK);
    GUI_Clear();

    /* Display the bitmap image on the screen */
    GUI_DrawBitmap(&bmPSoC6Image, 0, 4);
}

/**
 * Main application function
 *
 * As this application is for consultation only, the UI is limited to
 * scroll the different information pages with the two capsense buttons.
 */
void mainTFT(void)
{
    int pageNumber = 0;		///< The screen current page number
    int oldPageNumber = 0;	///< The screen previous page number
    int drawVolume = 0;		///< The volume level (mic) mapped to the gauge

    cy_rslt_t result;	///< Capsense buttons control

    int16_t  audio_frame[FRAME_SIZE] = {0};


    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    __enable_irq(); /* Enable global interrupts. */

    // Init the clocks
    clock_init();

    initialize_capsense_tuner();
    cy_status status = initialize_capsense();

    // Initialize the PDM/PCM block
    cyhal_pdm_pcm_init(&pdm_pcm, PDM_DATA, PDM_CLK, &audio_clock, &pdm_pcm_cfg);
    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_pcm_isr_handler, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm, CYHAL_PDM_PCM_ASYNC_COMPLETE, CYHAL_ISR_PRIORITY_DEFAULT, true);
    cyhal_pdm_pcm_start(&pdm_pcm);

    // Start first scan
    Cy_CapSense_ScanAllWidgets(&cy_capsense_context);
    // Initialize EmWin Graphics
    GUI_Init();
    // Display the startup screen
    ShowStartupScreen();

    // Call the corresponding pages function depending on the user capsense
    // button pressed
    for(;;) {
    	// Executes the page change only if the user has pressed the button
        if(CY_CAPSENSE_NOT_BUSY == Cy_CapSense_IsBusy(&cy_capsense_context)) {
            // Process all widgets
            Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

            // Update the functions pointer array (if no button has been pressed,
            // no increment happens).
            pageNumber += process_touch();

            // Establishes synchronized operation between the CapSense
            // middleware and the CapSense Tuner tool.
            Cy_CapSense_RunTuner(&cy_capsense_context);

            // Start next scan
            Cy_CapSense_ScanAllWidgets(&cy_capsense_context);

            // If page number has changed and the previous page was showing
            // the sound noise we should release the memory used by the widget
            if( (oldPageNumber != pageNumber) && (oldPageNumber == PAGE_NOISE) ) {
                // Delete GUI_AUTODEV-object and free memeory
                GUI_MEMDEV_DeleteAuto(&AutoDev);
            } // Clean memory from noise level widget

            // Check for the correspondence between the page number and the
			// button pressed.
			if(pageNumber >= NUMBER_OF_PAGES) {
				pageNumber = 0;
				oldPageNumber = pageNumber;	// Saves the last page change
			} // Reset the page number to the first page
			else if(pageNumber < 0) {
				pageNumber = NUMBER_OF_PAGES - 1;
				oldPageNumber = pageNumber;	// Saves the last page change
			} // Reset the page number to the last page

            // Show the page corresponding to the selection
			// If the page number has changed in the range
			if (pageNumber != oldPageNumber) {
				oldPageNumber = pageNumber;	// Saves the last page change
				// Launch the function corresponding to the current selected page
				// Note that this selection is only for creating the UI of the corresponding
				// page. Then the on-screen values are updated until the user has not changed
				// the current page.
	            switch(pageNumber) {
	            	case PAGE_LOGO:
	            		ShowBitmap();
	            		break;
	            	case PAGE_NOISE:
	            		// Try to allocate memory do traw the widget
	            		// and draw the UI widget screen
						if (GUI_ALLOC_GetNumFreeBytes() < RECOMMENDED_MEMORY) {
						  GUI_ErrorOut("Not enough memory available.");
						} else {
							NoiseLevelInitGUI();	// Setup the screen
							DrawScale(drawVolume);	// Draw the scale
						}
	            		break;
//	            	case PAGE_LIGHT:
//	            		break;
//	            	case PAGE_AWS:
//	            		break;
//	            	case PAGE_CENTER:
//	            		break;

	            } // Switch page number
			} // Page number has changed
        } // Capsense button is pressed
		// Fixed as there is only one object
		if(pageNumber == PAGE_NOISE) {
	        /* Check if any microphone has data to process */
	        if (pdm_pcm_flag)
	        {
	            /* Clear the PDM/PCM flag */
	            pdm_pcm_flag = 0;

	            /* Reset the volume */
	            volume = 0;

	            /* Calculate the volume by summing the absolute value of all the
	             * audio data from a frame */
	            for (uint32_t index = 0; index < FRAME_SIZE; index++)
	            {

	                volume += abs(audio_frame[index]);
	            }

				drawVolume = (int)(volume / 10);	// Normalize the volume for representation
				if(drawVolume > ABSOLUTE_MAX_NOISE) {
					drawVolume = ABSOLUTE_MAX_NOISE;
				}

	        }

	        DrawScale(drawVolume);
			/* Setup to read the next frame */
			cyhal_pdm_pcm_read_async(&pdm_pcm, audio_frame, FRAME_SIZE);

        } // Showing noise gauge
    } // Main program loop
}

/**
 * Process the touch sense process managed by the capsense buttons.
 * Here we don't use the slider of the board
 *
 * @return The button touch status
 */
int process_touch(void)
{
    uint32_t button0_status;
    uint32_t button1_status;

    static uint32_t button0_status_prev;
    static uint32_t button1_status_prev;

    /* Get button 0 status */
    button0_status = Cy_CapSense_IsSensorActive(
                                CY_CAPSENSE_BUTTON0_WDGT_ID,
                                CY_CAPSENSE_BUTTON0_SNS0_ID,
                                &cy_capsense_context);

    /* Get button 1 status */
    button1_status = Cy_CapSense_IsSensorActive(
                                CY_CAPSENSE_BUTTON1_WDGT_ID,
                                CY_CAPSENSE_BUTTON0_SNS0_ID,
                                &cy_capsense_context);

    /* Detect new touch on Button0 */
    if((0u != button0_status) &&
       (0u == button0_status_prev))
    {
        /* Update previous touch status */
        button0_status_prev = button0_status;
        button1_status_prev = button1_status;
    	return -1;
    }

    /* Detect new touch on Button1 */
    if((0u != button1_status) &&
       (0u == button1_status_prev))
    {
        /* Update previous touch status */
        button0_status_prev = button0_status;
        button1_status_prev = button1_status;
    	return 1;
    }

    /* Update previous touch status */
    button0_status_prev = button0_status;
    button1_status_prev = button1_status;
    return 0;
}

/**
* initialize_capsense initializes the CapSense and configure the CapSense
*  interrupt.
*/
static cy_status initialize_capsense(void)
{
    cy_status status;

    /* CapSense interrupt configuration */
    const cy_stc_sysint_t CapSense_interrupt_config =
    {
        .intrSrc = CYBSP_CSD_IRQ,
        .intrPriority = CAPSENSE_INTR_PRIORITY,
    };

    /* Capture the CSD HW block and initialize it to the default state. */
    status = Cy_CapSense_Init(&cy_capsense_context);

    if(CYRET_SUCCESS == status)
    {
        /* Initialize CapSense interrupt */
        Cy_SysInt_Init(&CapSense_interrupt_config, capsense_isr);
        NVIC_ClearPendingIRQ(CapSense_interrupt_config.intrSrc);
        NVIC_EnableIRQ(CapSense_interrupt_config.intrSrc);

        /* Initialize the CapSense firmware modules. */
        status = Cy_CapSense_Enable(&cy_capsense_context);
    }

    return status;
}

/**
* capsense_isr is the wrapper function for handling interrupts from CapSense block.
*/
static void capsense_isr(void)
{
    Cy_CapSense_InterruptHandler(CYBSP_CSD_HW, &cy_capsense_context);
}

/**
* ezi2c_isr is a wrapper function for handling interrupts from EZI2C block.
*/
static void ezi2c_isr(void)
{
    Cy_SCB_EZI2C_Interrupt(CSD_COMM_HW, &ezi2c_context);
}


/**
* initialize_capsense_tuner initializes interface between Tuner GUI and PSoC 6 MCU.
*/
static void initialize_capsense_tuner(void)
{
    /* EZI2C configuration structure */
    const cy_stc_scb_ezi2c_config_t csd_comm_config =
    {
        .numberOfAddresses = CY_SCB_EZI2C_ONE_ADDRESS,
        .slaveAddress1 = 8U,
        .slaveAddress2 = 0U,
        .subAddressSize = CY_SCB_EZI2C_SUB_ADDR16_BITS,
        .enableWakeFromSleep = false,
    };

    /* EZI2C interrupt configuration structure */
    static const cy_stc_sysint_t ezi2c_intr_config =
    {
        .intrSrc = CSD_COMM_IRQ,
        .intrPriority = EZI2C_INTR_PRIORITY,
    };

    /* Initialize EZI2C pins */
    Cy_GPIO_Pin_FastInit(CSD_COMM_SCL_PORT, CSD_COMM_SCL_PIN,
                         CY_GPIO_DM_OD_DRIVESLOW, 1, CSD_COMM_SCL_HSIOM_SEL);
    Cy_GPIO_Pin_FastInit(CSD_COMM_SDA_PORT, CSD_COMM_SDA_PIN,
                         CY_GPIO_DM_OD_DRIVESLOW, 1, CSD_COMM_SDA_HSIOM_SEL);

    /* Configure EZI2C clock */
    Cy_SysClk_PeriphDisableDivider(CSD_COMM_CLK_DIV_HW, CSD_COMM_CLK_DIV_NUM);
    Cy_SysClk_PeriphAssignDivider(CSD_COMM_PCLK, CSD_COMM_CLK_DIV_HW,
                                  CSD_COMM_CLK_DIV_NUM);
    Cy_SysClk_PeriphSetDivider(CSD_COMM_CLK_DIV_HW, CSD_COMM_CLK_DIV_NUM,
                               CSD_COMM_CLK_DIV_VAL);
    Cy_SysClk_PeriphEnableDivider(CSD_COMM_CLK_DIV_HW, CSD_COMM_CLK_DIV_NUM);


    /* Initialize EZI2C */
    Cy_SCB_EZI2C_Init(CSD_COMM_HW, &csd_comm_config, &ezi2c_context);

    /* Initialize and enable EZI2C interrupts */
    Cy_SysInt_Init(&ezi2c_intr_config, ezi2c_isr);
    NVIC_EnableIRQ(ezi2c_intr_config.intrSrc);

    /* Set up communication data buffer to CapSense data structure to be exposed
     * to I2C master at primary slave address request.
     */
    Cy_SCB_EZI2C_SetBuffer1(CSD_COMM_HW, (uint8 *)&cy_capsense_tuner,
                            sizeof(cy_capsense_tuner), sizeof(cy_capsense_tuner),
                            &ezi2c_context);

    /* Enable EZI2C block */
    Cy_SCB_EZI2C_Enable(CSD_COMM_HW);
}

/**
* pdm_pcm_isr_handler set a flag to be processed in the main loop detecting if
* there are data on the microphone
*
* @param arg not used
* @param event event that occurred
*/
void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event)
{
    (void) arg;
    (void) event;

    pdm_pcm_flag = true;
}

/**
* clock_init initialize the clocks in the system.
*/
void clock_init(void)
{
    /* Initialize the PLL */
    cyhal_clock_get(&pll_clock, &CYHAL_CLOCK_PLL[0]);
    cyhal_clock_init(&pll_clock);
    cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);

    /* Initialize the audio subsystem clock (HFCLK1) */
    cyhal_clock_get(&audio_clock, &CYHAL_CLOCK_HF[1]);
    cyhal_clock_init(&audio_clock);
    cyhal_clock_set_source(&audio_clock, &pll_clock);
    cyhal_clock_set_enabled(&audio_clock, true, true);
}

/* [] END OF FILE */
