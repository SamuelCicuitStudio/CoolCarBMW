#include "BMW_E60_CAN.h"
// ===== Predefined command frames (definitions) =====
const uint8_t CAS_RESET_1[7]     = {0x40,0x05,0x2E,0x3F,0xFF,0x61,0x61};
const uint8_t CAS_RESET_2[4]     = {0x40,0x02,0x11,0x01};

const uint8_t DEFROST_ON_1[5]    = {0x78,0x03,0x30,0x08,0x07};
const uint8_t DEFROST_ON_2[6]    = {0x78,0x04,0x30,0x20,0x07,0x00};
const uint8_t DEFROST_ON_3[6]    = {0x78,0x04,0x30,0x20,0x07,0x02};
const uint8_t DEFROST_OFF_1[5]   = {0x78,0x03,0x30,0x09,0x07};

const uint8_t BLOWER_ON_1[5]     = {0x78,0x03,0x30,0x08,0x07};
const uint8_t BLOWER_ON_2[6]     = {0x78,0x04,0x30,0x26,0x07,0x00};
const uint8_t BLOWER_ON_3[7]     = {0x78,0x05,0x30,0x26,0x07,0x02,0x64};
const uint8_t BLOWER_ON_4[2]     = {0x70,0xF0};
const uint8_t BLOWER_OFF_1[5]    = {0x78,0x03,0x30,0x09,0x07};
const uint8_t BLOWER_OFF_2[2]    = {0x30,0xF0};

const uint8_t AC_ON_1[6]         = {0x78,0x04,0x30,0x5E,0x07,0x04};
const uint8_t AC_OFF_1[6]        = {0x78,0x04,0x30,0x5E,0x07,0x00};

const uint8_t RDC_INIT_FULL_F1[8]= {0x40,0x2A,0x00,0x15,0xFF,0xFF,0xFF,0xFF};
const uint8_t RDC_INIT_FULL_F2[8]= {0x40,0xB8,0x00,0x14,0xFF,0xFF,0xFF,0xFF};
const uint8_t RDC_INIT_FULL_X1[8]= {0xE2,0x3C,0x51,0x59,0x00,0x00,0x00,0x00};
const uint8_t RDC_INIT_FULL_X2[7]= {0x00,0x00,0x00,0xFE,0xFF,0xFE,0xFF};


/**
 * @brief Constructor for BMW_E60_CAN class.
 * 
 * Initializes the CAN interface by accepting an interrupt pin (`intPin`), a pointer to the MCP_CAN object (`can`),
 * and initializes the member variables `rxId` and `len`.
 * 
 * @param intPin The interrupt pin used to trigger CAN bus activity.
 * @param can Pointer to an MCP_CAN object for managing CAN bus communication.
 */
BMW_E60_CAN::BMW_E60_CAN(int intPin, MCP_CAN *can) : rxId(0), len(0), intPin(intPin), can(can) {}

/**
 * @brief Initializes the CAN bus interface.
 * 
 * This method initializes the CAN bus with a baud rate of 100 Kbps and an 8 MHz clock frequency. It also sets the mode to normal
 * and configures the interrupt pin as an input.
 * 
 * @return `true` if initialization was successful, `false` otherwise.
 */
bool BMW_E60_CAN::begin() {
    PressCount = 0;
    lockFLag= false;
    MessCount = 0;

    if (can->begin(MCP_ANY, CAN_100KBPS, MCP_8MHZ) == CAN_OK) {
        can->setMode(MCP_NORMAL);
        pinMode(intPin, INPUT);
        
        return true;
    }
    return false;
}

/**
 * @brief Reads incoming CAN messages and processes them.
 * 
 * This method checks the interrupt pin for activity. If a message is available, it reads the message into `rxBuf`,
 * along with its ID (`rxId`) and length (`len`). After reading the message, it invokes the `processMessages()` method
 * to process the received data.
 */
void BMW_E60_CAN::ReadMessages() {
        can->readMsgBuf(&rxId, &len, rxBuf);  // Read data: rxId = ID, len = data length, rxBuf = data byte(s)
        processMessages();
        rxId = 0;
}


/**
 * @brief Processes received CAN messages and calls the corresponding functions based on the message ID.
 * 
 * This function checks the received message ID and calls the appropriate method to process the data associated with that ID.
 * It handles various vehicle parameters like torque, RPM, door status, engine temperature, etc., by invoking respective getter functions.
 * 
 * @note This function uses a switch-case structure to determine which message to process based on the message ID (`rxId`).
 */
