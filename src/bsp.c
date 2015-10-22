/*****************************************************************************
* Model: icbm.qm
* File:  ./bsp.c
*
* This code has been generated by QM tool (see state-machine.com/qm).
* DO NOT EDIT THIS FILE MANUALLY. All your changes will be lost.
*
* This program is open source software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published
* by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*****************************************************************************/
/*${.::bsp.c} ..............................................................*/
/* Allegro A4960 BLDC test fixture board support package */
#include "qpn_port.h"
#include "bsp.h"
#include "icbm.h"
#include <libpic30.h>
//rev0 notes
//1.console on UART1
//SPI1
//CS on pin 25

/* PIC24FV16KM202-specific */

/* MCU configuration bits                      */
/* external 8 MHz crystal w/PLL, 32MHz clock   */
#pragma config BWRP=OFF, BSS=OFF, GWRP=OFF, GCP=OFF
#pragma config FNOSC = PRIPLL, SOSCSRC = DIG, LPRCSEL = LP, IESO = ON
#pragma config POSCMOD = HS, OSCIOFNC = CLKO, POSCFREQ = MS, FCKSM = CSECME
#pragma config FWDTEN = OFF, WINDIS = OFF
#pragma config BOREN=BOR0, RETCFG=OFF, PWRTEN = OFF, MCLRE=ON
#pragma config ICS = PGx2

/* Local-scope objects -----------------------------------------------------*/
                             /* frequency of the oscillator for PIC24 */
#define FOSC_HZ                 32000000.0
                                       /* instruction cycle clock frequency */
#define FCY_HZ                  (FOSC_HZ / 2.0)

                                /* controlling the LED */
#define LED_ON()                //(LATA |= (1U << 0))
#define LED_OFF()               //(LATA &= ~(1U << 0))
#define LED_TOGGLE()            //(LATA ^= (1U << 0))

/* Peripherals                                -----------------------------*/

#define MISO_TRIS _TRISA7
#define TACHO_TRIS _TRISB9
#define DIAG_TRIS _TRISB3
#define RBUT_TRIS _TRISA0
#define RBUT_PIN _RA0
#define BBUT_TRIS _TRISA1
#define BBUT_PIN _RA1

/* High Endurance EEPROM        ------------------------------------------*/
#define EE_SIZE 256U                 //x16 bit
#define HEE_PAGES 4U                  //more than 2 to detect
                                     //failed pack
#define HEE_PAGESIZE EE_SIZE/HEE_PAGES

#define EE_ROW_SIZE 8U               //8 words erase max
#define EE_ROWS 8U

#define EE_ERASE_8    0x1a
#define EE_ERASE_4    0x19
#define EE_ERASE_1    0x18
#define EE_ERASE_ALL  0x10
#define EE_WRITE   0x04

//EEPROM Write queue size - must be power of 2
#define HEEQ_BUFSIZE 64
#define HEEQ_BUFMASK ( HEEQ_BUFSIZE - 1 )

#if ( HEEQ_BUFSIZE & HEEQ_BUFMASK )
#error EEPROM Write queue size is not a power of 2
#endif

typedef union
{
    uint8_t val;
    struct
    {
        unsigned addrNotFound:1;        // Return 0xFFFF
        unsigned expiredPage:1;               // Return 0x1
        unsigned packBeforePageFull:1;    // Not a return condition
        unsigned packBeforeInit:1;        // Return 0x3
        unsigned packSkipped:1;            // Return 0x4
        unsigned IllegalAddress:1;        // Return 0x5
        unsigned pageCorrupt:1;            // Return 0x6
        unsigned writeError:1;            // Return 0x7
    };
} HEE_FLAGS;

typedef struct __attribute__((__packed__)) heep_t {    //page status
    union {
        uint16_t val;
        struct {
            unsigned count:13;
            unsigned expired:1;
            unsigned available:1;
            unsigned current:1;
        };
    };
} HEE_PAGE_STATUS;

typedef struct heeq_t {    //EEPROM modification request
    uint16_t data;
    _prog_addressT addr;
    uint8_t op;
} HEE_REQ;

//todo: check if zeroing it at programming is certain
__eds__ const uint16_t heep[4][64] __attribute__((space(eedata), aligned(2)));


