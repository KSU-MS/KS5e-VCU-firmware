// library includes
#include <Arduino.h>
#include <FlexCAN_T4.h>
#include <Metro.h>
#include <string.h>
#include <stdint.h>
#include <FreqMeasureMulti.h>

// our includes
#include "MCU_status.hpp"
#include "KS2eCAN.hpp"
#include "KS2eVCUgpios.hpp"
#include "FlexCAN_util.hpp"
#include "parameters.hpp"
#include "inverter.hpp"
#include "accumulator.hpp"
#include "state_machine.hpp"
#include "FlexCAN_util.hpp"

static can_obj_ksu_dbc_h_t ksu_can;
// Metro timers for inverter:
Metro timer_mc_kick_timer = Metro(50, 1);
Metro timer_inverter_enable = Metro(2000, 1); // Timeout failed inverter enable
Metro timer_motor_controller_send = Metro(100, 1);
Metro timer_current_limit_send = Metro(500,1);

// timers for the accumulator:
Metro pchgMsgTimer = Metro(1000, 0);
// Metro pchgTimeout = Metro(500);

// timers for the pedals:

Metro timer_debug_pedals_raw = Metro(100, 1);
Metro pedal_out = Metro(50, 1);
Metro pedal_check = Metro(40, 1);

// timers for the dashboard:
Metro pm100speedInspection = Metro(500, 1);

// timers for the state machine:
Metro timer_ready_sound = Metro(1000); // Time to play RTD sound
Metro debug_tim = Metro(200, 1);

// PID shit
volatile double current_rpm, set_rpm, throttle_out;
double KP = D_KP;
double KI = D_KI;
double KD = D_KD;
double OUTPUT_MIN = D_OUTPUT_MIN;
double OUTPUT_MAX = D_OUTPUT_MAX;


AutoPID speedPID(&current_rpm, &set_rpm, &throttle_out, OUTPUT_MIN, OUTPUT_MAX, KP, KI, KD);

// timers for VCU state out:
Metro timer_can_update = Metro(1000, 1);

// Wheel speed shit
FreqMeasureMulti wsfl;
FreqMeasureMulti wsfr;

// objects
Dashboard dash;
Inverter pm100(&ksu_can,&timer_mc_kick_timer, &timer_inverter_enable, &timer_motor_controller_send, &timer_current_limit_send);
Accumulator accum(&pchgMsgTimer, &ksu_can);
PedalHandler pedals(&timer_debug_pedals_raw, &pedal_out, &speedPID, &current_rpm, &set_rpm, &throttle_out, &wsfl, &wsfr);
StateMachine state_machine(&pm100, &accum, &timer_ready_sound, &dash, &debug_tim, &pedals, &pedal_check);
MCU_status mcu_status = MCU_status();

static CAN_message_t mcu_status_msg;
static CAN_message_t fw_hash_msg;

//----------------------------------------------------------------------------------------------------------------------------------------

void setup()
{
    Serial.begin(57600);
    delay(100);

    InitCAN();

    mcu_status.set_max_torque(0); // no torque on startup
    mcu_status.set_torque_mode(0);
    // build up fw git hash message
    fw_hash_msg.id = ID_VCU_FW_VERSION; fw_hash_msg.len=8;
    Serial.printf("FW git hash: %lu",AUTO_VERSION);
    // long git_hash = strtol(AUTO_VERSION,0,16);
    unsigned long git_hash = AUTO_VERSION;
    memcpy(&fw_hash_msg.buf[0],&git_hash,sizeof(git_hash));

    pinMode(BUZZER, OUTPUT); // TODO write gpio initialization function
    digitalWrite(BUZZER, LOW);
    pinMode(LOWSIDE1, OUTPUT);
    pinMode(LOWSIDE2, OUTPUT);
    pinMode(WSFL, INPUT_PULLUP);
    pinMode(WSFR, INPUT_PULLUP);
    mcu_status.set_inverter_powered(true); // note VCU does not control inverter power on rev3
    mcu_status.set_torque_mode(1); //TODO torque modes should be an enum
    mcu_status.set_max_torque(torque_1);   // TORQUE_1=60nm, 2=120nm, 3=180nm, 4=240nm
    state_machine.init_state_machine(mcu_status);
}

void loop()
{
    state_machine.handle_state_machine(mcu_status);

    if (timer_can_update.check())
    {
        // Send Main Control Unit status message
        mcu_status.write(mcu_status_msg.buf);
        mcu_status_msg.id = ID_VCU_STATUS;
        mcu_status_msg.len = sizeof(mcu_status);
        WriteCANToInverter(mcu_status_msg);

        //broadcast firmware git hash
        WriteCANToInverter(fw_hash_msg);
        Serial.println("Can object: ");
        char stringg[50];
        FILE *test;
        (print_message(&ksu_can,CAN_ID_MSGID_0X6B1,test));
        while (fgets(stringg, 50, test) != NULL) {
            Serial.printf("%s", stringg);
        }
    }
}

