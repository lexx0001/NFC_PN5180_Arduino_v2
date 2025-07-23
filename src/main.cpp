// NAME: main.cpp
//
// DESC: Use the PN5180-NFC Module from NXP Semiconductors.
//
// BEWARE: SPI with an Arduino to a PN5180 module has to be at a level of 3.3V
// use of logic-level converters from 5V->3.3V is absolutly neccessary
// on most Arduinos for all input pins of PN5180!
// If used with an ESP-32, there is no need for a logic-level converter, since
// it operates on 3.3V already.
//
// Arduino <-> Level Converter <-> PN5180 pin mapping:
// 5V             <-->             5V
// 3.3V           <-->             3.3V
// GND            <-->             GND
// 5V      <-> HV
// GND     <-> GND (HV)
//             LV              <-> 3.3V
//             GND (LV)        <-> GND
// SCLK,13 <-> HV1 - LV1       --> SCLK
// MISO,12        <---         <-- MISO
// MOSI,11 <-> HV3 - LV3       --> MOSI
// SS,10   <-> HV4 - LV4       --> NSS (=Not SS -> active LOW)
// BUSY,9         <---             BUSY
// Reset,7 <-> HV2 - LV2       --> RST

#include <PN5180.h>
#include <PN5180ISO14443.h>

#define PN5180_NSS 10
#define PN5180_BUSY 9
#define PN5180_RST 7

PN5180ISO14443 nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);
void printCardWorkInfo();
uint32_t irqStatus = 0;
// uint32_t loopCnt = 0;
bool errorFlag = false;

void setup()
{
  Serial.begin(9600);

  while (nfc.PN5180_Start() == false)
  {
    Serial.println(F("PN5180 not detected!"));
    Serial.println(F("Please check wiring and power supply!"));
    Serial.println(F("Restarting PN5180..."));
    Serial.flush();
    delay(900); // wait for a second before retrying
  }
  nfc.setupRF();
}

// ISO 14443 loop
void loop()
{
  if (errorFlag)
  {
    nfc.reset();
    nfc.setupRF();
    errorFlag = false;
    delay(10);
  }

  // Serial.println(F("------------------------------------------------"));
  // Serial.print(F("Loop #"));
  // Serial.println(loopCnt++);
  irqStatus = nfc.getIRQStatus();
  // nfc.showIRQStatus(irqStatus);

  if (irqStatus != 0x24007 && irqStatus != 0)
  {
    // Serial.println(F("Error: Unexpected IRQ status (not 0x24007 or 0)"));
    errorFlag = true;
    // delay(1000);
    return;
  }

  printCardWorkInfo();
  delay(2);
}

// Print card serial number, ATQA and SAK
void printCardWorkInfo()
{
  uint8_t uidLength;
  uint8_t buffer[10] = {0}; // 0-1: ATQA, 2: SAK, 3-9: UID
  uidLength = nfc.cardDetect(buffer);

  if (!uidLength)
  {
    // Serial.println(F("Incorrect UID length or no card detected!"));
    return;
  }

  // --- UID ---
  Serial.print(F("UID: "));
  for (int i = 3; i < 3 + uidLength; i++)
  {
    if (i > 3)
      Serial.print(":");
    char byteStr[4];
    snprintf(byteStr, sizeof(byteStr), "%02X", buffer[i]);
    Serial.print(byteStr);
  }
  Serial.println();

  // --- SAK ---
  char sakStr[12];
  snprintf(sakStr, sizeof(sakStr), "SAK: 0x%02X", buffer[2]);
  Serial.println(sakStr);

  // --- ATQA ---
  char atqaStr[16];
  snprintf(atqaStr, sizeof(atqaStr), "ATQA: 0x%02X%02X", buffer[1], buffer[0]); // порядок [1][0] = High:Low
  Serial.println(atqaStr);

  // --- Обработка в зависимости от SAK ---
  if (buffer[2] == 0x20)
  {
    Serial.println(F("SAK == 0x20, карта поддерживает APDU."));
    nfc.sendRATS();
  }
  else
  {
    // Serial.println(F("Это не APDU карта. Проверка на mifare_UL_EV1..."));

    // Проверка на mifare_UL_EV1 48 кБ. UID длина 7 байт, SAK = 0x00, ATQA = 0x0044
    if (uidLength == 7 && buffer[2] == 0x00 && buffer[0] == 0x44 && buffer[1] == 0x00)
    {
      Serial.println(F("Обнаружена mifare_UL_EV1 48 кБ!"));
    }
    else
    {
      Serial.println(F("Это не mifare_UL_EV1 по ATQA/SAK."));
      nfc.mifareHalt();
      Serial.println(F("------------------------------------------------"));
      delay(1000);
      return;
    }

    uint8_t versionData[8];
    if (nfc.mifare_UL_EV1_GetVersion(versionData))
    {
      // Проверяем, что это MIFARE Ultralight EV1 48 байт
      if (versionData[2] == 0x03 && versionData[4] == 0x01 && versionData[6] == 0x0B)
      {
        Serial.println(F("Подтверждена mifare_UL_EV1 48 кБ по версии!"));
        // Аутентификация PWD_AUTH
        // uint8_t password[4] = {0xD1, 0xF7, 0x34, 0x85}; //  твой пароль
        uint8_t password[4] = {0xFF, 0xFF, 0xFF, 0xFF}; //  пароль по умолчанию
        uint8_t pack_read[2];

        if (nfc.mifare_UL_EV1_PwdAuth(password, pack_read))
        {
          Serial.print(F("Аутентификация прошла успешно! PACK: "));
          Serial.print(pack_read[0], HEX);
          Serial.print(":");
          Serial.println(pack_read[1], HEX);
        }
        else
        {
          Serial.println(F("Аутентификация не удалась."));
          return;
        }
      }
      else
      {
        Serial.println(F("Это не mifare_UL_EV1 48 кБ по версии"));
      }
    }
    else
    {
      Serial.println(F("Не удалось получить версию метки"));
    }
  }

  // Завершаем сессию
  nfc.mifareHalt();
  Serial.println(F("------------------------------------------------"));
  delay(1300);
}
