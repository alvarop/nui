#include <IRremote.h>
#include <SPI.h>  // include the SPI library:

int MUTE_PIN = 9;
int RECV_PIN = 21;
int CS_PIN = 10;
int ZC_EN = 8;
IRrecv irrecv(RECV_PIN);
decode_results results;

uint8_t volume = 128;
uint8_t mute = 1;

void setup()
{
  Serial.begin(9600);
  irrecv.enableIRIn(); // Start the receiver
  Serial.println("Started");
  pinMode(MUTE_PIN, OUTPUT);
  digitalWrite(MUTE_PIN, 1);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  pinMode(ZC_EN, OUTPUT);
  digitalWrite(ZC_EN, 1);
  SPI.begin();
  SPI.setSCK(14);

  Serial.printf("Volume: %d\n", volume);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(volume);
  SPI.transfer(volume);
  digitalWrite(CS_PIN, HIGH);
}


typedef enum {
  AUX_VOL_DOWN = 0x4BB6C03F,
  AUX_VOL_UP = 0x4BB640BF,
  AUX_MUTE = 0x4BB6A05F
} commands_t;
#define DOWN 0x4BB6C03F

void loop() {

  if (irrecv.decode(&results)) {

    switch (results.value) {
      case AUX_VOL_DOWN:
        {
          Serial.println("Volume Down!");
          if (volume > 0) {
            volume--;
          }
          break;
        }
      case AUX_VOL_UP:
        {
          Serial.println("Volume Up!");
          if (volume < 255) {
            volume++;
          }
          break;
        }
      case AUX_MUTE:
        {
          Serial.println("Mute!");
          mute ^= 1;
          digitalWrite(MUTE_PIN, mute);
          break;
        }
      default:
        {
          Serial.printf("Unknown command: 0x%08X\n", results.value);
        }

    }
    irrecv.resume(); // Receive the next value
    Serial.printf("Volume: %d\n", volume);
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(volume);
    SPI.transfer(volume);
    digitalWrite(CS_PIN, HIGH);
  }
}
