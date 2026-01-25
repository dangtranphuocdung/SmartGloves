#include "mouse-control.h"
#include <Wire.h>
#include <BleMouse.h>

BleMouse bleMouse;

#define MPU6050_ADDR         0x68
#define MPU6050_SMPLRT_DIV   0x19
#define MPU6050_CONFIG       0x1a
#define MPU6050_GYRO_CONFIG  0x1b
#define MPU6050_ACCEL_CONFIG 0x1c
#define MPU6050_WHO_AM_I     0x75
#define MPU6050_PWR_MGMT_1   0x6b

// Nút bấm
#define LEFT_BTN 16
#define RIGHT_BTN 17
#define SCROLL_BTN 5

double offsetX = 0, offsetY = 0, offsetZ = 0;
float dpsX, dpsY, dpsZ;

// Độ nhạy (Giữ nguyên 15 như ông chỉnh là ổn)
float x_kand = 0.5;
float y_kand = 0.4;
float sc_kand = 0.5; // Tăng lên xíu cho cuộn nhanh hơn
float offsety = 1.5; // Cái này lát chạy thử nếu trôi lên xuống thì chỉnh lại

int i1 = 0, i2 = 0;

void calcRotation(){
  int16_t raw_gyro_x, raw_gyro_y, raw_gyro_z;
  
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x43); // Đọc từ thanh ghi Gyro (đỡ đọc thừa Accelerometer cho nhanh)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 6, true);
  
  raw_gyro_x = Wire.read() << 8 | Wire.read();
  raw_gyro_y = Wire.read() << 8 | Wire.read();
  raw_gyro_z = Wire.read() << 8 | Wire.read();

  dpsX = ((float)raw_gyro_x) / 65.5;
  dpsY = ((float)raw_gyro_y) / 65.5;
  dpsZ = ((float)raw_gyro_z) / 65.5;
}

void writeMPU6050(byte reg, byte data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);   
  Wire.write(data);
  Wire.endTransmission();
}

byte readMPU6050(byte reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission(true);
  Wire.requestFrom(MPU6050_ADDR, 1); 
  byte data =  Wire.read();
  return data;
}

void setupMouse() {
  // SỬA 1: Dùng INPUT_PULLUP để chống nhiễu, chống treo máy
  pinMode(LEFT_BTN, INPUT_PULLUP);
  pinMode(RIGHT_BTN, INPUT_PULLUP);
  pinMode(SCROLL_BTN, INPUT_PULLUP); // Thêm chân 5
  
  Wire.begin();
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B); 
  Wire.write(0); 
  Wire.endTransmission(true);
  
  Serial.begin(115200); // Nhớ chỉnh Monitor là 115200 nhé
  bleMouse.begin();
  
  delay(100);
  
  if (readMPU6050(MPU6050_WHO_AM_I) != 0x68) {
    Serial.println("\nWHO_AM_I error.");
    while (true);
  }

  writeMPU6050(MPU6050_SMPLRT_DIV, 0x00);
  writeMPU6050(MPU6050_CONFIG, 0x00);
  writeMPU6050(MPU6050_GYRO_CONFIG, 0x08);
  writeMPU6050(MPU6050_ACCEL_CONFIG, 0x00);
  writeMPU6050(MPU6050_PWR_MGMT_1, 0x01);

  Serial.print("Dang can chinh...");
  // Giảm xuống 1000 lần cho nhanh (3000 hơi lâu)
  for(int i = 0; i < 1000; i++){
    calcRotation();
    offsetX += dpsX;
    offsetY += dpsY;
    offsetZ += dpsZ;
    delay(2); // Thêm delay nhỏ để đọc chính xác hơn
  }          
  Serial.println(" Xong!");

  offsetX /= 1000;
  offsetY /= 1000;
  offsetZ /= 1000;
}

void loopMouse() {
  calcRotation();

  if (Serial.available()) {
    // Đọc số ông gõ từ bàn phím
    float input = Serial.parseFloat(); 
    
    // Nếu gõ số khác 0 (tránh trường hợp gõ nhầm Enter)
    if (input != 0.0) {
        offsety = input; // Cập nhật ngay lập tức
        Serial.print("--> Da doi OFFSET Y thanh: ");
        Serial.println(offsety);
    }
    // Xóa bộ nhớ đệm để không đọc trùng
    while(Serial.available()) Serial.read(); 
  }
  // ---------------------------------------------------------

  // 3. In ra để ông theo dõi xem nó còn trôi không
  // Dòng này giúp ông nhìn xem Y_move có về 0 chưa
  Serial.print("Y_move: "); 
  Serial.print((dpsY - offsety) * y_kand); 
  Serial.print(" | Offset hien tai: ");
  Serial.println(offsety);

  // In ra để debug (Nhớ bật Serial Plotter xem sóng cho sướng)
  Serial.print("X_move:"); Serial.print(-(dpsZ - offsetZ)*x_kand);
  Serial.print("\tY_move:"); Serial.println((dpsY - offsety)*y_kand);

  if(bleMouse.isConnected()) {     
    
    // SỬA 2: Bỏ comment dòng này thì chuột mới chạy!
    // Trừ đi offsetZ để nó không bị trôi ngang
    bleMouse.move(-(dpsZ - offsetZ)*x_kand, (dpsY - offsety)*y_kand, 0);

    // Click trái
    if(digitalRead(LEFT_BTN)==0){ // 0 là nhấn (vì dùng PULLUP)
        if(i1 == 0){ // Chỉ bấm 1 lần
           bleMouse.press();
           i1=1;
           delay(20); // Chống rung
        }
    }
    else if(i1==1){
        bleMouse.release();
        i1=0;
    }

    // Click phải
    if(digitalRead(RIGHT_BTN)==0){
        if(i2 == 0){
           bleMouse.press(MOUSE_RIGHT);
           i2=1;
           delay(20);
        }
    }
    else if(i2==1){
        bleMouse.release(MOUSE_RIGHT);
        i2=0;
    }

    // Cuộn (Dùng chân 5)
    // Logic: Giữ nút 5 và nghiêng chuột lên xuống để cuộn
    if(digitalRead(SCROLL_BTN)==0){
        bleMouse.move(0,0, (dpsY - offsety) * sc_kand);            
    }
  }
  
  // SỬA 3: Giữ delay 20 để không bị lock máy
  delay(20);
}