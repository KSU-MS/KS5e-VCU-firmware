#include "pedal_handler.hpp"
#include "state_machine.hpp"
Metro debugPrint = Metro(1000);
Metro deb = Metro(10);

// initializes pedal's ADC
void PedalHandler::init_pedal_handler()
{
    // TODO don't htink you actually have to init analog GPIOs on teensy
    // for(int i; i < sizeof(analog_init_list)/sizeof(int);i++){
    //     pinMode(analog_init_list[i],INPUT);
    // }
    pedal_ADC = ADC_SPI(DEFAULT_SPI_CS, DEFAULT_SPI_SPEED);
    pid_->setTimeStep(PID_TIMESTEP);
    wsfl_->begin(WSFL);
    wsfr_->begin(WSFR);
    pid_->setBangBang(double(BANGBANG_RANGE));
}

int PedalHandler::calculate_torque(int16_t &motor_speed, int &max_torque, bool regen_button)
{
    int calculated_torque = 0;
    
    int torque1 = map(round(accel1_), START_ACCELERATOR_PEDAL_1,
                      END_ACCELERATOR_PEDAL_1, 0, max_torque);
    int torque2 = map(round(accel2_), START_ACCELERATOR_PEDAL_2,
                      END_ACCELERATOR_PEDAL_2, 0, max_torque);
    torque1=torque2; //TODO un-cheese (apps1 borked)
    // torque values are greater than the max possible value, set them to max
    if (torque1 > max_torque)
    {
        torque1 = max_torque;
    }
    if (torque2 > max_torque)
    {
        torque2 = max_torque;
    }
    // compare torques to check for accelerator implausibility
    
    calculated_torque = (torque1 + torque2) / 2; //TODO un-cheese this

    if (calculated_torque > max_torque)
    {
        calculated_torque = max_torque;
    }
    if (calculated_torque < 0)
    {
        calculated_torque = 0;
    }

    // TODO actual regen mapping and not on/off, this was jerky on dyno
    bool off_brake = (VCUPedalReadings.get_brake_transducer_1() <= 1850);
    bool off_gas =  (torque1 <= 5);

    // Serial.print("button: ");
    // Serial.println(regen_button);
    // Serial.print("brake: ");
    // Serial.println(off_brake);
    // Serial.print("gas: ");
    // Serial.println(off_gas);
    if(off_gas && off_brake && regen_button)
    {
        // regen_command = ;
        // 40191 = -10nm
        // 10101 = -100nm
        uint16_t calculated_torque2 = (uint16_t)(-(REGEN_NM*10));
        calculated_torque = static_cast<int>(calculated_torque2);
        // calculated_torque = 40191;
        Serial.print("regen torque: ");
        Serial.println(calculated_torque);
        // torquePart1=0x9C;
        // torquePart2=0xFf; //-10nm sussy regen
    }
    Serial.println(calculated_torque);
    return calculated_torque;
}

// returns true if the brake pedal is active
bool PedalHandler::read_pedal_values()
{
    /* Filter ADC readings */

    accel1_ =
        ALPHA * accel1_ + (1 - ALPHA) * pedal_ADC.read_adc(ADC_ACCEL_1_CHANNEL);
    accel2_ =
        ALPHA * accel2_ + (1 - ALPHA) * pedal_ADC.read_adc(ADC_ACCEL_2_CHANNEL);
    brake1_ =
        ALPHA * brake1_ + (1 - ALPHA) * pedal_ADC.read_adc(ADC_BRAKE_1_CHANNEL);
    
    steering_angle_ =
        ALPHA * steering_angle_ + (1 - ALPHA) * pedal_ADC.read_adc(ADC_HALL_CHANNEL);

    if(debugPrint.check()){
        #ifdef DEBUG
        Serial.print("ADC1 :");
        Serial.println(accel1_);
        Serial.print("ADC2 :");
        Serial.println(accel2_);
        Serial.print("BRAKE :");
        Serial.println(brake1_);
        Serial.print("STEERING_ANGLE :");
        Serial.println(steering_angle_);        
        #endif
    }

    VCUPedalReadings.set_accelerator_pedal_1(accel1_);
    VCUPedalReadings.set_accelerator_pedal_2(accel2_);
    VCUPedalReadings.set_brake_transducer_1(brake1_);
    VCUPedalReadings.set_brake_transducer_2(steering_angle_);
    // only uses front brake pedal
    brake_is_active_ = (brake1_ >= BRAKE_ACTIVE);
    return brake_is_active_;
}

