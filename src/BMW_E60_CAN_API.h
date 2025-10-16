#ifndef BMW_E60_CAN_API_H
#define BMW_E60_CAN_API_H

#include <mcp_can.h>
#include <SPI.h>
#include <Arduino.h>

// Define CAN message IDs
#define BMW_E60_odo_range_avFuel           0x330
#define BMW_E60_KL15_STATUS                0x130
#define BMW_E60_VIN                        0x380
#define BMW_E60_Voltage_EngineState        0x3B4
#define BMW_E60_Engine_Temp                0x1D0
#define BMW_E60_Torque                     0xA8
#define BMW_E60_RPM_Throttle               0xAA
#define BMW_E60_wheel_speeds               0xCE
#define BMW_E60_steering_wheel_buttons     0x1D6
#define BMW_E60_Seat_Heating_driver        0x1E7
#define BMW_E60_gear_lever                 0x304
#define BMW_E60_Indicator_lever            0x1EE
#define BMW_E60_Inside_Lights              0x2F6
#define BMW_E60_PDC_Status                 0x24A
#define BMW_E60_Whiper_lever               0x2A6
#define BMW_E60_Outside_temp               0x2CA
#define BMW_E60_ABS_Alive_Signal           0xC0
#define BMW_E60_Seat_Heating_passenger     0x1E8
#define BMW_E60_DateTime                   0x2F8
#define BMW_E60_Adjust_steering_wheel      0x1EA
#define BMW_E60_Indicator_Status           0x1F6
#define BMW_E60_Dimmer                     0x202
#define BMW_E60_Key_Fob_Buttons            0x23A
#define BMW_E60_Wiper_Status               0x252
#define BMW_E60_Door_Status                0x2FC
#define BMW_E60_Outside_Temp_and_Range     0x366
#define BMW_E60_Passenger_Door_Status      0xE2
#define BMW_E60_Rear_Passenger_Door_Status 0xE6
#define BMW_E60_Driver_Door_Status         0xEA
#define BMW_E60_Rear_Driver_Door_Status    0xEE
#define BMW_E60_InternalTemp               0x32E
#define BMW_E60_Seconds_since_bat_change   0x328
#define BMW_E60_DSC                        0x336
#define BMW_E60_avSpeed_avMileage          0x362
#define BMW_E60_PDC_Sensordata             0x1C2
#define BMW_E60_AirBag_Alive_Signal        0xD7
#define BMW_E60_Speed                      0x1A0
#define BMW_E60_SteeringWheelSensor        0xC4
#define BMW_E60_SteeringWheelSensor_2      0xC8
#define BMW_E60_Boot_status                0xF2
#define BMW_E60_Brake                      0x19E

#define BMW_E60_handbrake                     0x1B4
#define BMW_E60_AirBag                     0x2FA

// ---- capture KOMBI time ----
struct Snap { bool seen; uint8_t len; uint8_t data[8]; };
static Snap s2F8 = {false,0,{0}};

// ---- reply-wait ----
struct ReplyWait {
  bool active;
  uint16_t expect[4];
  uint8_t nExpect;
  uint32_t deadline_ms;
  const char* label;
} gWait = { false, {0}, 0, 0, nullptr };


// Define struct for Voltage_EngineState
struct Voltage_EngineState {
    float Voltage;
    uint16_t Engine_running_state;
};

// Define struct for Outside_Temp_and_Range
struct Outside_Temp_and_Range {
    uint16_t Temp;
    uint16_t Range;
};

// Define struct for avSpeed_avMileage
struct avSpeed_avMileage {
    uint16_t avMileage;
    uint16_t avSpeed;
    uint16_t avMileage_2;
    uint16_t avSpeed_2;
};

// Define struct for odo_range_avFuel
struct odo_range_avFuel {
    uint16_t ODO;
    uint16_t AV_Fuel;
    uint16_t Range;
};

// Define struct for Seconds_since_bat_change
struct Seconds_since_bat_change {
    uint32_t Seconds_since_bat_change;
    uint32_t days_since_bat_change;
};

// Define struct for Door_Status
struct Door_Status {
    bool rear_Driver_Door;
    bool Passenger_Door;
    bool rear_Passenger_Door;
    bool Driver_Door;
    bool Boot;
    bool Bonnet;

    // Constructor initializes all members to false
    Door_Status() 
        : rear_Driver_Door(false), Passenger_Door(false),
          rear_Passenger_Door(false), Driver_Door(false),
          Boot(false), Bonnet(false) {}
};


// Define struct for DateTime_
struct DateTime_ {
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Month;
    uint8_t Day;
    uint16_t Year;
};

// Define struct for Whiperlever
struct Whiperlever {
    uint8_t Whiper_lever;
    uint8_t Whiper_Speed;
};

// Define struct for Engine_Temp
struct Engine_Temp {
    uint8_t Coolant_Temp;
    uint8_t Oil_Temp; 
};

// Define struct for PDC_Sensordata
struct PDC_Sensordata {
    uint8_t Front_Left_outer;
    uint8_t Front_Left_inner;
    uint8_t Front_Right_inner;
    uint8_t Front_Right_outer;
    uint8_t Rear_Right_outer;
    uint8_t Rear_Left_outer;
    uint8_t Rear_Left_inner;
    uint8_t Rear_Right_inner;
};

// Define struct for WheelSpeeds
struct WheelSpeeds {
    float wheel01_speeds;
    float wheel02_speeds;
    float wheel03_speeds;
    float wheel04_speeds;
};

// Define struct for Brake
struct Brake {
    uint8_t SIGNAL10843;
    uint8_t BrakePressed;
    uint8_t Brake_force;
};

// Define struct for Speed
struct Speed {
    float DSC_Speed41;
    float DSC_Speed;
};

// Define struct for DoorStatus
struct DoorStatus {
    uint8_t lockStatus;
    uint8_t doorStatus;
};

// Define struct for BootStatus
struct BootStatus {
    uint8_t lockStatus;
    uint8_t Boot_button;
    uint8_t Boot_Status;
};

#endif  // BMW_E60_CAN_API_H