void BMW_E60_CAN::processMessages() {
    switch (rxId) {
        case BMW_E60_handbrake :break;
        GetHandbrake();
        if (Handbrake != prev_Handbrake) {
            Serial.println("Handbrake: " + String(Handbrake ? "ON" : "OFF"));
            prev_Handbrake = Handbrake;
        }; 
        break;
        case BMW_E60_Torque:break;
            GetClutch();
            if (abs(CLutch - prev_CLutch) >= 1) {
                if (CLutch == 37) {
                    Serial.println("ClutchPedal: PRESSED");
                } else if (CLutch == 36) {
                    Serial.println("ClutchPedal: RELEASED");
                }
                prev_CLutch = CLutch;
            }
            break;

        

        case BMW_E60_RPM_Throttle:
            GetRPM(); 
            if(abs(RPM - prev_RPM)>=10){
                //Serial.println("RPM: " + String(RPM));
                prev_RPM = RPM;
            };
            GetThrottle();
            if(abs(Throttle - prev_Throttle)>=10){
                Serial.println("Throttle: " + String(Throttle));
                prev_Throttle = Throttle;
            };
            //GetTorque();            
            //Serial.println("Torque: " + String(GetTorque()));
            break;

        case BMW_E60_ABS_Alive_Signal:break;
            Serial.println("ABS Alive: " + String(GetAbsAlive()));
            break;

        case BMW_E60_SteeringWheelSensor:break;
        case BMW_E60_SteeringWheelSensor_2:break;
            Serial.println("Steering Angle: " + String(GetStrAngle()));
            Serial.println("ICR Steering Angle: " + String(GetIcrStrAngle()));
            break;

        case BMW_E60_wheel_speeds:break;
            Serial.println("Wheel01 Speeds: " + String(GetWheelSpeeds().wheel01_speeds));
            Serial.println("Wheel02 Speeds: " + String(GetWheelSpeeds().wheel02_speeds));
            Serial.println("Wheel03 Speeds: " + String(GetWheelSpeeds().wheel03_speeds));
            Serial.println("Wheel04 Speeds: " + String(GetWheelSpeeds().wheel04_speeds));
            break;

        case BMW_E60_AirBag_Alive_Signal:break;
            Serial.println("Airbag Alive: " + String(GetAirBgAlive()));
            break;

        case BMW_E60_Passenger_Door_Status:break;
            Serial.println("Passenger Door Status: " + String(GetDoorStatus(PassangerDor).doorStatus));break;
            Serial.println("Passenger Door lock Status: " + String(GetDoorStatus(PassangerDor).lockStatus));
            break;

        case BMW_E60_Rear_Passenger_Door_Status:break;
            Serial.println("Rear Passenger Door Status: " + String(GetDoorStatus(RearPassangerDor).doorStatus));break;
            Serial.println("Rear Passenger Door Lock Status: " + String(GetDoorStatus(RearPassangerDor).lockStatus));
            break;

        case BMW_E60_Driver_Door_Status:break;
            Serial.println("Driver Door status: " + String(GetDoorStatus(DriverDor).doorStatus));break;
            Serial.println("Driver Door Lock Stats: " + String(GetDoorStatus(DriverDor).lockStatus));
            break;

        case BMW_E60_Rear_Driver_Door_Status:break;
            Serial.println("Rear Driver Door status: " + String(GetDoorStatus(RearDriverDor).doorStatus));break;
            Serial.println("Rear Driver Door lock status: " + String(GetDoorStatus(RearDriverDor).lockStatus));
            break;

        case BMW_E60_Boot_status:break;
            Serial.println("Boot lock Status: " + String(GetBootStatus().lockStatus));
            Serial.println("Boot button Status: " + String(GetBootStatus().Boot_button));
            Serial.println("Boot Status: " + String(GetBootStatus().Boot_Status));
            break;

        case BMW_E60_Brake:break;
            Serial.println("Brake SIGNAL10843: " + String(GetBrake().SIGNAL10843));
            Serial.println("Brake Pressed : " + String(GetBrake().BrakePressed));
            Serial.println("Brake Forced : " + String(GetBrake().Brake_force));
            break;

        case BMW_E60_Speed:break;
            Serial.println("DSC Speed 41 : " + String(GetSpeed().DSC_Speed41));
            Serial.println("DSC Speed: " + String(GetSpeed().DSC_Speed));
            break;

        case BMW_E60_PDC_Sensordata:break;
            GetPDC();

            if(PDC_Status == 5)
            {if(abs(PDC.Front_Left_outer - prev_PDC.Front_Left_outer) >=10){
                Serial.println("PDC Sensor Data Front_Left_outer: " + String(PDC.Front_Left_outer) + "Cm");
                prev_PDC.Front_Left_outer = PDC.Front_Left_outer;
            };
            if(abs(PDC.Front_Left_inner - prev_PDC.Front_Left_inner) >=10){
                Serial.println("PDC Sensor Data Front_Left_inner: " + String(PDC.Front_Left_inner) + "Cm");
                prev_PDC.Front_Left_inner = PDC.Front_Left_inner;
            };

            if(abs(PDC.Front_Right_inner - prev_PDC.Front_Right_inner) >=10){
                Serial.println("PDC Sensor Data Front_Right_inner: " + String(PDC.Front_Right_inner) + "Cm");
                prev_PDC.Front_Right_inner = PDC.Front_Right_inner;
            };
            if(abs(PDC.Front_Right_outer - prev_PDC.Front_Right_outer) >=10){
                Serial.println("PDC Sensor Data Front_Right_outer: " + String(PDC.Front_Right_outer) + "Cm");
                prev_PDC.Front_Right_outer = PDC.Front_Right_outer;
            };

            if(abs(PDC.Rear_Right_outer - prev_PDC.Rear_Right_outer) >=10){
                Serial.println("PDC Sensor Data Rear_Right_outer: " + String(PDC.Rear_Right_outer) + "Cm");
                prev_PDC.Rear_Right_outer = PDC.Rear_Right_outer;
            };
            if(abs(PDC.Rear_Left_outer - prev_PDC.Rear_Left_outer) >=10){
                Serial.println("PDC Sensor Data Rear_Left_outer: " + String(PDC.Rear_Left_outer) + "Cm");
                prev_PDC.Rear_Left_outer = PDC.Rear_Left_outer;
            };

            if(abs(PDC.Rear_Left_inner - prev_PDC.Rear_Left_inner) >=10){
                Serial.println("PDC Sensor Data Rear_Left_inner: " + String(PDC.Rear_Left_inner) + "Cm");
                prev_PDC.Rear_Left_inner = PDC.Rear_Left_inner;
            };
            if(abs(PDC.Rear_Right_inner - prev_PDC.Rear_Right_inner) >=10){
                Serial.println("PDC Sensor Data Rear_Right_inner: " + String(PDC.Rear_Right_inner) + "Cm");
                prev_PDC.Rear_Right_inner = PDC.Rear_Right_inner;
            };
        }; break;

        case BMW_E60_Engine_Temp:break;
            Serial.println("Engine Coolant_Temp: " + String(GetEngineTemp().Coolant_Temp));
            Serial.println("Engine Oil_Temp: " + String(GetEngineTemp().Oil_Temp));
            break;

        case BMW_E60_steering_wheel_buttons: break;
            Getsteeringwheelbuttons();
            if (abs(steeringwheelbuttons - prev_steeringwheelbuttons) >= 10) {
                switch (steeringwheelbuttons) {
                    case 192:
                        //Serial.println("button released");
                        break;
                    case 208:
                        Serial.println("Down button pressed");
                        break;
                    case 224:
                        Serial.println("Up button pressed");
                        break;
                    case 16576: // 0xCD60
                        Serial.println("CD button pressed");
                        break;
                    case 4288: // 0x10C0
                        Serial.println("Air circulation button pressed");
                        break;
                    case 448: // 0x1C0
                        Serial.println("Call button pressed");
                        break;
                    default:
                        Serial.println("Unknown Steering Wheel Button: " + String(steeringwheelbuttons));
                        break;
                }
                
                prev_steeringwheelbuttons = steeringwheelbuttons;
            }
            break;


        case BMW_E60_Seat_Heating_driver:break;
            Serial.println("Seat Heating Driver: " + String(GetParam4(Seat_heating_buttonDriver)));
            break;

        case BMW_E60_Seat_Heating_passenger:break;
            Serial.println("Seat Heating Passenger: " + String(GetParam4(Seat_heating_buttonPassanger)));
            break;

        case BMW_E60_Adjust_steering_wheel:break;
            Serial.println("Adjust Steering Wheel: " + String(GetParam4(button)));
            break;

        case BMW_E60_Indicator_lever:break;
            Serial.println("Indicator Lever: " + String(GetParam8(Indicator_lever, 0)));
            break;

        case BMW_E60_Indicator_Status:break;
            Serial.println("Indicator Status: " + String(GetParam8(Indicator_Status, 0)));
            break;

        case BMW_E60_Dimmer:break;
            Serial.println("Dimmer: " + String(GetParam8(Dimmer_Value, 0)));
            break;

        case BMW_E60_Key_Fob_Buttons:
        Key_Button = GetParam8(Key_Button, 2);
        if (abs(Key_Button - prev_Key_Button) >= 0) {
            keyAction = "";
            if (Key_Button == 1) {
                keyAction = "Car Unlocked";
                lockFLag =  true;
                MessCount ++;
            } else if (Key_Button == 4) {
                keyAction = "Car Locked";
            } else if (Key_Button == 64) {
                keyAction = "Trunk Opened";
            }
            
            if (prev_keyAction != keyAction) {
                //Serial.println(keyAction);
                prev_keyAction = "";
                prev_keyAction = keyAction;
            }
            
            prev_Key_Button = Key_Button;
        };
         GetParam8(inserted, 1);
            if(abs(inserted - prev_inserted)>= 0){
                //Serial.println("Inserted: " + String(inserted));
                prev_inserted=inserted;
            };
            break;

        case BMW_E60_PDC_Status:break;
            PDC_Status = GetParam4(PDC_Status);
            if (abs(PDC_Status - prev_PDC_Status) >= 1) {
                String statusText = (PDC_Status == 5) ? "ON" : (PDC_Status == 6) ? "OFF" : String(PDC_Status);
                Serial.println("PDC Status: " + statusText);
                prev_PDC_Status = PDC_Status;
            }
            break;
        

        case BMW_E60_Wiper_Status:break;
            Serial.println("Wiper Status: " + String(GetParam8(Front_Whiper_direction, 0)));
            break;

        case BMW_E60_Whiper_lever:break;
            Serial.println("Wiper Lever lever: " + String(GetWhiper().Whiper_lever));
            Serial.println("Wiper Lever Speed: " + String(GetWhiper().Whiper_Speed));
            break;

        case BMW_E60_Outside_temp:break;
            Outside_Temp = GetParam8(Outside_Temp, 0) * 0.343137;
            Serial.println("Outside Temp: " + String(Outside_Temp));
            break;

        case BMW_E60_Inside_Lights:break;
            Serial.println("Inside Lights: " + String(GetParam4(Inside_Lights)));
            break;

        case BMW_E60_DateTime:break;
            Serial.println("Date Time: " + String(GetTimeData().Day));
            break;

        case BMW_E60_Door_Status:
            GetBoolDorStatus(); 
            if(BoolDoorStat.Driver_Door!= prev_BoolDoorStat.Driver_Door){
                if(BoolDoorStat.Driver_Door){
                    Serial.println("Driver Door Opened!");
                }else{
                    Serial.println("Driver Door Closed!");
                }
                prev_BoolDoorStat.Driver_Door = BoolDoorStat.Driver_Door;
            };
            if(BoolDoorStat.rear_Driver_Door != prev_BoolDoorStat.rear_Driver_Door){
                if(BoolDoorStat.rear_Driver_Door){
                    Serial.println("Rear Driver Door Opened!");
                }else{
                    Serial.println("Rear Driver Door Closed!");
                }
                prev_BoolDoorStat.rear_Driver_Door = BoolDoorStat.rear_Driver_Door;
            };
            if(BoolDoorStat.Passenger_Door != prev_BoolDoorStat.Passenger_Door){
                if(BoolDoorStat.Driver_Door){
                    Serial.println("Passenger Door Opened!");
                }else{
                    Serial.println("Passenger Door Closed!");
                }
                prev_BoolDoorStat.Passenger_Door = BoolDoorStat.Passenger_Door;
            };
            if(BoolDoorStat.rear_Passenger_Door!= prev_BoolDoorStat.rear_Passenger_Door){
                if(BoolDoorStat.rear_Driver_Door){
                    Serial.println("Rear Passenger Door Opened!");
                }else{
                    Serial.println("Rear Passenger Door Closed!");
                }
                prev_BoolDoorStat.rear_Passenger_Door = BoolDoorStat.rear_Passenger_Door;
            };
            if(BoolDoorStat.Bonnet!=prev_BoolDoorStat.Bonnet){
                if(BoolDoorStat.Bonnet){
                   // Serial.println("Bonnet Opened!");
                }else{
                    //Serial.println("Bonnet Closed!");
                }
                prev_BoolDoorStat.Bonnet = BoolDoorStat.Bonnet;
            };
            break;

        case BMW_E60_gear_lever: break;
            gear_lever = GetParam8(gear_lever, 0);
            Serial.println("Gear Lever: " + String(gear_lever));
            break;

        case BMW_E60_Seconds_since_bat_change:break;
            Serial.println("Seconds Since Battery Change: " + String(GetSecSince().Seconds_since_bat_change));
            Serial.println("days Since Battery Change: " + String(GetSecSince().days_since_bat_change));
            break;

        case BMW_E60_InternalTemp:break;

            InternalTemp = (GetParam8(InternalTemp, 3) * 0.1) + 6;
            if(abs(InternalTemp-prev_InternalTemp)>=0.5){
                Serial.println("Internal Temp: " + String(InternalTemp) + " °C");
                prev_InternalTemp = InternalTemp;
            }            
            break;

        case BMW_E60_odo_range_avFuel:break;
            //Serial.println("Odo : " + String(GetOdoRange().ODO));
            //Serial.println("Odo Range AV_Fuel: " + String(GetOdoRange().AV_Fuel));

            GetOdoRange();
            if(abs(OdoRange.Range-prev_OdoRange.Range)>=1){
                Serial.println("Odo Range: " + String(OdoRange.Range) + "Km");
                prev_OdoRange.Range = OdoRange.Range;
            };
            break;

        case BMW_E60_DSC:break;
            Serial.println("DSC: " + String(GetParam8(DSC, 3)));
            break;

        case BMW_E60_avSpeed_avMileage:break;
            Serial.println("Avg Mileage: " + String(GetAvSpdMil().avMileage));
            Serial.println("Avg Speed: " + String(GetAvSpdMil().avSpeed));
            Serial.println("Avg Mileage 2: " + String(GetAvSpdMil().avMileage_2));
            Serial.println("Avg Speed 2: " + String(GetAvSpdMil().avSpeed_2));
            break;

        case BMW_E60_Outside_Temp_and_Range:break;
            Serial.println("Outside Temp: " + String(GetOUtempRang().Temp));
            Serial.println("Outside Range: " + String(GetOUtempRang().Range));
            break;

        case BMW_E60_VIN:break;
            Serial.println("VIN Range: " + String(GetOUtempRang().Range));
            Serial.println("VIN Temp: " + String(GetOUtempRang().Temp));
            break;

        case BMW_E60_Voltage_EngineState:break;
            GetVoltEnSTA();
            if(abs(VoltengSTA.Voltage - prev_VoltengSTA.Voltage)>=0.2){
                if(VoltengSTA.Voltage <= 7) goto out;
                Serial.println("Voltage: " + String(VoltengSTA.Voltage) + "V");
                prev_VoltengSTA.Voltage = VoltengSTA.Voltage;
                out:;
            };
            if(abs(VoltengSTA.Engine_running_state - prev_VoltengSTA.Engine_running_state)>=5){
                
                if(VoltengSTA.Engine_running_state == 0){
                    //Serial.println("Engine Started!");
                    //Serial.println("Engine running state : " + String(VoltengSTA.Engine_running_state));
                };
                if(VoltengSTA.Engine_running_state > 5){
                    //Serial.println("Engine Stopped!");
                    //Serial.println("Engine running state : " + String(VoltengSTA.Engine_running_state));
                };

                prev_VoltengSTA.Engine_running_state = VoltengSTA.Engine_running_state;
            };
            break;
            case BMW_E60_KL15_STATUS:
            GetKl15();
            if(abs(KL15_stat-prev_KL15_stat)> 0.1){
                Serial.println("KL15: " + String(KL15_stat));
                prev_KL15_stat = KL15_stat;
            }; break;

        default:
            //Serial.println("Unknown Message ID: " + String(rxId));
            break;
    }
}

