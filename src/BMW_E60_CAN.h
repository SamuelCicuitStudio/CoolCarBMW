#ifndef BMW_E60_CAN_H
#define BMW_E60_CAN_H

#include "BMW_E60_CAN_API.h" // Include the header file for the CAN API
#include <String.h>
#include "Config.h" 
// ===== Predefined command frames (extern) =====
extern const uint8_t CAS_RESET_1[7];
extern const uint8_t CAS_RESET_2[4];

extern const uint8_t DEFROST_ON_1[5];
extern const uint8_t DEFROST_ON_2[6];
extern const uint8_t DEFROST_ON_3[6];
extern const uint8_t DEFROST_OFF_1[5];

extern const uint8_t BLOWER_ON_1[5];
extern const uint8_t BLOWER_ON_2[6];
extern const uint8_t BLOWER_ON_3[7];
extern const uint8_t BLOWER_ON_4[2];
extern const uint8_t BLOWER_OFF_1[5];
extern const uint8_t BLOWER_OFF_2[2];

extern const uint8_t AC_ON_1[6];
extern const uint8_t AC_OFF_1[6];

extern const uint8_t RDC_INIT_FULL_F1[8];
extern const uint8_t RDC_INIT_FULL_F2[8];
extern const uint8_t RDC_INIT_FULL_X1[8];
extern const uint8_t RDC_INIT_FULL_X2[7];

class BMW_E60_CAN {
private:
        //==================== CAN Interface and Message Buffers =========================//

        MCP_CAN *can;                  // Pointer to the MCP_CAN object for CAN communication
        unsigned char len;   // Length of the received message
        int intPin;       // Interrupt pin for CAN bus
        long unsigned int rxId; // ID of the received message
        unsigned char rxBuf[8]; // Buffer to store received CAN message (max 8 bytes)

        //============================ Vehicle Data =====================================//

        double Torque, prev_Torque;     // Vehicle torque value
        double RPM, prev_RPM;           // Vehicle RPM (revolutions per minute)
        uint8_t Throttle, prev_Throttle; // Throttle position (percentage)
        uint8_t AbsAlive, prev_AbsAlive; // ABS (Anti-lock Braking System) alive signal
        float Steering_angle, prev_Steering_angle; // Steering angle in degrees
        float Incremental_steering_angle, prev_Incremental_steering_angle; // Incremental steering angle in degrees
        WheelSpeeds wheelSpeed, prev_wheelSpeed; // Wheel speed data (all four wheels)
        
        //=========================== Door and Boot Status =============================//

        DoorStatus PassangerDor, prev_PassangerDor; // Passenger door status (open/closed)
        DoorStatus RearPassangerDor, prev_RearPassangerDor; // Rear passenger door status (open/closed)
        DoorStatus DriverDor, prev_DriverDor; // Driver door status (open/closed)
        DoorStatus RearDriverDor, prev_RearDriverDor; // Rear driver door status (open/closed)
        BootStatus Boot, prev_Boot; // Boot status (open/closed)
        Door_Status BoolDoorStat, prev_BoolDoorStat; // Boolean door status for different doors
        


        //=========================== Airbag and Sensor Data ===========================//

        uint8_t AirBag_Alive_Signal, prev_AirBag_Alive_Signal; // Airbag alive signal
        
        PDC_Sensordata PDC, prev_PDC; // PDC (Park Distance Control) sensor data

        Engine_Temp EngineTemp, prev_EngineTemp; // Engine temperature data
        Outside_Temp_and_Range OutTempR, prev_OutTempR; // Outside temperature and range information
        Whiperlever Whiper, prev_Whiper; // Wiper lever position (on/off, speed)
        Seconds_since_bat_change SecSince, prev_SecSince; // Time in seconds since the last battery change
        avSpeed_avMileage SpdMileage, prev_SpdMileage; // Average speed and mileage data
        Brake brake, prev_brake; // Brake status and data

        //======================= Vehicle Control and Status ===========================//