//EEPROM WR Queue
HEE_REQ heeq[HEEQ_BUFSIZE];
uint8_t heeq_Head;
volatile uint8_t heeq_Tail;    //interrupt changes this

/* A4960 */

#define A4960_CS_PIN _RB15


#define TIMER1_ISR_PRIO         4

//- Const

//Enable-disable message
const char const zen[] = "\x08\x08\x08 0 - Enable, 1- Disable";

//Common units

const char const us[] = "us";    //microseconds
const char const ms[] = "ms";    //milliseconds
const char const pct[] = "%";    //percent

//- Static

static uint8_t tach_ro;    //rollover count for Tacho

/* ISRs --------------------------------------------------------------------*/
/*${BSP::ISR::_AddressError} ...............................................*/
void __attribute__((__interrupt__,__no_auto_psv__)) _AddressError(void) {
    while(1);    //Address error
}
/*${BSP::ISR::_StackError} .................................................*/
void __attribute__((__interrupt__,__no_auto_psv__)) _StackError(void) {
    while(1);
}
/*${BSP::ISR::_T1Interrupt} ................................................*/
void __attribute__((__interrupt__, auto_psv)) _T1Interrupt(void) {
    //typedef struct but_t {    //button debouncing


    static uint32_t btn_debounced  = 0U;
    static uint8_t  debounce_state = 0U;

    //static uint32_t rbut_debounced  = 0U;
    //static uint8_t  rbut_debounce_state = 0U;

    //static uint32_t bbut_debounced  = 0U;
    //static uint8_t  bbut_debounce_state = 0U;

    uint8_t btn = RBUT_PIN;          /* read the push button state */
        switch (debounce_state) {
            case 0:
                if (btn != btn_debounced) {
                    debounce_state = 1;         /* transition to the next state */
                }
                break;
            case 1:
                if (btn != btn_debounced) {
                    debounce_state = 2;         /* transition to the next state */
                }
                else {
                    debounce_state = 0;           /* transition back to state 0 */
                }
                break;
            case 2:
                if (btn != btn_debounced) {
                    debounce_state = 3;         /* transition to the next state */
                }
                else {
                    debounce_state = 0;           /* transition back to state 0 */
                }
                break;
            case 3:
                if (btn != btn_debounced) {
                    btn_debounced = btn;     /* save the debounced button value */

                    if (btn == 0) {                 /* is the button depressed? */
                        QACTIVE_POST_X_ISR((QActive *)&AO_Console, 1,
                            RBUT_PRESS_SIG, 0);
                    }
                    else {
                        QACTIVE_POST_X_ISR((QActive *)&AO_Console, 1,
                            RBUT_RELEASE_SIG, 0);
                    }

                }
                debounce_state = 0;               /* transition back to state 0 */
                break;
        }

    _T1IF = 0;                              /* clear Timer 1 interrupt flag */

    QACTIVE_POST_X_ISR((QActive*)&AO_Console, 1,
        TIME_TICK_SIG, 0U);

    QF_tickISR();                /* handle all armed time events in QF-nano */
}

/*${BSP::Tacho::_CCT2Interrupt} ............................................*/
void __attribute__((__interrupt__, auto_psv)) _CCT2Interrupt(void) {
    tach_ro++;
    _CCT2IF = 0;
}
/*${BSP::Tacho::_CCP2Interrupt} ............................................*/
void __attribute__((__interrupt__, auto_psv)) _CCP2Interrupt(void) {
    static uint16_t oldval = 0;
    uint16_t tmpdata = 0;
    uint16_t tmp2 = 0;

    while(CCP2STATLbits.ICBNE) {    //data present
        tmpdata = CCP2BUFL;
    }

    tmp2 = tmpdata;
    tmpdata += oldval;
    oldval = ~tmp2;

    //tmpdata |= (uint32_t)tach_ro << 16;

    QACTIVE_POST_X_ISR ((QActive*)&AO_Console, 1,
    TACHO_SIG, tmpdata);

    //tach_ro = 0;

    _CCP2IF = 0;
}

