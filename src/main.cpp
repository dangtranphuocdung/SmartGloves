#include <Wire.h>    //esp32 SDA=21,SCL=22
#include <BleMouse.h>
#include <Arduino.h>
#include "mouse-control.h"

void setup() {
  setupMouse();


}
void loop() {
  loopMouse();

  //
}