        odo_range_avFuel OdoRange, prev_OdoRange; // Odometer reading, range, and average fuel usage
        uint8_t DSC, prev_DSC; // DSC (Dynamic Stability Control) status (enabled/disabled)
        Speed Spd, prev_Spd; // Speed data (current speed of the vehicle)
        uint16_t steeringwheelbuttons, prev_steeringwheelbuttons; // Steering wheel button press status
        uint8_t Seat_heating_buttonDriver, prev_Seat_heating_buttonDriver; // Driver seat heating button status
        uint8_t Seat_heating_buttonPassanger, prev_Seat_heating_buttonPassanger; // Passenger seat heating button status
        uint8_t button, prev_button; // Generic button press status (e.g., mode buttons)
        uint8_t Indicator_lever, prev_Indicator_lever; // Indicator lever position (left/right)
        Voltage_EngineState VoltengSTA, prev_VoltengSTA; // Voltage and engine state information
        uint8_t gear_lever, prev_gear_lever; // Gear lever position (P/R/N/D)
        uint8_t Indicator_Status, prev_Indicator_Status; // Indicator status (on/off)
        uint8_t Dimmer_Value, prev_Dimmer_Value; // Dimmer (brightness) value for internal lighting
        uint8_t PDC_Status, prev_PDC_Status; // Park Distance Control status (active/inactive)
        uint8_t Inside_Lights, prev_Inside_Lights; // Inside lights status (on/off)
        float Outside_Temp, prev_Outside_Temp; // Current outside temperature
        uint8_t Front_Whiper_direction, prev_Front_Whiper_direction; // Direction of the front wipers (e.g., intermittent)
        uint8_t Key_Button, prev_Key_Button, inserted, prev_inserted; // Key button press status and key insertion status
        uint16_t Vin, prev_Vin; // Vehicle Identification Number (VIN)
        float InternalTemp, prev_InternalTemp; // Internal temperature of the vehicle cabin
        DateTime_ Time, prev_Time; // Date and time data from the vehicle system

        uint8_t CLutch, prev_CLutch;
        bool Handbrake = true,prev_Handbrake = false;
        String keyAction, prev_keyAction;

        friend class SecurityH;
        
        

public:
    uint8_t PressCount;
    uint8_t MessCount;
    void ReadMessages(); // Function to read incoming CAN messages
    uint8_t KL15_stat, prev_KL15_stat; // k;l15
    bool lockFLag;

    //===================== Constructor ============================================//

    BMW_E60_CAN(int intPin, MCP_CAN *can); // Constructor with interrupt pin and CAN object

    //==================== Initialization and Setup ================================//

    bool begin(); // Initialize the CAN bus communication and peripherals

    //==================== Message Processing ================================//

    void processMessages(); // Process the incoming CAN messages and update vehicle data

    //==================== Getter Functions ================================//

    double GetTorque();                          // Get current torque value
    double GetRPM();                             // Get current RPM value
    uint8_t GetThrottle();                       // Get throttle position
    uint8_t GetAbsAlive();                       // Get ABS alive signal
    float GetStrAngle();                         // Get steering angle
    float GetIcrStrAngle();                      // Get incremental steering angle
    WheelSpeeds GetWheelSpeeds();                // Get wheel speed data
    uint8_t GetAirBgAlive();                     // Get airbag alive signal
    DoorStatus GetDoorStatus(DoorStatus dor);    // Get specific door status (passenger, driver, etc.)
    BootStatus GetBootStatus();                  // Get boot (trunk) status
    Brake GetBrake();                            // Get brake system status
    Speed GetSpeed();                            // Get current vehicle speed
    PDC_Sensordata GetPDC();                     // Get PDC sensor data
    Engine_Temp GetEngineTemp();                 // Get engine temperature data
    uint16_t Getsteeringwheelbuttons();          // Get steering wheel button press status
    uint8_t GetParam4(uint8_t type);             // Get 4-byte parameter based on type
    uint8_t GetParam8(uint8_t type, uint8_t index); // Get 8-byte parameter based on type and index
    Whiperlever GetWhiper();                    // Get wiper lever status
    DateTime_ GetTimeData();                    // Get date and time data
    Door_Status GetBoolDorStatus();             // Get boolean door status for all doors
    Seconds_since_bat_change GetSecSince();     // Get seconds since battery change
    odo_range_avFuel GetOdoRange();             // Get odometer, range, and average fuel data
    avSpeed_avMileage GetAvSpdMil();            // Get average speed and mileage
    Outside_Temp_and_Range GetOUtempRang();     // Get outside temperature and range
    uint16_t GetVin();                          // Get VIN (Vehicle Identification Number)
    Voltage_EngineState GetVoltEnSTA();         // Get voltage and engine state status
    uint8_t GetClutch();         // Get voltage and engine state status
    bool GetHandbrake();
    uint8_t GetKl15();
    // ---- Small helpers ----
    static inline void hx2(uint8_t v){ if(v<16) Serial.print('0'); Serial.print(v, HEX); }
    static inline void id3(uint16_t id){
    if(id<0x100) Serial.print('0');
    if(id<0x10)  Serial.print('0');
    Serial.print(id, HEX);
   };