/*${BSP::HEE::_NVMInterrupt} ...............................................*/
void __attribute__((__interrupt__, auto_psv)) _NVMInterrupt(void) {
    if( heeq_Head != heeq_Tail ) {    //data available
                heeq_Tail++;
    #if HEEQ_BUFMASK < 255
                heeq_Tail &= HEEQ_BUFMASK;
    #endif

    // __ builtin_software_breakpoint();

        HEE_REQ req = heeq[heeq_Tail];

    //_prog_addressT p = req.addr;

        NVMCONbits.NVMOP = req.addr;
    //    TBLPAG = __builtin_tblpage(p);
    //    uint16_t offset = __builtin_tbloffset(&p);

        if(req.op == EE_WRITE) {
            // Write Data Value To Holding Latch
    //        __builtin_tblwtl(offset, req.data);
        }
        else {
    //        __builtin_tblwtl(offset, offset);
        }
        // Disable Interrupts For 5 Instructions
        asm volatile ("disi #5");
        // Issue Unlock Sequence & Start Write Cycle
        __builtin_write_NVM();
    }
    else { //queue empty
        _NVMIE = 0;
    }

    _NVMIF = 0;
}

/*${BSP::A4960::Common::SPI_init} ..........................................*/
static void SPI_init(void) {
    SSP2STATbits.CKE = 1;
    SSP2CON1bits.SSPM = 0b1010;
    SSP2ADD = 0x0f;    //1 MHz SPI clock
    SSP2CON1bits.SSPEN = 1; //enable SPI
    MISO_TRIS = 1;    //set MISO line to input
}
/*${BSP::A4960::Common::A4960_xfer} ........................................*/
static uint16_t A4960_xfer(uint8_t reg, uint16_t data) {
    A4960_CS_PIN = 0;

    uint16_t tmpdata;

    SSP2BUF = ((reg << 4) | (data >> 8));    //send upper byte
    while(SSP2STATbits.BF == 0);    //wait till end of xfer

    tmpdata = SSP2BUF;

    if(tmpdata & 0x80) {    //A4960 fault
        QACTIVE_POST_X((QActive*)&AO_Console, 1, FF_SIG,0U);
    };

    SSP2BUF = data & 0xff;        //send lower byte

    tmpdata = tmpdata << 8;
    while(SSP2STATbits.BF == 0);    //wait till end of xfer

    tmpdata = (tmpdata | SSP2BUF);

    A4960_CS_PIN = 1;

    /* Look out for events */

    if(SSP2CON1bits.WCOL == 1) {    //write collision
        SSP2CON1bits.WCOL = 0;        //clear
        QACTIVE_POST_X((QActive*)&AO_Console, 1, WCOL_SIG,0U);
    }

    return tmpdata;
}
/*${BSP::A4960::Common::A4960_getField} ....................................*/
static uint16_t A4960_getField(ITEM* item) {
    uint16_t tmpdata = A4960_xfer(item->reg,0U);
    uint16_t tmpmask = item->mask;

    tmpdata &= tmpmask;    //clear the rest

    while((tmpmask & 0x01) == 0) {    //LSB equals 0
        tmpmask >>= 1;
        tmpdata >>= 1;
    };

    return(tmpdata);
}
/*${BSP::A4960::Common::A4960_setField} ....................................*/
static uint16_t A4960_setField(uint16_t val, ITEM* item) {
    uint16_t tmpdata = A4960_xfer(item->reg,0U);
    uint16_t tmpmask = item->mask;
    uint16_t tmpval = val;

    tmpdata &= ~tmpmask; //clear the field position

    while((tmpmask & 0x1) == 0) { //LSB equals 0
        tmpmask >>= 1;
        tmpval <<= 1;
    }

    tmpdata |= tmpval;            //insert field

    return(A4960_xfer(item->reg + 1, tmpdata));//write
}
/*${BSP::A4960::Common::getField} ..........................................*/
uint16_t getField(FIELD* field) {
    uint16_t tmpdata = A4960_xfer(field->reg,0U);
    uint16_t tmpmask = field->mask;

    tmpdata &= tmpmask;    //clear the rest

    while((tmpmask & 0x01) == 0) {    //LSB equals 0
        tmpmask >>= 1;
        tmpdata >>= 1;
    };

    return(tmpdata);
}
/*${BSP::A4960::Common::setField} ..........................................*/
uint16_t setField(uint16_t val, FIELD* field) {
    uint16_t tmpdata = A4960_xfer(field->reg,0U);
    uint16_t tmpmask = field->mask;
    uint16_t tmpval = val;

    tmpdata &= ~tmpmask; //clear the field position

    while((tmpmask & 0x1) == 0) { //LSB equals 0
        tmpmask >>= 1;
        tmpval <<= 1;
    }

    tmpdata |= tmpval;            //insert field

    return(A4960_xfer(field->reg + 1, tmpdata));//write
}