void PedalHandler::send_readings(){
    if (pedal_out->check())
    {
        // Send Main Control Unit pedal reading message
        CAN_message_t tx_msg;
        VCUPedalReadings.write(tx_msg.buf);
        tx_msg.id = ID_VCU_PEDAL_READINGS;
        tx_msg.len = sizeof(VCUPedalReadings);

        // Send wheel speed readings
        CAN_message_t tx_msg2;
        tx_msg2.id = ID_VCU_WS_READINGS;
        tx_msg2.len = 8;
        uint32_t rpm_wsfl = (int)(current_rpm * 100);
        uint32_t rpm_wsfr = (int)(current_rpm2 * 100);
        memcpy(&tx_msg2.buf[0], &rpm_wsfl, sizeof(rpm_wsfl));
        memcpy(&tx_msg2.buf[4], &rpm_wsfr, sizeof(rpm_wsfr));

        // Send miscellaneous board analog readings
        CAN_message_t tx_msg3;
        tx_msg3.id = ID_VCU_BOARD_ANALOG_READS_ONE;
        memcpy(&tx_msg3.buf[0],&glv_current_,sizeof(glv_current_));
        memcpy(&tx_msg3.buf[2],&glv_voltage_,sizeof(glv_voltage_));
        memcpy(&tx_msg3.buf[4],&bspd_voltage_,sizeof(bspd_voltage_));
        memcpy(&tx_msg3.buf[6],&vcc_voltage_,sizeof(vcc_voltage_));

        CAN_message_t tx_msg4;
        tx_msg4.id = ID_VCU_BOARD_ANALOG_READS_TWO;
        memcpy(&tx_msg4.buf[0],&sdc_voltage_,sizeof(sdc_voltage_));
        memcpy(&tx_msg4.buf[2],&sdc_current_,sizeof(sdc_current_));
        memcpy(&tx_msg4.buf[4],&analog_input_nine_voltage_,sizeof(analog_input_nine_voltage_));
        memcpy(&tx_msg4.buf[6],&analog_input_ten_voltage_,sizeof(analog_input_ten_voltage_));


        WriteCANToInverter(tx_msg);
        WriteCANToInverter(tx_msg2);
        WriteCANToInverter(tx_msg3);
        WriteCANToInverter(tx_msg4);
    }
}

void PedalHandler::verify_pedals(
    bool &accel_is_plausible, bool &brake_is_plausible,
    bool &accel_and_brake_plausible, bool &impl_occ)
{
    int max_torque = TORQUE_1 * 10;
    // int torque1 = map(round(accel1_), START_ACCELERATOR_PEDAL_1,
    //                   END_ACCELERATOR_PEDAL_1, 0, max_torque);
    int torque2 = map(round(accel2_), START_ACCELERATOR_PEDAL_2,
                      END_ACCELERATOR_PEDAL_2, 0, max_torque);
    int torque1 = torque2; //TODO un-cheese (apps1 borked)
    float torqSum = abs(torque1 - torque2);
    float torqAvg = (torque1 + torque2) / 2;
    float torqDiff = torqSum / torqAvg;

    if (accel1_ < MIN_ACCELERATOR_PEDAL_1 ||
        accel1_ > MAX_ACCELERATOR_PEDAL_1)
    {
        accel_is_plausible = false;
    }
    else if (accel2_ < MIN_ACCELERATOR_PEDAL_2 ||
             accel2_ > MAX_ACCELERATOR_PEDAL_2)
    {
        accel_is_plausible = false;
    }
    // check that the pedals are reading within 10% of each other TODO re-enabled 6/10/23, why was it commented out in the first place? how did we fix it before??
    // sum of the two readings should be within 10% of the average travel
    // T.4.2.4
    else if (torqDiff*100 > 50)
    {
        accel_is_plausible = false;
    }
    else
    {
        // mcu_status.set_no_accel_implausability(true);
        accel_is_plausible = true;
    }
    Serial.printf("Torque 1: %d Torque 2: %d Torque Sum: %f Torque Average %f Torque Difference: %f\n",torque1,torque2,torqSum,torqAvg,torqDiff);

    // BSE check
    // EV.5.6
    // FSAE T.4.3.4
    if (brake1_ < 200 || brake1_ > 4000)
    {
        brake_is_plausible = false;
    }
    else
    {
        brake_is_plausible = true;
    }

    // FSAE EV.5.7
    // APPS/Brake Pedal Plausability Check
    if (((accel1_ > ((END_ACCELERATOR_PEDAL_1 - START_ACCELERATOR_PEDAL_1) / 4 +
                     START_ACCELERATOR_PEDAL_1)) ||
         (accel2_ > ((END_ACCELERATOR_PEDAL_2 - START_ACCELERATOR_PEDAL_2) / 4 +
                     START_ACCELERATOR_PEDAL_2))) &&
        brake_is_active_)
    {
        
        accel_and_brake_plausible = false;
    }
    else if ((accel1_ <
              ((END_ACCELERATOR_PEDAL_1 - START_ACCELERATOR_PEDAL_1) / 20 +
               START_ACCELERATOR_PEDAL_1)) &&
             (accel2_ <
              ((END_ACCELERATOR_PEDAL_2 - START_ACCELERATOR_PEDAL_2) / 20 +
               START_ACCELERATOR_PEDAL_2)))
    {
        accel_and_brake_plausible = true;
        implausibility_occured_ = false; // only here do we want to reset this flag
    }
    else
    {
        accel_and_brake_plausible = true;
    }

    if ((!accel_and_brake_plausible) || (!brake_is_plausible) ||
        (!accel_is_plausible))
    {
        implausibility_occured_ = true;
    }

    impl_occ = implausibility_occured_;
    Serial.printf("Implaus occured: %d accel plaus: %d brake plaus: %d accel and brake plaus: %d\n",implausibility_occured_,accel_is_plausible,brake_is_plausible,accel_and_brake_plausible);
}