/**
 * @brief Retrieves the voltage and engine state information.
 * 
 * This function processes the CAN bus data to extract the voltage and engine running state.
 * 
 * @return Voltage_EngineState The voltage and engine running state data.
 */
Voltage_EngineState BMW_E60_CAN::GetVoltEnSTA(){
    VoltengSTA.Voltage =   0.0147059*(((rxBuf[1] & 0x0F) << 8) | rxBuf[0]);
    VoltengSTA.Engine_running_state=rxBuf[2] & 0x0F;
    return VoltengSTA;
}

/**
 * @brief Retrieves the VIN (Vehicle Identification Number) from the CAN bus.
 * 
 * This function extracts and returns the VIN value from the CAN bus data.
 * 
 * @return uint16_t The VIN value.
 */
uint16_t BMW_E60_CAN::GetVin(){
    Vin = 0.0625 *((rxBuf[2] << 8) | rxBuf[1]);
    return Vin;
}

/**
 * @brief Retrieves the outside temperature and range data.
 * 
 * This function processes the CAN bus data to extract the outside temperature and range values.
 * 
 * @return Outside_Temp_and_Range The outside temperature and range data.
 */
Outside_Temp_and_Range BMW_E60_CAN::GetOUtempRang(){
    OutTempR.Temp = rxBuf[0] * 0.343137 ;
    OutTempR.Range = 0.0625 *((rxBuf[2] << 8) | rxBuf[1]);
    return OutTempR; 
}

