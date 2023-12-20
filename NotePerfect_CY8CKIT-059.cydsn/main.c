/******************************************************************************
* File Name: Main.c
*
* Version 1.1
*
* Description:
* This file contains the main function for the voltage Display test.
*
* Note:
*
* Code tested with:
* PSoC Creator: 3.0
* Device Tested With: CY8C5868AXI-LP035
* Compiler    : ARMGCC 4.4.1, ARM RVDS Generic, ARM MDK Generic
*
********************************************************************************
* Copyright (2013), Cypress Semiconductor Corporation. All Rights Reserved.
********************************************************************************
* This software is owned by Cypress Semiconductor Corporation (Cypress)
* and is protected by and subject to worldwide patent protection (United
* States and foreign), United States copyright laws and international treaty
* provisions. Cypress hereby grants to licensee a personal, non-exclusive,
* non-transferable license to copy, use, modify, create derivative works of,
* and compile the Cypress Source Code and derivative works for the sole
* purpose of creating custom software in support of licensee product to be
* used only in conjunction with a Cypress integrated circuit as specified in
* the applicable agreement. Any reproduction, modification, translation,
* compilation, or representation of this software except as specified above 
* is prohibited without the express written permission of Cypress.
*
* Disclaimer: CYPRESS MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH 
* REGARD TO THIS MATERIAL, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
* Cypress reserves the right to make changes without further notice to the 
* materials described herein. Cypress does not assume any liability arising out 
* of the application or use of any product or circuit described herein. Cypress 
* does not authorize its products for use as critical components in life-support 
* systems where a malfunction or failure may reasonably be expected to result in 
* significant injury to the user. The inclusion of Cypress' product in a life-
* support systems application implies that the manufacturer assumes all risk of 
* such use and in doing so indemnifies Cypress against all charges. 
*
* Use of this Software may be limited by and subject to the applicable Cypress
* software license agreement. 
*******************************************************************************/

/******************************************************************************
*                           THEORY OF OPERATION
* This project demonstrates how ADC is used to read the input voltage at 
* it's input and display it on the LCD.
* 
* The Potentiometer is connected to the input of the DelSig ADC. ADC is 
* configured with 20 bit of resolution to measure the input voltage with 
* higher accuracy. Moving average filter of 128 samples is applied to the ADC
* conversion result before displaying the result in micro volts on the LCD. 
*
* Hardware connection on the Kit
* Potentiometer - PORT 6[5] 
* LCD - PORT 2[0..6]
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
    
    uint8_t previousButton0 = OFF, previousButton1 = OFF, previousButton2 = OFF;
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
            
            if(CapSense_CheckIsWidgetActive(CapSense_BUTTON2__BTN))
            {
                if(previousButton2 == OFF)
                {
                    previousButton2 = ON;
                }
            }
            else
            {
                previousButton2 = OFF;
            }
                
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

