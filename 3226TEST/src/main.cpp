#include <Arduino.h>

// put function declarations here:
#define lock 10
#define drain 11
#define inlet1 12
#define inlet2 13


void setup() {
  // put your setup code here, to run once:
  pinMode(lock,OUTPUT);
  pinMode(drain,OUTPUT);
  pinMode(inlet1,OUTPUT);
  pinMode(inlet2,OUTPUT);
  
}
void loop() {
  // put your main code here, to run repeatedly:
digitalWrite(lock,HIGH);
digitalWrite(drain,HIGH);
digitalWrite(inlet1,HIGH);
digitalWrite(inlet2,HIGH);
delay(1000);
digitalWrite(lock,LOW);
digitalWrite(drain,LOW);
digitalWrite(inlet1,LOW);
digitalWrite(inlet2,LOW);
delay(1000);
}





//10-13