/**
 * @brief Retrieves the average speed and mileage data.
 * 
 * This function processes the CAN bus data to extract the average speed and mileage information.
 * 
 * @return avSpeed_avMileage The average speed and mileage data.
 */
avSpeed_avMileage BMW_E60_CAN::GetAvSpdMil() {
    SpdMileage.avMileage = 0.1*((rxBuf[1] << 8) | (rxBuf[0] & 0x0F));  // Mask rxBuf[0] and combine with rxBuf[1]
    SpdMileage.avSpeed  = 0.1*((rxBuf[2] << 8) | ((rxBuf[1] & 0xF0) >> 4)); // Mask rxBuf[1] and shift the upper 4 bits
    SpdMileage.avMileage_2 = 0.1*(((rxBuf[4] & 0x0F) << 8) | rxBuf[3]); // Mask rxBuf[4] and combine with rxBuf[3]
    SpdMileage.avSpeed_2 = 0.1*((rxBuf[5] << 8) | ((rxBuf[4] & 0xF0) >> 4)); // Mask rxBuf[4] and shift the upper 4 bits
    return SpdMileage;
}

/**
 * @brief Retrieves the odometer, range, and average fuel data.
 * 
 * This function processes the CAN bus data to extract the odometer, range, and average fuel values.
 * 
 * @return odo_range_avFuel The odometer, range, and average fuel data.
 */