/*${BSP::A4960::Common::A4960_convPercen~} .................................*/
static uint16_t A4960_convPercent(ITEM* item) {
    return((A4960_getField(item) + 1)*625U);
}
/*${BSP::A4960::Common::convPercent} .......................................*/
static uint16_t convPercent(FIELD* field) {
    return((getField(field) + 1)*625U);
}
//- Limits
/*${BSP::A4960::Limits::ConvCommBlankTim~} .................................*/
static uint16_t ConvCommBlankTime(FIELD* field) {
    const uint16_t const blanktimes[] = {50U,100U,400U,1000U};

    uint8_t tmpdata = getField(field);

    return(blanktimes[tmpdata]);
}
/*${BSP::A4960::Limits::ConvBlankTime} .....................................*/
static uint16_t ConvBlankTime(FIELD* field) {
    return(400U*getField(field));
}
/*${BSP::A4960::Limits::ConvDeadTime} ......................................*/
static uint16_t ConvDeadTime(FIELD* field) {
    uint16_t tmpdata = getField(field);

    return((tmpdata < 3) ? 100U : tmpdata*50U);
}
//- Common ConvPercent used for Curr.Sense Ratio
/*${BSP::A4960::Limits::ConvVdsThreshold} ..................................*/
static uint16_t ConvVdsThreshold(FIELD* field) {
    return(25U*getField(field));
}

/* Item access structure. Layout as follows:

name - what is printed in the menu
unit - what is printed after the value
point - decimal point
reg - register containing the item
mask - bitmask of an item inside the register
get - function to get contents -->redundant?
set - function to set contents -->same as above
conv - function to convert contents to value
*/

FIELD const CommBlankTime =
    {"Commutation Blank Time", us, 0U, A4960_CONF0_RD, 0x0c00,
        &ConvCommBlankTime};

FIELD const BlankTime =
    {"Blank Time",us, 3U, A4960_CONF0_RD, 0x03c0,
        &ConvBlankTime};

FIELD const DeadTime =
    {"Dead Time","ns", 0U, A4960_CONF0_RD, 0x003f,
        &ConvDeadTime};

FIELD const CurrentSenseRefRatio =
    {"Current Sense",pct, 2U, A4960_CONF1_RD, 0x03c0,
        &convPercent};

FIELD const VdsThreshold =
    {"VDS Threshold","mV", 0U, A4960_CONF1_RD, 0x003f,
        &ConvVdsThreshold};

//- Run
/*${BSP::A4960::Run::A4960_ConvFixedO~} ....................................*/
static uint16_t A4960_ConvFixedOffTime(ITEM* item) {
    return(10+(A4960_getField(item)*16U));
}
/*${BSP::A4960::Run::A4960_ConvPhaseA~} ....................................*/
static uint16_t A4960_ConvPhaseAdvance(ITEM* item) {
    return(A4960_getField(item)*1875U); //deg(e), DS p.28
}
//BemfHyst - get
/*${BSP::A4960::Run::A4960_ConvBemfWi~} ....................................*/
static uint16_t A4960_ConvBemfWindow(ITEM* item) {
    return(0x04 << A4960_getField(item)); //us DS p.29
}
//Brake - get
//Direction - get
//Run - get


ITEM const A4960_FixedOffTime =
    {"Fixed Off Time", "us", 1U, A4960_CONF2_RD, 0x001f,
        &A4960_getField, &A4960_setField, &A4960_ConvFixedOffTime};

ITEM const A4960_PhaseAdvance =
    {"Phase Advance","deg(e)", 3U, A4960_CONF5_RD, 0x0c00,
        &A4960_getField, &A4960_setField, &A4960_ConvPhaseAdvance};