    void printFrame(uint16_t id, uint8_t len, const uint8_t *d);
    void armReplyWait(const uint16_t *ids, uint8_t n, uint32_t timeout_ms, const char* label);
    static void clearReplyWait(){ gWait.active = false; };
    void checkReplyTimeout();
    void decodeKombiTime(const uint8_t *b, uint8_t len);
    void sendAndPrint(uint16_t id, uint8_t len, const uint8_t *data);
    String readLine();
    long parseDec(const String& s, bool &ok){ char*endptr=nullptr; long v=strtol(s.c_str(),&endptr,10); ok=(endptr!=s.c_str()); return v; }
    void cmd_help();
    void printStatus();
    // ---- triggers ----
 void do_cas_reset(){
  Serial.println(F("# CAS reset")); sendAndPrint(0x6F1, sizeof(CAS_RESET_1), CAS_RESET_1); delay(40); sendAndPrint(0x6F1, sizeof(CAS_RESET_2), CAS_RESET_2); delay(40);
  const uint16_t expect[]={0x640}; armReplyWait(expect,1,600,"CAS reset"); }
 void do_defrost_on(){
  Serial.println(F("# Defrost ON")); sendAndPrint(0x6F1, sizeof(DEFROST_ON_1), DEFROST_ON_1); delay(30); sendAndPrint(0x6F1, sizeof(DEFROST_ON_2), DEFROST_ON_2); delay(30); sendAndPrint(0x6F1, sizeof(DEFROST_ON_3), DEFROST_ON_3); delay(30);
  const uint16_t expect[]={0x678}; armReplyWait(expect,1,600,"IHKA defrost_on"); }
 void do_defrost_off(){
  Serial.println(F("# Defrost OFF")); sendAndPrint(0x6F1, sizeof(DEFROST_OFF_1), DEFROST_OFF_1); delay(30);
  const uint16_t expect[]={0x678}; armReplyWait(expect,1,600,"IHKA defrost_off"); }
 void do_blower_on(){
  Serial.println(F("# Blower ON")); sendAndPrint(0x6F1, sizeof(BLOWER_ON_1), BLOWER_ON_1); delay(30); sendAndPrint(0x6F1, sizeof(BLOWER_ON_2), BLOWER_ON_2); delay(30); sendAndPrint(0x6F1, sizeof(BLOWER_ON_3), BLOWER_ON_3); delay(30); sendAndPrint(0x2F0, sizeof(BLOWER_ON_4), BLOWER_ON_4); delay(30);
  const uint16_t expect[]={0x678}; armReplyWait(expect,1,600,"IHKA blower_on"); }
 void do_blower_off(){
  Serial.println(F("# Blower OFF")); sendAndPrint(0x6F1, sizeof(BLOWER_OFF_1), BLOWER_OFF_1); delay(30); sendAndPrint(0x2F0, sizeof(BLOWER_OFF_2), BLOWER_OFF_2); delay(30);
  const uint16_t expect[]={0x678}; armReplyWait(expect,1,600,"IHKA blower_off"); }
 void do_ac_on(){
  Serial.println(F("# AC clutch ON")); sendAndPrint(0x6F1, sizeof(AC_ON_1), AC_ON_1); delay(30); const uint16_t expect[]={0x678}; armReplyWait(expect,1,600,"IHKA ac_on"); }
 void do_ac_off(){
  Serial.println(F("# AC clutch OFF")); sendAndPrint(0x6F1, sizeof(AC_OFF_1), AC_OFF_1); delay(30); const uint16_t expect[]={0x678}; armReplyWait(expect,1,600,"IHKA ac_off"); }
 void do_rdc_init_full(){
  Serial.println(F("# RDC init full sequence"));
  const uint8_t* F_list[] = {RDC_INIT_FULL_F1, RDC_INIT_FULL_F2};
  const size_t F_sizes[] = {sizeof(RDC_INIT_FULL_F1), sizeof(RDC_INIT_FULL_F2)};
  for(size_t i=0;i<2;i++){ sendAndPrint(0x5A9, F_sizes[i], F_list[i]); delay(45); }
  sendAndPrint(0x7C3, sizeof(RDC_INIT_FULL_X1), RDC_INIT_FULL_X1); delay(45);
  sendAndPrint(0x336, sizeof(RDC_INIT_FULL_X2), RDC_INIT_FULL_X2); delay(45);
  const uint16_t expect[]={0x5AA,0x7C4,0x336}; armReplyWait(expect,3,1200,"RDC init"); }

  void handleCommand(const String &line);
  void handleRx();
};

#endif // BMW_E60_CAN_H