odo_range_avFuel BMW_E60_CAN::GetOdoRange(){
    OdoRange.ODO = (rxBuf[2] << 16) | (rxBuf[1] << 8) | rxBuf[0]; 
    OdoRange.AV_Fuel = rxBuf[3];
    OdoRange.Range = 0.0625*((rxBuf[7] << 8) | rxBuf[6]);
    return OdoRange;
}

/**
 * @brief Retrieves the seconds and days since the battery change.
 * 
 * This function extracts the seconds and days since the battery change from the CAN bus data.
 * 
 * @return Seconds_since_bat_change The seconds and days since the battery change data.
 */
Seconds_since_bat_change BMW_E60_CAN::GetSecSince() {
    SecSince.Seconds_since_bat_change = (rxBuf[3] << 24) | (rxBuf[2] << 16) | (rxBuf[1] << 8) | rxBuf[0]; // Combine bytes 0-3 for days_since_bat_change
    SecSince.days_since_bat_change = (rxBuf[5] << 8) | rxBuf[4]; // Combine bytes 4-5 for seconds_since_bat_change
    return SecSince;
}

/**
 * @brief Retrieves the door status of the vehicle.
 * 
 * This function processes the CAN bus data to extract the door status for various doors (e.g., front, rear, etc.).
 * 
 * @return Door_Status The status of each door in the vehicle.
 */
Door_Status BMW_E60_CAN::GetBoolDorStatus() {
    BoolDoorStat.rear_Driver_Door = (rxBuf[1] >> 4) & 0x01;  // bit 4
    BoolDoorStat.Passenger_Door = (rxBuf[1] >> 2) & 0x01;    // bit 2
    BoolDoorStat.rear_Passenger_Door = (rxBuf[1] >> 6) & 0x01; // bit 6
    BoolDoorStat.Driver_Door = rxBuf[1] & 0x01;               // bit 0
    BoolDoorStat.Boot = rxBuf[2] & 0x01;                       // bit 0
    BoolDoorStat.Bonnet = (rxBuf[2] >> 2) & 0x01;             // bit 2

    return BoolDoorStat;
}

/**
 * @brief Retrieves the time data (hour, minute, second, day, month, year).
 * 
 * This function processes the CAN bus data to extract the time-related information such as hour, minute, second, day, month, and year.
 * 
 * @return DateTime_ The time data of the vehicle.
 */
DateTime_ BMW_E60_CAN::GetTimeData(){
    Time.Hour = rxBuf[0];
    Time.Minute= rxBuf[1];
    Time.Second= rxBuf[2];
    Time.Month= rxBuf[4]&0x0F;
    Time.Day= rxBuf[3];
    Time.Year= (rxBuf[6] << 8) | rxBuf[5];;
    return Time;
}

/**
 * @brief Retrieves the wiper status and speed.
 * 
 * This function processes the CAN bus data to extract the wiper status and speed.
 * 
 * @return Whiperlever The wiper status and speed data.
 */
Whiperlever BMW_E60_CAN::GetWhiper(){
    Whiper.Whiper_Speed = rxBuf[1]& 0x0F;
    Whiper.Whiper_lever = rxBuf[0];
    return Whiper;
}

/**
 * @brief Retrieves the 4-bit parameter from the CAN bus.
 * 
 * This function extracts the 4-bit parameter from the CAN bus data.
 * 
 * @param type The value of the 4-bit parameter.
 * @return uint8_t The 4-bit parameter value.
 */
uint8_t BMW_E60_CAN::GetParam4(uint8_t type){
    type = rxBuf[0]& 0x0F;
    return type;
}

/**
 * @brief Retrieves the 8-bit parameter from the CAN bus.
 * 
 * This function extracts the 8-bit parameter from the specified index in the CAN bus data.
 * 
 * @param type The value of the 8-bit parameter.
 * @param index The index to retrieve the parameter from.
 * @return uint8_t The 8-bit parameter value.
 */
uint8_t BMW_E60_CAN::GetParam8(uint8_t type, uint8_t index){
    type = rxBuf[index];
    return type;
}

/**
 * @brief Retrieves the steering wheel buttons status.
 * 
 * This function processes the CAN bus data to extract the steering wheel button status.
 * 
 * @return uint16_t The steering wheel buttons status.
 */
uint16_t BMW_E60_CAN::Getsteeringwheelbuttons(){
    steeringwheelbuttons = (rxBuf[1] << 8) | rxBuf[0];
    return steeringwheelbuttons;
}

/**
 * @brief Retrieves the engine temperature (coolant and oil).
 * 
 * This function processes the CAN bus data to extract the coolant and oil temperature values.
 * 
 * @return Engine_Temp The coolant and oil temperature data.
 */