ITEM const A4960_BemfHyst =
    {"BEMF Hysteresis","0 - Auto, 1 - None, 2 - High, 3 - Low", 0U, A4960_RUN_RD, 0x0c00,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_BemfWindow =
    {"BEMF Window","us", 0U, A4960_RUN_RD, 0x0380,
        &A4960_getField, &A4960_setField, &A4960_ConvBemfWindow};

ITEM const A4960_Brake =
    {"Brake","0 - Off, 1 - On", 0U, A4960_RUN_RD, 0x0004,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_Direction =
    {"Direction","0 - Fwd, 1 - Rev", 0U, A4960_RUN_RD, 0x0002,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_Run =
    {"Run","0 - Coast, 1 - Run", 0U, A4960_RUN_RD, 0x0001,
        &A4960_getField, &A4960_setField, &A4960_getField};





#ifdef UNDEF

ITEM const A4960_CommBlankTime =
    {"Comm. Blank Time", zen/*"ns"*/, 0U, A4960_CONF0_RD, 0x0c00,
        &A4960_getField, &A4960_setField, &A4960_ConvCommBlankTime};

ITEM const A4960_BlankTime =
    {"Blank Time","us", 3U, A4960_CONF0_RD, 0x03c0,
        &A4960_getField, &A4960_setField, &A4960_ConvBlankTime};

ITEM const A4960_DeadTime =
    {"Dead Time","ns", 0U, A4960_CONF0_RD, 0x003f,
        &A4960_getField, &A4960_setField, &A4960_ConvDeadTime};

ITEM const A4960_CurrentSenseRefRatio =
    {"Current Sense","%", 2U, A4960_CONF1_RD, 0x03c0,
        &A4960_getField, &A4960_setField, &A4960_convPercent};

ITEM const A4960_VdsThreshold =
    {"VDS Threshold","mV", 0U, A4960_CONF1_RD, 0x003f,
        &A4960_getField, &A4960_setField, &A4960_ConvVdsThreshold};



/* ----------------------------- */

//- Run
/*${BSP::A4960::Run::A4960_ConvFixedO~} ....................................*/
static uint16_t A4960_ConvFixedOffTime(ITEM* item) {
    return(10+(A4960_getField(item)*16U));
}
/*${BSP::A4960::Run::A4960_ConvPhaseA~} ....................................*/
static uint16_t A4960_ConvPhaseAdvance(ITEM* item) {
    return(A4960_getField(item)*1875U); //deg(e), DS p.28
}
//BemfHyst - get
/*${BSP::A4960::Run::A4960_ConvBemfWi~} ....................................*/
static uint16_t A4960_ConvBemfWindow(ITEM* item) {
    return(0x04 << A4960_getField(item)); //us DS p.29
}
//Brake - get
//Direction - get
//Run - get


ITEM const A4960_FixedOffTime =
    {"Fixed Off Time", "us", 1U, A4960_CONF2_RD, 0x001f,
        &A4960_getField, &A4960_setField, &A4960_ConvFixedOffTime};

ITEM const A4960_PhaseAdvance =
    {"Phase Advance","deg(e)", 3U, A4960_CONF5_RD, 0x0c00,
        &A4960_getField, &A4960_setField, &A4960_ConvPhaseAdvance};

ITEM const A4960_BemfHyst =
    {"BEMF Hysteresis","0 - Auto, 1 - None, 2 - High, 3 - Low", 0U, A4960_RUN_RD, 0x0c00,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_BemfWindow =
    {"BEMF Window","us", 0U, A4960_RUN_RD, 0x0380,
        &A4960_getField, &A4960_setField, &A4960_ConvBemfWindow};

ITEM const A4960_Brake =
    {"Brake","0 - Off, 1 - On", 0U, A4960_RUN_RD, 0x0004,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_Direction =
    {"Direction","0 - Fwd, 1 - Rev", 0U, A4960_RUN_RD, 0x0002,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_Run =
    {"Run","0 - Coast, 1 - Run", 0U, A4960_RUN_RD, 0x0001,
        &A4960_getField, &A4960_setField, &A4960_getField};


#endif

//HoldTorque - percent
/*${BSP::A4960::Startup::A4960_ConvHoldTi~} ................................*/
static uint16_t A4960_ConvHoldTime(ITEM* item) {
    return(A4960_getField(item)*8 + 2); //ms DS p.27
}
/*${BSP::A4960::Startup::A4960_ConvEndCom~} ................................*/
static uint16_t A4960_ConvEndCommTime(ITEM* item) {
    return((A4960_getField(item) + 1)*2U); //ms, DS p.28
}
/*${BSP::A4960::Startup::A4960_ConvStartC~} ................................*/
static uint16_t A4960_ConvStartCommTime(ITEM* item) {
    return((A4960_getField(item) + 1)*8U); //ms, DS p.28
}
//ForcedCommTorque - A_ConvPercent
/*${BSP::A4960::Startup::A4960_ConvRampRa~} ................................*/
static uint16_t A4960_ConvRampRate(ITEM* item) {
    return((A4960_getField(item) + 1)*2U); //ms, DS p.28
}

ITEM const A4960_HoldTorque =
    {"Hold Torque", "%", 2U, A4960_CONF3_RD, 0x00f0,
        &A4960_getField, &A4960_setField, &A4960_convPercent};

ITEM const A4960_HoldTime =
    {"Hold Time", "ms", 0U, A4960_CONF3_RD, 0x000f,
        &A4960_getField, &A4960_setField, &A4960_ConvHoldTime};

ITEM const A4960_EndCommTime =
    {"End Comm. Time", "ms", 0U, A4960_CONF4_RD, 0x00f0,
        &A4960_getField, &A4960_setField, &A4960_ConvEndCommTime};

ITEM const A4960_StartCommTime =
    {"Start Comm. Time", "ms", 0U, A4960_CONF4_RD, 0x000f,
        &A4960_getField, &A4960_setField, &A4960_ConvStartCommTime};

ITEM const A4960_ForcedCommTorque =
    {"Forced Comm. Ramp-up Torque", "%", 2U, A4960_CONF5_RD, 0x00f0,
        &A4960_getField, &A4960_setField, &A4960_convPercent};

ITEM const A4960_RampRate =
    {"Ramp Rate", "ms", 1U, A4960_CONF5_RD, 0x000f,
        &A4960_getField, &A4960_setField, &A4960_ConvRampRate};

//Flags - all generic

ITEM const A4960_VaFlag =
    {"Bootcap A Fault", zen, 0U, A4960_MASK_RD, 0x0100,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_VbFlag =
    {"Bootcap B Fault", zen, 0U, A4960_MASK_RD, 0x0080,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_VcFlag =
    {"Bootcap C Fault", zen, 0U, A4960_MASK_RD, 0x0040,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_AhFlag =
    {"Phase A High-Side Fault", zen, 0U, A4960_MASK_RD, 0x0020,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_AlFlag =
    {"Phase A Low-Side Fault", zen, 0U, A4960_MASK_RD, 0x0010,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_BhFlag =
    {"Phase B High-Side Fault", zen, 0U, A4960_MASK_RD, 0x0008,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_BlFlag =
    {"Phase B Low-Side Fault", zen, 0U, A4960_MASK_RD, 0x0004,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_ChFlag =
    {"Phase C High-Side Fault", zen, 0U, A4960_MASK_RD, 0x0002,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_ClFlag =
    {"Phase C Low-Side Fault", zen, 0U, A4960_MASK_RD, 0x0001,
        &A4960_getField, &A4960_setField, &A4960_getField};

//- Misc all generic

ITEM const A4960_TorqueCtlMethod =
    {"Torque Control Method", "0 - Current, 1 - Duty Cycle", 0U, A4960_CONF3_RD, 0x0100,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_EnableStopOnFail =
    {"Stop on Fail","0 - Dis, 1- En", 0U, A4960_RUN_RD, 0x0040,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_DiagOutput =
    {"Diag Output","0 - Flt, 1 - LOS, 2 -VDS Thr, 3 - clock", 0U, A4960_RUN_RD, 0x0030,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_RestartControl =
    {"Restart Control","0 - Dis, 1 - En", 0U, A4960_RUN_RD, 0x0080,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_TwFlag =
    {"Temperature Warning Fault", zen, 0U, A4960_MASK_RD, 0x0800,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_TsFlag =
    {"Thermal Shutdown Fault", zen, 0U, A4960_MASK_RD, 0x0400,
        &A4960_getField, &A4960_setField, &A4960_getField};

ITEM const A4960_LosFlag =
    {"BEMF Sync. Loss Fault", zen, 0U, A4960_MASK_RD, 0x0200,
        &A4960_getField, &A4960_setField, &A4960_getField};


//- PWM access

//One of the 8 pre-determined frequencies

static const uint16_t pwmPeriod[] = { 64000U, 32000U, 16000U,
    8000U, 3200U, 1600U, 800U, 320U };

static const uint16_t pwmFreq[] = { 250U, 500U, 1000U,
    2000U, 5000U, 10000U, 20000U, 50000U };

/*${BSP::PWM::PWM_init} ....................................................*/
static void PWM_init(void) {
    CCP1CON1Lbits.CCSEL = 0;    //mode
    CCP1CON1Lbits.MOD = 0b0101;

    CCP1CON1Lbits.TMR32 = 0;    //timebase
    CCP1CON1Lbits.TMRSYNC = 0;
    CCP1CON1Lbits.CLKSEL = 0b000;
    CCP1CON1Lbits.TMRPS = 0b00;
    CCP1CON1Hbits.TRIGEN = 0;
    CCP1CON1Hbits.SYNC = 0b00000;

    CCP1CON2Hbits.OCBEN = 1;    //enable PWM on the output
    CCP1CON3Hbits.OUTM = 0b000;
    CCP1CON3Hbits.POLBDF = 0;    //active high
    CCP1TMRL = 0x0000;
    CCP1PRL = 3200U;
    CCP1RA = 0x000;
    CCP1RB = 0x001ff;
    CCP1CON1Lbits.CCPON = 1;
}
/*${BSP::PWM::PWM_idxLookup} ...............................................*/
static uint8_t PWM_idxLookup(const uint16_t* array, uint16_t val) {
    uint8_t i = 0;

    while( i < 8 ) {
        if(array[i] == val) {
            return(i);
        }
        i++;
    }

    return(0xff); //not found
}
/*${BSP::PWM::PWM_getPeriodIdx} ............................................*/
static uint16_t PWM_getPeriodIdx(ITEM* item) {
    return(PWM_idxLookup(pwmPeriod,CCP1PRL));
}

/*${BSP::PWM::PWM_setPeriod} ...............................................*/
static uint16_t PWM_setPeriod(uint16_t val, ITEM* item) {
    return(CCP1PRL = pwmPeriod[val]);
}
/*${BSP::PWM::PWM_convFreq} ................................................*/
static uint16_t PWM_convFreq(ITEM* item) {
    return(pwmFreq[PWM_getPeriodIdx(item)]);
}
/*${BSP::PWM::PWM_getDuty} .................................................*/
static uint16_t PWM_getDuty(ITEM* item) {
    return(0);
}
/*${BSP::PWM::PWM_setDuty} .................................................*/
static uint16_t PWM_setDuty(uint16_t val, ITEM* item) {
    return(0);
}

ITEM const PWM_Freq =
    {"PWM Frequency", "kHz", 3U, 0U, 0x07,
        &PWM_getPeriodIdx, &PWM_setPeriod, &PWM_convFreq};

ITEM const PWM_Duty =
    {"PWM Duty Cycle", "%", 0U, 0U, 0x00,
        &PWM_getDuty, &PWM_setDuty, &PWM_getDuty};


/*${BSP::HEE::HEE_init} ....................................................*/
static void HEE_init(void) {
    NVMCONbits.PGMONLY = 1;
    _NVMIP = 1;    //priority
}

/*${BSP::Tacho::Tacho_init} ................................................*/
static void Tacho_init(void) {
    CCP2CON1Lbits.CCPON = 0;   //disable
    CCP2CON1Lbits.CCSEL = 1;   //input capture mode
    CCP2CON1Lbits.CLKSEL = 0;  //sysclk
    CCP2CON1Lbits.TMR32 = 0;   //32-bit mode
    CCP2CON1Lbits.MOD = 1;     //rising every rising edge
    CCP2CON2Hbits.ICSEL = 0;   //rising edge
    CCP1CON1Hbits.IOPS = 0;    //interrupt on every event
    CCP1CON1Lbits.TMRPS = 0;   //prescaler
    CCP2CON1Lbits.CCPON = 1;   //enable

    _CCT2IP = 1;    //tacho timer interrupt priority
                    //must be lower than tacho capture
    _CCP2IP = 6;    //tacho capture interrupt priority
}
/*${BSP::BSP_init} .........................................................*/
void BSP_init(void) {
    RCONbits.SWDTEN = 0;                                /* disable Watchdog */

    TRISA = 0x00;                                /* set LED pins as outputs */
    PORTA = 0x00;                               /* set LEDs drive state low */
    TRISB = 0x00;
    PORTB = 0x00;
    ANSA = 0x00;
    ANSB = 0x00;

    TACHO_TRIS = 1;    //tachometer input
    DIAG_TRIS = 1;     //diag pin input
    RBUT_TRIS = 1;    //RUN button
    BBUT_TRIS = 1;    //BRAKE button



    SPI_init();
    PWM_init();
    Tacho_init();
    HEE_init();
}

#ifdef UNDEF
/*--------------------------------------------------------------------------*/
void BSP_init(void) {
    RCONbits.SWDTEN = 0;                                /* disable Watchdog */

    TRISA = 0x00;                                /* set LED pins as outputs */
    PORTA = 0x00;                               /* set LEDs drive state low */
    TRISB = 0x00;
    PORTB = 0x00;
    ANSA = 0x00;
    ANSB = 0x00;

    TACHO_TRIS = 1;    //tachometer input
    DIAG_TRIS = 1;     //diag pin input

    SPI_init();
    PWM_init();
    Tacho_init();
    HEE_init();
}

#endif

/*${BSP::QPn::QF_onStartup} ................................................*/
void QF_onStartup(void) {
    T1CON = 0x0000U;  /* Use Internal Osc (Fcy), 16 bit mode, prescaler = 1 */
    TMR1  = 0x0000U; /* Start counting from 0 and clear the prescaler count */
    PR1   = (uint16_t)((FCY_HZ / BSP_TICKS_PER_SEC) - 1.0 + 0.5); /* period */
    _T1IP = TIMER1_ISR_PRIO;              /* set Timer 2 interrupt priority */
    _T1IF = 0;                           /* clear the interrupt for Timer 1 */
    _T1IE = 1;                              /* enable interrupt for Timer 1 */
    T1CONbits.TON = 1;                                     /* start Timer 1 */

    /* Enable peripheral interrupts as late as possible */
    _CCT2IE = 1;    //tacho timer
    _CCP2IE = 1;    //tacho capture
    _U1RXIE = 1;                                     /* Console on UART1 Rx */
    _NVMIE = 1;            //EEPROM
}

/*****************************************************************************
* NOTE01:
* The callback function QF_onIdle() is called with interrupts disabled,
* because the idle condition can be invalidated by any enabled interrupt
* that would post events. The QF_onIdle() function *must* enable interrupts
* internally
*
* NOTE02:
* To be on the safe side, the DISICNT counter is set to just 1 cycle just
* before entering the Idle mode (or Sleep mode, if you choose). This way,
* interrupts (with priorities 1-6) get enabled at the same time as the
* transition to the low-power mode.
*/
/*${BSP::QPn::QF_onIdle} ...................................................*/
void QF_onIdle(void) {
    //LED_ON ();
    //LED_OFF();

    #ifdef NDEBUG
        __asm__ volatile("disi #0x0001");
        Idle();                          /* transition to Idle mode, see NOTE02 */
    #else
        QF_INT_ENABLE();                       /* enable interrupts, see NOTE01 */
    #endif
}
/*${BSP::QPn::Q_onAssert} ..................................................*/
void Q_onAssert(char const Q_ROM * const Q_ROM_VAR file, int line) {
    (void)file;                                   /* avoid compiler warning */
    (void)line;                                   /* avoid compiler warning */
    QF_INT_DISABLE();             /* make sure that interrupts are disabled */
    for (;;) {
    }
}

