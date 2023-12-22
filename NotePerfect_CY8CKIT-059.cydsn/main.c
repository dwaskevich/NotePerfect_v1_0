/******************************************************************************
* File Name: main.c
*
* Version 1.0
*
* Description:
* This file contains the main function for the NotePerfect control voltage
* quantizer. 
*
* Code tested with PSoC Creator: 4.3
* Device tested with CY8C5888LTI-LP097 (CY8CKIT-059 Prototyping Kit)
* Compiler: ARMGCC 5.4-2016-q2-update
*
*******************************************************************************/

/******************************************************************************
*                           THEORY OF OPERATION
*
* This project takes an analog Control Voltage input, digitizes it with a
* 20-bit delta-sigma ADC and then outputs the closest 1/12th note CV via
* RC-filtered PWM. Filtered PWM output is buffered with an internal opamp.
*
* LCD is managed with a community-supplied component (from MEH ... Mark
* Hastings) to limit pin count and enable non-adjacent GPIOs:
* https://community.infineon.com/t5/PSoC-Creator-Designer/Character-LCD-mp-Multi-Port/td-p/242544
* LCD contrast is controlled via VDAC+opamp connected to P0.1.
*
* This project is based on the PSoC 5LP code example for the DelSigADC:
* C:\Program Files (x86)\Cypress\PSoC 5LP Development Kit\1.0\Firmware\VoltageDisplay_DelSigADC
* The DelSigADC is configured for 20-bit resolution to measure the input
* voltage with high accuracy. A moving-average filter of 128 samples is
* applied to the ADC conversion before using the result in NotePerfect
* calculations and displaying the result (in micro volts) on the LCD.
*
* NotePerfect output voltages are determined by calculating the nearest
* "step" (0V - 5V) and then using the step number as an index into a lookup 
* table for PWM compare values. Full-scale voltage, number of notes per
* volt and correction window are determined by #defines at the top of
* main.c.
* 
* There are two input channels (In_A and In_B) seleced via Analog Mux
* using front-panel CapSense touch buttons:
* 1. In_A -> 1/8" TS jack on front panel
* 2. In_B -> 3-pin .1" DuPont female header on side (for potentiometer)
*  -> Note - potentiometer input is primarily for testing and calibration.
*
* Quantized output voltage is available from 1/8" TS jack on front panel.
*
* A tri-color LED indicates what's happening:
*  - Red indicates In_A is active. Intensity corresponds to incoming
*    voltage level.
*  - Green indicates In_B is active. Intensity corresponds to incoming
*    voltage level.
*  - Blue indicates a correction is being applied to the incoming voltage.
*
* Three CapSense buttons on the front panel control/select which input
* is active (In_A or In_B), Misc button is for future use.
* 
* 
*******************************************************************************/
#include <device.h>
#include "stdio.h"
#include "stdlib.h"

#define IN_B (0u)
#define IN_A  (1u)

/* Number of samples to be taken before averaging the ADC value */
#define MAX_SAMPLE                  ((uint8)128)

/* Threshold value to reset the filter for sharp change in signal */
#define SIGNAL_SLOPE                1000

/* Number of shifts for calculating the sum and average of MAX_SAMPLE */
#define DIV                         7

/* Number of "NotePerfect" control voltages per volt */
#define NUMBER_NOTES_PER_VOLT       (12u)

/* Maximum control voltage */
#define MAX_CONTROL_VOLTAGE         (5u)

/* Total number of "NotePerfect" quantized steps/values (60) */
#define NUMBER_NOTE_PERFECT_STEPS (NUMBER_NOTES_PER_VOLT * MAX_CONTROL_VOLTAGE)

/* NotePerfect step size (in mV) */
#define NOTEPERFECT_STEP_SIZE_MV    ((MAX_CONTROL_VOLTAGE * 1000) / NUMBER_NOTE_PERFECT_STEPS)

/* Correction window (in mV) */
#define CORRECTION_WINDOW           (NOTEPERFECT_STEP_SIZE_MV / 4)

/* Define constants for capsense buttons */
#define ON           (1)
#define OFF          (0)

#define LED_OFF     (0u)
#define LED_ON      (1u)

/* control register masks */
#define BLUE_CTRL   (0x01)
#define RED_CTRL    (0x02)
#define GREEN_CTRL  (0x04)

/* NotePerfect step number lookup table */
uint16_t PWM_Lookup[] = {0, 83, 167, 250, 333, 417, 500, 583, 667, 750, 833, 917, 1000,
                            1083, 1167, 1250, 1333, 1417, 1500, 1583, 1667, 1750, 1833, 1917, 2000,
                            2083, 2167, 2250, 2333, 2417, 2500, 2583, 2667, 2750, 2833, 2917, 3000,
                            3083, 3167, 3250, 3333, 3417, 3500, 3583, 3667, 3750, 3833, 3917, 4000,
                            4083, 4167, 4250, 4333, 4417, 4500, 4583, 4667, 4750, 4833, 4917, 5000};