Engine_Temp BMW_E60_CAN::GetEngineTemp(){
    EngineTemp.Coolant_Temp = rxBuf[0] - 48 ;
    EngineTemp.Oil_Temp = rxBuf[1] - 48 ;
    return EngineTemp;
}

/**
 * @brief Retrieves the PDC (Park Distance Control) sensor data.
 * 
 * This function processes the CAN bus data to extract the PDC sensor readings for the front and rear of the vehicle.
 * 
 * @return PDC_Sensordata The PDC sensor data for the vehicle.
 */
PDC_Sensordata BMW_E60_CAN::GetPDC(){
    PDC.Front_Left_outer = rxBuf[7];
    PDC.Front_Left_inner = rxBuf[6];
    PDC.Front_Right_inner = rxBuf[5];
    PDC.Front_Right_outer = rxBuf[4];
    PDC.Rear_Right_outer = rxBuf[0];
    PDC.Rear_Left_outer = rxBuf[3];
    PDC.Rear_Left_inner = rxBuf[2];
    PDC.Rear_Right_inner = rxBuf[1];

    return PDC;
}

/**
 * @brief Retrieves the vehicle speed data.
 * 
 * This function processes the CAN bus data to extract the speed data for the vehicle.
 * 
 * @return Speed The speed data of the vehicle.
 */
Speed BMW_E60_CAN::GetSpeed(){
    int Raw1 = (rxBuf[3] << 8) | rxBuf[2];
    int Raw2 = (rxBuf[1] << 8) | rxBuf[0];

    Spd.DSC_Speed = 0.01 * Raw1;
    Spd.DSC_Speed41 = 0.01 * Raw2;

    return Spd;
}

/**
 * @brief Retrieves the brake system status and force data.
 * 
 * This function processes the CAN bus data to extract brake system information, such as brake force and the brake pressed status.
 * 
 * @return Brake The brake system data.
 */
Brake BMW_E60_CAN::GetBrake(){
    brake.SIGNAL10843 = rxBuf[2] & 0x0F;
    brake.Brake_force = rxBuf[6];
    brake.BrakePressed = rxBuf[5];
    return brake;
}

/**
 * @brief Retrieves the boot status and button information.
 * 
 * This function processes the CAN bus data to extract the boot lock status, boot button status, and boot overall status.
 * 
 * @return BootStatus The boot status and button data.
 */
BootStatus BMW_E60_CAN::GetBootStatus(){
    Boot.lockStatus = rxBuf[0] & 0x0F;
    Boot.Boot_button = rxBuf[2] & 0x0F;
    Boot.Boot_Status = rxBuf[3] & 0x0F;
    return Boot;
}

/**
 * @brief Retrieves the door status for the passenger door.
 * 
 * This function processes the CAN bus data to extract the lock and door status for the passenger door.
 * 
 * @param dor The structure to hold the door status data.
 * @return DoorStatus The passenger door status data.
 */
DoorStatus BMW_E60_CAN::GetDoorStatus(DoorStatus dor) {
    PassangerDor.lockStatus = rxBuf[0];
    PassangerDor.doorStatus = rxBuf[1];
    return PassangerDor;
}



/**
 * @brief Retrieves the airbag alive signal.
 * 
 * This function processes the CAN bus data to extract the airbag alive signal status.
 * 
 * @return uint8_t The airbag alive signal value.
 */
uint8_t BMW_E60_CAN::GetAirBgAlive() {
    AirBag_Alive_Signal = rxBuf[0];
    return AirBag_Alive_Signal;
}

/**
 * @brief Retrieves the torque value.
 * 
 * This function processes the CAN bus data to extract the torque value and returns it in Nm.
 * 
 * @return double The torque value in Nm.
 */
double BMW_E60_CAN::GetTorque() {
    int TorqueRaw = (rxBuf[2] << 8) | rxBuf[1];
    Torque = 0.03125 * TorqueRaw;
    return Torque;
}

/**
 * @brief Retrieves the RPM (Revolutions Per Minute) value.
 * 
 * This function processes the CAN bus data to extract the RPM value and returns it.
 * 
 * @return double The RPM value.
 */
double BMW_E60_CAN::GetRPM() {
    int RPMRaw = (rxBuf[5] << 8) | rxBuf[4];
    RPM = 0.25 * RPMRaw;
    return RPM;
}

/**
 * @brief Retrieves the throttle value.
 * 
 * This function processes the CAN bus data to extract the throttle value and returns it.
 * 
 * @return uint8_t The throttle value.
 */
uint8_t BMW_E60_CAN::GetThrottle() {
    int ThrottleRaw = rxBuf[3];
    Throttle = 1 * ThrottleRaw;
    return Throttle;
}

/**
 * @brief Retrieves the ABS (Anti-lock Braking System) alive signal.
 * 
 * This function processes the CAN bus data to extract the ABS alive signal status.
 * 
 * @return uint8_t The ABS alive signal value.
 */
uint8_t BMW_E60_CAN::GetAbsAlive() {
    return rxBuf[0] & 0x0F;
}

/**
 * @brief Retrieves the steering angle.
 * 
 * This function processes the CAN bus data to extract the steering angle and returns it in degrees.
 * 
 * @return float The steering angle in degrees.
 */
float BMW_E60_CAN::GetStrAngle() {
    int Raw = (rxBuf[1] << 8) | rxBuf[0];
    Steering_angle = 0.0434783 * Raw;
    return Steering_angle;
}

/**
 * @brief Retrieves the incremental steering angle.
 * 
 * This function processes the CAN bus data to extract the incremental steering angle and returns it in degrees.
 * 
 * @return float The incremental steering angle in degrees.
 */
float BMW_E60_CAN::GetIcrStrAngle() {
    int Raw = (rxBuf[4] << 8) | rxBuf[3];
    Incremental_steering_angle = 0.0434783 * Raw;
    return Incremental_steering_angle;
}