// idgaf anything below (all wheel speed)
double PedalHandler::get_wsfr() { return current_rpm2; }
double PedalHandler::get_wsfl() { return current_rpm; }
void PedalHandler::get_ws()
{
    unsigned long test = millis();
    if ((test - current_rpm_change_time) > RPM_TIMEOUT)
    {
        // rpm.data = 0;
        current_rpm = 0;
    }
    if (wsfl_->available())
    {
        // average several reading together
        sum = sum + wsfl_->read();
        count = count + 1;
        current_rpm_change_time = millis();
        if (count > 1)
        {
            float testRpm = wsfl_->countToFrequency(sum / count) * 60 / 18;
            current_rpm = testRpm;

            /*if ( testRpm - prev_rpm < 1)
            {
              current_rpm = testRpm;
              prev_rpm = testRpm;
            }*/

            sum = 0;
            count = 0;
            // prev_rpm = testRpm;
        }
    }
    unsigned long test2 = millis();
    if ((test2 - current_rpm_change_time2) > RPM_TIMEOUT)
    {
        // rpm.data = 0;
        current_rpm2 = 0;
    }
    if (wsfr_->available())
    {
        // average several reading together
        sum2 = sum2 + wsfr_->read();
        count2 = count2 + 1;
        current_rpm_change_time2 = millis();
        if (count2 > 1)
        {
            float testRpm2 = wsfr_->countToFrequency(sum2 / count2) * 60 / 18;
            current_rpm2 = testRpm2;
            /*if ( testRpm2 - prev_rpm2 < 1)
            {
              current_rpm2 = testRpm2;
              prev_rpm2 = testRpm2;
            }*/

            sum2 = 0;
            count2 = 0;
            prev_rpm2 = testRpm2;
        }
    }
}
// Returns true if BSPD is ok, false if not
bool PedalHandler::get_board_sensor_readings(){
    glv_current_ = ALPHA * glv_current_ + (1 - ALPHA) * analogRead(GLV_ISENSE);
    glv_voltage_ = ALPHA * glv_voltage_ + (1 - ALPHA) * analogRead(GLV_VSENSE);
    bspd_voltage_ = ALPHA * bspd_voltage_ + (1 - ALPHA) * analogRead(BSPDSENSE);
    sdc_voltage_ = ALPHA * sdc_voltage_ + (1 - ALPHA) * analogRead(SDCVSENSE);
    sdc_current_ = ALPHA * sdc_current_ + (1 - ALPHA) * analogRead(SDCISENSE);
    vcc_voltage_ = ALPHA * vcc_voltage_ + (1 - ALPHA) * analogRead(_5V_VSENSE);
    analog_input_nine_voltage_ = ALPHA * analog_input_nine_voltage_ + (1 - ALPHA) * analogRead(A9);
    analog_input_ten_voltage_ = ALPHA * analog_input_ten_voltage_ + (1 - ALPHA) * analogRead(A10);
    return (bspd_voltage_ > BSPD_OK_HIGH_THRESHOLD);
}  