int main(void)
{
    uint8 i;
    
    /* Array to store ADC count for moving average filter */
    int32 adcCounts[MAX_SAMPLE] = {0};
    
    /* Variable to hold ADC conversion result */
    int32 result = 0;
    
    /* Variable to store accumulated sample for filter array */
    int32 sum = 0;
    
    /* Variable for testing sharp change in signal slope */
    int16 diff = 0;
    
    /* Variable to hold the result in milli volts converted from filtered 
     * ADC counts */
    int32 milliVolts = 0;
	
    /* Variable to hold the moving average filtered value */
    int32 averageCounts = 0;
	
    /* Index variable to work on the filter array */
    uint8 index = 0;
    
    /* Variables to calculate and hold NotePerfect DAC value */
    uint32 notePerfectValue = 0;
    uint32 previousNotePerfectValue = 0;
    uint32 remainder = 0;
    
    /* Character array to hold the micro volts*/
    char displayStr[15] = {'\0'};
    
    uint8_t previousButton0 = OFF, previousButton1 = OFF;
//    uint8_t previousButton2 = OFF;
    uint8_t inputChannel = IN_A;

    CYGlobalIntEnable;
    
    VDAC8_Start();
    Opamp_1_Start();
    PWM_Red_Start(); /* start IN_A PWM */
    
    /* Start capsense and initialize baselines and enable scan */
    CapSense_Start();
    CapSense_InitializeAllBaselines();

    /* Start ADC and start conversion */
    ADC_Start();
    ADC_StartConvert();

    /* Start LCD and set position */
    LCD_Start();
    LCD_Position(0,0);
    LCD_PrintString("In A     Step=");

    /* Print mV unit on the LCD */
    LCD_Position(1,4);
    LCD_PutChar('m');
    LCD_PutChar('V');
    LCD_Position(1,8);
    LCD_PrintString("DAC=");
    
    /* Read one sample from the ADC and initialize the filter */
    ADC_IsEndConversion(ADC_WAIT_FOR_RESULT);
    result = ADC_GetResult32();
    
    for(i = 0; i < MAX_SAMPLE; i++)
    {
        adcCounts[i] = result;
    }
    
    /* Store sum of 128 samples*/
    sum = result << DIV;
    
    /* Average count is equal to one single sample for first ADC reading */
    averageCounts = result;
    
    /* start Opamp and PWM */
    Opamp_Start();
    PWM_Start();
    
    /* initialize indicator LEDs */
    LED3_Write(LED_ON);
    
    /* start and initialize Analog input Mux to IN_A as the source */
    myMux_Start();
    myMux_FastSelect(IN_A);

    while(1)
    {
        /* user interface stuff ... switch between input sources (IN_A vs IN_B input) */
        if(CapSense_IsBusy() == 0)
        {
            /* Update baseline for all the sensors */
            CapSense_UpdateEnabledBaselines();
            
            /* Test if button widgets are active */
            if(CapSense_CheckIsWidgetActive(CapSense_BUTTON0__BTN))
            {
                if(previousButton0 == OFF)
                {
                    myMux_FastSelect(IN_A); /* select IN_A as input source */
                    inputChannel = IN_A;
                    /* start Red LED pwm and stop Green */
                    PWM_Red_Start();
                    Control_Reg_Write((Control_Reg_Read() | GREEN_CTRL));
                    PWM_Green_Stop();
                    Control_Reg_Write((Control_Reg_Read() & ~GREEN_CTRL));
                    /* update LCD */
                    LCD_Position(0,0);
                    LCD_PrintString("In A");

                    previousButton0 = ON;
                }
            }
            else
            {
                previousButton0 = OFF;
            }
            
            if(CapSense_CheckIsWidgetActive(CapSense_BUTTON1__BTN))
            {
                if(previousButton1 == OFF)
                {
                    myMux_FastSelect(IN_B); /* select IN_B as input source */
                    inputChannel = IN_B;
                    /* start Green LED pwm and stop Red */
                    PWM_Green_Start();
                    Control_Reg_Write((Control_Reg_Read() | RED_CTRL));
                    PWM_Red_Stop();
                    Control_Reg_Write((Control_Reg_Read() & ~RED_CTRL));
                    /* update LCD */
                    LCD_Position(0,0);
                    LCD_PrintString("In B");
                    
                    previousButton1 = ON;
                }
            }
            else
            {
                previousButton1 = OFF;
            }
            
            /* future use ... for demo only */
            if(CapSense_CheckIsWidgetActive(CapSense_BUTTON2__BTN))
            {
                PWM_Blue_WriteCompare(5000); /* arbitrary action for demo purpose */
                PWM_Blue_Start();
                PWM_Red_WriteCompare(0);
                PWM_Green_WriteCompare(0);
            }
//                
//                if(previousButton2 == OFF)
//                {
//                    previousButton2 = ON;
//                }
//            }
//            else
//            {
//                previousButton2 = OFF;
//            }
                
            CapSense_ScanEnabledWidgets();
        }
        
        /* start the ADC conversion and wait for the result */
        ADC_IsEndConversion(ADC_WAIT_FOR_RESULT);
        result = ADC_GetResult32();
        
        diff = abs(averageCounts - result); /* calculate instantaneous difference to determine if abrupt change has happened */

        /* If sharp change in the signal then reset the filter with the new signal value */
        if(diff > SIGNAL_SLOPE)
        {
            /* fill the filter array with current value */
            for(i = 0; i < MAX_SAMPLE; i++)
            {
                adcCounts[i] = result;
            }
            
            /* Store sum of 128 samples*/
            sum = result << DIV;
    
            /* Average count is equal to new sample */
            averageCounts = result;
            index = 0;
        }
        
        /* Get moving average */
        else
        {
            /* Remove the oldest element and add new sample to sum and get the average */
            sum = sum - adcCounts[index];
            sum = sum + result;
            averageCounts = sum >> DIV;
            
            /* Remove the oldest sample and store new sample */
            adcCounts[index] = result;
            index++;
            if (index == MAX_SAMPLE)
            {
                index = 0;
            }
        }
        
        /* convert ADC counts to milliVolts */
        milliVolts = ADC_CountsTo_mVolts(averageCounts);
        
        /* NotePerfect magic happens here */
        notePerfectValue = (milliVolts / NOTEPERFECT_STEP_SIZE_MV); /* calculate integer step number */
        remainder = milliVolts % NOTEPERFECT_STEP_SIZE_MV; /* use modulo to get remainder */
        if(remainder >= NOTEPERFECT_STEP_SIZE_MV/2) /* determine if round-up is necessary */
            notePerfectValue += 1;
        
        if(notePerfectValue != previousNotePerfectValue) /* only spend time if notePerfect step value has changed */
        {
            PWM_WriteCompare(PWM_Lookup[notePerfectValue]); /* lookup PWM compare value and update PWM */
            
            /* front-panel LED housekeeping */
            if(IN_A == inputChannel)
            {
                if(0 == notePerfectValue) /* don't let indicator LED go all the way off (i.e. PWM to zero) */
                    PWM_Red_WriteCompare(PWM_Lookup[1]);
                else
                    PWM_Red_WriteCompare(PWM_Lookup[notePerfectValue]); /* LED brightness follows input voltage */
            }
            else if(IN_B == inputChannel)
            {
                if(0 == notePerfectValue) /* don't let indicator LED go all the way off (i.e. PWM to zero) */
                    PWM_Green_WriteCompare(PWM_Lookup[1]);
                else
                    PWM_Green_WriteCompare(PWM_Lookup[notePerfectValue]); /* LED brightness follows input voltage */
            }
            
            /* display housekeeping */
            sprintf(displayStr, "%4d", PWM_Lookup[notePerfectValue]);
            LCD_Position(1,12);
            LCD_PrintString(displayStr);   
            
            /* Convert notePerfectValue to string and display on the LCD */
            sprintf(displayStr, "%2ld", notePerfectValue);
            LCD_Position(0,14);
            LCD_PrintString(displayStr);
            
            previousNotePerfectValue = notePerfectValue;
        }
            
        /* Convert milli volts to string and display on the LCD */
        sprintf(displayStr, "%4ld", milliVolts);
        LCD_Position(1,0);
        LCD_PrintString(displayStr);
        
        /* determine if correction was applied */
        if((uint32_t) milliVolts > PWM_Lookup[notePerfectValue] + CORRECTION_WINDOW || //
                            (uint32_t) milliVolts < PWM_Lookup[notePerfectValue] - CORRECTION_WINDOW)
        {
            if(notePerfectValue > 10) /* limit Blue LED brightness to step 10 value (arbitrary) */
                PWM_Blue_WriteCompare(PWM_Lookup[10]);
            else PWM_Blue_WriteCompare(PWM_Lookup[notePerfectValue]); /* Blue LED brightness follows input voltage */
            PWM_Blue_Start(); /* Blue LED indicates correction has been applied */
        }
        else
        {
            /* Fixed-function PWMs hold value when stopped, so reset first then stop */
            Control_Reg_Write((Control_Reg_Read() | BLUE_CTRL));
            PWM_Blue_Stop();
            Control_Reg_Write((Control_Reg_Read() & ~BLUE_CTRL));
        }
    }
}

/* [] END OF FILE */