/**
 * @brief Retrieves the wheel speeds for all four wheels.
 * 
 * This function calculates the wheel speeds for all four wheels based on the raw RPM data from the CAN bus.
 * The speed is calculated using a helper lambda function.
 * 
 * @return WheelSpeeds The wheel speed data for all four wheels.
 */
WheelSpeeds BMW_E60_CAN::GetWheelSpeeds() {
    // Helper lambda function to calculate the wheel speed from raw RPM data
    auto calculateWheelSpeed = [](uint8_t highByte, uint8_t lowByte) -> float {
        int RPMRaw = (highByte << 8) | lowByte;
        return 0.0416667f * RPMRaw;
    };

    // Calculate speeds for each wheel
    wheelSpeed.wheel01_speeds = calculateWheelSpeed(rxBuf[1], rxBuf[0]);
    wheelSpeed.wheel02_speeds = calculateWheelSpeed(rxBuf[3], rxBuf[2]);
    wheelSpeed.wheel03_speeds = calculateWheelSpeed(rxBuf[5], rxBuf[4]);
    wheelSpeed.wheel04_speeds = calculateWheelSpeed(rxBuf[7], rxBuf[6]);

    return wheelSpeed;
}


uint8_t BMW_E60_CAN::GetClutch(){
    CLutch = rxBuf[5] - 204;
    return CLutch;
}

bool BMW_E60_CAN::GetHandbrake() {
    Handbrake = (rxBuf[5] & 0x02) >> 1;
    return Handbrake;
}

uint8_t BMW_E60_CAN::GetKl15(){
    KL15_stat = rxBuf[0];
    return KL15_stat;
}

 void BMW_E60_CAN:: printFrame(uint16_t id, uint8_t len, const uint8_t *d){
  Serial.print("CAN "); id3(id);
  Serial.print(" ["); Serial.print(len); Serial.print("]  ");
  for(uint8_t i=0;i<8;i++){
    if(i) Serial.print(' ');
    if(i<len) hx2(d[i]); else Serial.print("..");
  }
  Serial.println();
}


 void BMW_E60_CAN::armReplyWait(const uint16_t *ids, uint8_t n, uint32_t timeout_ms, const char* label){
  gWait.active = true;
  gWait.nExpect = (n>4) ? 4 : n;
  for(uint8_t i=0;i<gWait.nExpect;i++) gWait.expect[i] = ids[i];
  gWait.deadline_ms = millis() + timeout_ms;
  gWait.label = label;
}

 void BMW_E60_CAN:: checkReplyTimeout(){
  if(gWait.active && (int32_t)(millis() - gWait.deadline_ms) >= 0){
    Serial.print(F("# no reply within timeout: "));
    if(gWait.label) Serial.println(gWait.label); else Serial.println(F("(unnamed)"));
    clearReplyWait();
  }
}
void BMW_E60_CAN::decodeKombiTime(const uint8_t *b, uint8_t len){
  if(len < 7){ Serial.println(F("# time packet too short")); return; }
  uint8_t hh=b[0], mm=b[1], ss=b[2], dd=b[3];
  uint8_t mcode=b[4];
  uint16_t yy = (uint16_t)b[5] | ((uint16_t)b[6]<<8);
  uint8_t mo = ((mcode & 0x0F)==0x0F) ? (mcode>>4) : 0;
  auto pr2=[&](uint8_t v){ if(v==0xFD||v==0xFF){Serial.print("--");}else{if(v<10)Serial.print('0');Serial.print(v);} };
  Serial.print(F("KOMBI TIME: "));
  pr2(hh); Serial.print(':'); pr2(mm); Serial.print(':'); pr2(ss);
  Serial.print(' '); Serial.print(dd); Serial.print('/');
  if(mo) Serial.print(mo); else Serial.print("??");
  Serial.print('/'); Serial.println(yy);
}

void BMW_E60_CAN:: sendAndPrint(uint16_t id, uint8_t len, const uint8_t *data){
  if(can->sendMsgBuf(id,0,len,(byte*)data)==CAN_OK){
    Serial.print(F("TX  ")); id3(id); Serial.print(F(" [")); Serial.print(len); Serial.print(F("]  "));
    for(uint8_t i=0;i<len;i++){ if(i) Serial.print(' '); hx2(data[i]); }
    Serial.println();
  } else {
    Serial.print(F("# send fail id=0x")); id3(id); Serial.println();
  }
}

String BMW_E60_CAN::readLine(){
  static String line;
  while(Serial.available()){
    char c=(char)Serial.read();
    if(c=='\r') continue;
    if(c=='\n'){ String out=line; line=""; return out; }
    line += c;
  }
  return String();
}

// ---- help + status ----
void BMW_E60_CAN:: cmd_help(){
  Serial.println(F("Commands:"));
  Serial.println(F("  help"));
  Serial.println(F("  status                  -> show commands + what is monitored"));
  Serial.println(F("  report time"));
  Serial.println(F("  set time SS MM HH DD MM YYYY   (decimal)"));
  Serial.println(F("  cas_reset"));
  Serial.println(F("  defrost_on / defrost_off"));
  Serial.println(F("  blower_on / blower_off"));
  Serial.println(F("  ac_on / ac_off"));
  Serial.println(F("  rdc_init_full"));
}

