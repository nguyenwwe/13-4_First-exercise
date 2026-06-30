// matth-x/MicroOcpp
// Copyright Matthias Akstaller 2019 - 2024
// MIT License
#include <SPI.h>
#include <RFID/MFRC522.h>

#define SS_PIN   13
#define RST_PIN  12

MFRC522 rfid(SS_PIN, RST_PIN);
#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti WiFiMulti;
#elif defined(ESP32)
#include <WiFi.h>
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

#include <MicroOcpp.h>

#define STASSID "HIPTECH_FPT"
#define STAPSK  "fpthiptech@2025"

#define OCPP_BACKEND_URL   "ws://192.168.100.136:8180/steve/websocket/CentralSystemService"
#define OCPP_CHARGE_BOX_ID "esp-charger"


float energyWh = 0;
float powerW = 2200;
unsigned long lastUpdate = 0;
//  Settings which worked for my SteVe instance:
//
//#define OCPP_BACKEND_URL   "ws://192.168.178.100:8180/steve/websocket/CentralSystemService"
//#define OCPP_CHARGE_BOX_ID "esp-charger"

void setup() {

    pinMode(4, OUTPUT);
    pinMode(23, INPUT_PULLUP);
    pinMode(22, INPUT_PULLUP);

    
    /*
    
     * Initialize Serial and WiFi
     */ 

    Serial.begin(115200);
    


    Serial.print(F("[main] Wait for WiFi: "));

#if defined(ESP8266)
    WiFiMulti.addAP(STASSID, STAPSK);
    while (WiFiMulti.run() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
#elif defined(ESP32)
    WiFi.begin(STASSID, STAPSK);
    while (!WiFi.isConnected()) {
        Serial.print('.');
        delay(1000);
    }
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

    Serial.println(F(" connected!"));

    /*
     * Initialize the OCPP library
     */
    mocpp_initialize(OCPP_BACKEND_URL, OCPP_CHARGE_BOX_ID, "Hiptech Charging Station", "Hiptech Solution Co,Ltd");

    /*
     * Integrate OCPP functionality. You can leave out the following part if your EVSE doesn't need it.
     */
    setEnergyMeterInput([]() {
        //take the energy register of the main electricity meter and return the value in watt-hours
        return (int)energyWh;
;
    });


    setSmartChargingCurrentOutput([](float limit) {
        //set the SAE J1772 Control Pilot value here
        Serial.printf("[main] Smart Charging allows maximum charge rate: %.0f\n", limit);
    });

    // setConnectorPluggedInput([]() {
    //     //return true if an EV is plugged to this EVSE
    //     return false;
    // });

    setEvReadyInput([]() {
        return true;
    });


    SPI.begin(14, 27, 26, SS_PIN);

    rfid.PCD_Init();

    Serial.println("RFID Ready");
    //... see MicroOcpp.h for more settings
}

void loop() {
    

    /*
     * Do all OCPP stuff (process WebSocket input, send recorded meter values to Central System, etc.)
     */
    mocpp_loop();

    // Plug an EV
    if (digitalRead(22) == LOW){
    setConnectorPluggedInput([]() {
        //return true if an EV is plugged to this EVSE
        return true;
    });}else{
        setConnectorPluggedInput([]() {
        //return false if an EV is unplugged to this EVSE
        return false;
    });
    }

   
    if (ocppPermitsCharge()) {
        unsigned long now = millis();
        float dt = (now - lastUpdate) / 1000.0; // seconds
        lastUpdate = now;

        energyWh += powerW * (dt / 3600.0); // Wh = W * hours
    } else {
        lastUpdate = millis();
    }


    /*
     * Energize EV plug if OCPP transaction is up and running
     */
    /*
     * Khai báo một biến tĩnh (static). Biến tĩnh sẽ không bị xóa khi hàm loop() chạy lại.
     * Ban đầu mặc định là false (tương đương với tắt sạc)
     */
    static bool lastChargingState = false; 

    /*
     * Lấy trạng thái cho phép sạc hiện tại từ thư viện OCPP
     */
    bool currentChargingState = ocppPermitsCharge();

    /*
     * KIỂM TRA: Nếu trạng thái hiện tại KHÁC với trạng thái ở chu kỳ trước (có sự thay đổi)
     */
    if (currentChargingState != lastChargingState) {
        
        if (currentChargingState == true) {
            // Trạng thái vừa chuyển từ TẮT sang BẬT
            Serial.println(F("\n================================"));
            Serial.println(F("[RELAY] >>> BẬT SẠC <<<"));
            Serial.println(F("[RELAY] Đã đóng rơ-le, bắt đầu cấp điện cho xe!"));
            Serial.println(F("================================\n"));
            
            digitalWrite(4, HIGH); // Bật rơ-le vật lý
        } 
        else {
            // Trạng thái vừa chuyển từ BẬT sang TẮT
            Serial.println(F("\n================================"));
            Serial.println(F("[RELAY] >>> TẮT SẠC <<<"));
            Serial.println(F("[RELAY] Ngắt rơ-le, dừng cấp điện!"));
            Serial.println(F("================================\n"));
            
            digitalWrite(4, LOW);  // Tắt rơ-le vật lý
        }

        // Cập nhật lại trạng thái cũ bằng trạng thái mới để không bị in lại ở vòng lặp sau
        lastChargingState = currentChargingState; 
    }


    /*
     * Use NFC reader to start and stop transactions
     */
    if ((rfid.PICC_IsNewCardPresent()) && (rfid.PICC_ReadCardSerial())){
        String idTag = "";
        for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10)
            idTag += "0";             // thêm số 0 phía trước nếu cần
        idTag += String(rfid.uid.uidByte[i], HEX);
        }    
        rfid.PICC_HaltA();          //Kết thúc giao tiếp với thẻ hiện tại, tránh đọc lặp liên tục
        rfid.PCD_StopCrypto1();     //Kết thúc phiên xác thực MIFARE và reset trạng thái RC522
        
    // if (/* RFID chip detected? */ digitalRead(23) == LOW) {
    //     delay(200); // Chống nhiễu nút bấm (Debounce)
        
    //     // Đợi cho đến khi nhả nút bấm ra để tránh việc lặp lệnh liên tục
    //     while(digitalRead(23) == LOW) { delay(10); }

    //     String idTag = "ESP_001"; //e.g. idTag = RFID.readIdTag();

        if (!getTransaction()) {
            //no transaction running or preparing. Begin a new transaction
            Serial.printf("[main] Begin Transaction with idTag %s\n", idTag.c_str());

            /*
             * Begin Transaction. The OCPP lib will prepare transaction by checking the Authorization
             * and listen to the ConnectorPlugged Input. When the Authorization succeeds and an EV
             * is plugged, the OCPP lib will send the StartTransaction
             */
            auto ret = beginTransaction(idTag.c_str());

//            bool ret = true;
            if (ret) {
                Serial.println(F("[main] Transaction initiated. OCPP lib will send a StartTransaction when" \
                                "ConnectorPlugged Input becomes true and if the Authorization succeeds"));
            } else {
                Serial.println(F("[main] No transaction initiated"));
            }

        } else {
            //Transaction already initiated. Check if to stop current Tx by RFID card
            if (idTag.equals(getTransactionIdTag())) {
                //card matches -> user can stop Tx
                Serial.println(F("[main] End transaction by RFID card"));

                endTransaction(idTag.c_str());
            } else {
                Serial.println(F("[main] Cannot end transaction by RFID card (different card?)"));
            }
        }
    }

    //... see MicroOcpp.h for more possibilities
}