void  BMW_E60_CAN::printStatus(){
  Serial.println(F("\n===== BMW K-CAN Tool Status ====="));
  Serial.println(F("Device: ESP32 + MCP2515 (8 MHz), K-CAN 100 kbps"));
  Serial.print  (F("Pins:   CS=")); Serial.print(CAN_CS_PIN);
  Serial.print  (F("  INT="));     Serial.println(CAN0_INT);
  Serial.println(F("Serial: 115200 8N1"));
  Serial.println();
  cmd_help();
  Serial.println();
  Serial.println(F("Listening for:"));
  Serial.println(F("  0x2F8  (KOMBI time)"));
  Serial.println(F("  0x338  (CC-ID, FE FE FE tail)"));
  Serial.println(F("  Replies: KOMBI set time=0x660, CAS=0x640, IHKA=0x678, RDC=0x5AA/0x7C4/0x336"));
  if (gWait.active){ Serial.print(F("Pending reply for ")); if(gWait.label) Serial.print(gWait.label); Serial.println(); }
  else Serial.println(F("Pending reply: none"));
  if (s2F8.seen){ Serial.print(F("Last KOMBI time: ")); decodeKombiTime(s2F8.data,s2F8.len); }
  else Serial.println(F("Last KOMBI time: (none yet)"));
  Serial.println(F("=================================\n"));
}

void BMW_E60_CAN:: printStatus(){
  Serial.println(F("\n===== BMW K-CAN Tool Status ====="));
  Serial.println(F("Device: ESP32 + MCP2515 (8 MHz), K-CAN 100 kbps"));
  Serial.print  (F("Pins:   CS=")); Serial.print(CAN_CS_PIN);
  Serial.print  (F("  INT="));     Serial.println(CAN0_INT);
  Serial.println(F("Serial: 115200 8N1"));
  Serial.println();
  cmd_help();
  Serial.println();
  Serial.println(F("Listening for:"));
  Serial.println(F("  0x2F8  (KOMBI time)"));
  Serial.println(F("  0x338  (CC-ID, FE FE FE tail)"));
  Serial.println(F("  Replies: KOMBI set time=0x660, CAS=0x640, IHKA=0x678, RDC=0x5AA/0x7C4/0x336"));
  if (gWait.active){ Serial.print(F("Pending reply for ")); if(gWait.label) Serial.print(gWait.label); Serial.println(); }
  else Serial.println(F("Pending reply: none"));
  if (s2F8.seen){ Serial.print(F("Last KOMBI time: ")); decodeKombiTime(s2F8.data,s2F8.len); }
  else Serial.println(F("Last KOMBI time: (none yet)"));
  Serial.println(F("=================================\n"));
}

void BMW_E60_CAN::handleCommand(const String &line){
  if(line.length()==0) return;
  String low=line; low.trim(); for(size_t i=0;i<low.length();i++){ char c=low[i]; if(c>='A'&&c<='Z') low.setCharAt(i,c+32); }
  if(low=="help"){ cmd_help(); return; }
  if(low=="status"){ printStatus(); return; }
  if(low=="report time"){ if(s2F8.seen) decodeKombiTime(s2F8.data,s2F8.len); else Serial.println(F("# no KOMBI time seen")); return; }
  if(low.startsWith("set time")){
    long vals[6]; int vi=0; int p=line.indexOf(' '); String rest=line.substring(p+1); rest.trim();
    int i=0; while(vi<6 && i<rest.length()){ while(i<rest.length() && rest[i]==' ') i++; int j=i; while(j<rest.length()&&rest[j]!=' ') j++; bool ok; long v=parseDec(rest.substring(i,j),ok); if(!ok){Serial.println(F("# bad number"));return;} vals[vi++]=v; i=j; }
    if(vi<6){ Serial.println(F("# need 6 values: SS MM HH DD MM YYYY")); return; }
    uint8_t bSS=vals[0], bMM=vals[1], bHH=vals[2], bDD=vals[3]; uint8_t MO=vals[4]; uint16_t YYYY=vals[5];
    uint8_t ylo=(uint8_t)(YYYY&0xFF); uint8_t mcode=(MO<<4)|0x0F;
    uint8_t f1[8]={0x60,0x10,bHH,bMM,bSS,bDD,mcode,ylo}; uint8_t f2[8]={0x60,0x21,bHH,bMM,bSS,bDD,mcode,ylo};
    Serial.print(F("# set time ")); Serial.print(bHH);Serial.print(':');Serial.print(bMM);Serial.print(':');Serial.println(bSS);
    sendAndPrint(0x6F1,8,f1); delay(35); sendAndPrint(0x6F1,8,f2); delay(35);
    const uint16_t expect[]={0x660}; armReplyWait(expect,1,800,"KOMBI set time"); return;
  }
  if(low=="cas_reset"){ do_cas_reset(); return; }
  if(low=="defrost_on"){ do_defrost_on(); return; }
  if(low=="defrost_off"){ do_defrost_off(); return; }
  if(low=="blower_on"){ do_blower_on(); return; }
  if(low=="blower_off"){ do_blower_off(); return; }
  if(low=="ac_on"){ do_ac_on(); return; }
  if(low=="ac_off"){ do_ac_off(); return; }
  if(low=="rdc_init_full"){ do_rdc_init_full(); return; }
  Serial.println(F("# unknown command — type help"));
}

void BMW_E60_CAN::handleRx(){
  if(CAN_MSGAVAIL!=can->checkReceive()) return;
  long unsigned int rxId; unsigned char len; unsigned char buf[8]; can->readMsgBuf(&rxId,&len,buf); uint16_t id=(uint16_t)rxId;
  if(gWait.active){ for(uint8_t i=0;i<gWait.nExpect;i++){ if(id==gWait.expect[i]){ Serial.print(F("ACK from ECU (")); if(gWait.label) Serial.print(gWait.label); Serial.print(F(") id=0x")); id3(id); Serial.println(); clearReplyWait(); break; } } }
  if(id==0x2F8){ s2F8.seen=true; s2F8.len=len; memcpy(s2F8.data,buf,len); Serial.print(F("TME ")); printFrame(id,len,buf); return; }
  if(id==0x338 && len>=8 && buf[5]==0xFE&&buf[6]==0xFE&&buf[7]==0xFE){ uint16_t cc=(uint16_t)buf[0]|((uint16_t)buf[1]<<8); Serial.print(F("CC-ID ")); Serial.println(cc); return; }
}