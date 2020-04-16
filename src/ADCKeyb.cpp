/*
 * ADCKeyb.cpp
 */
#include "ADCKeyb.h"
//#include "common.h"
#include "ESPEasy_common.h"
#include "_Plugin_Helper.h"

#define ADCKeyb_Log 0

bool ADCKeybClass::queue_put(keymes p){
  if( queuel < QMAX-1) {
    queue[queuel++]=p;
    #if ADCKeyb_Log > 0  
    String log = F("BSG key p: ");
    for(uint8  i = 0; i < queuel; i++) {
         log += String(queue[i].key);
         log += ":";
         log += String(queue[i].f);
         log += ";  ";
      }
    addLog(LOG_LEVEL_INFO, log);
	#endif
	return true;
  }  else
     return false;
}

bool ADCKeybClass::queue_notempty(){
  if(  queuel )    return true;
  else  return false;
}
keymes ADCKeybClass::queue_get(){
  keymes t;	
  t.key = 0b111;
  t.f = 0;
  if(queue_notempty()) {
	 t = queue[0];
	 #if ADCKeyb_Log > 0  
     String log = F("BSG key g:");
     log += String(t.key);
     log += "|";
     log += String(t.f);
     log += "# ";
	 #endif
  	 for(uint8_t  i = 0; i < queuel; i++) {
         queue[i] = queue[i+1];
     }
	 queuel--;
	 #if ADCKeyb_Log > 0  
     for(uint8_t  i = 0; i < queuel; i++) {
         log += String(queue[i].key);
         log += ":";
         log += String(queue[i].f);
         log += ";  ";
      }
     addLog(LOG_LEVEL_INFO, log);
	 #endif
  };
  return t;
}

ADCKeybClass::ADCKeybClass(){
}
void ADCKeybClass::SetOnPress(OnAdcButton_f f) {
	OnPressFunc=f;
}
void ADCKeybClass::SetOnDoublePress(OnAdcButton_f f) {
	OnDoublePressFunc=f;
}
void ADCKeybClass::SetOnLongPress(OnAdcButton_f f) {
	OnLongPressFunc=f;
}
void ADCKeybClass::SetOnRelease(OnAdcButton_f f) {
	OnReleaseFunc=f;
}
void ADCKeybClass::Pool() {
	  if (millis() - pooltime > 100) {
		  pooltime = millis();
	  	  a0 = analogRead(A0);
	  	  if (a0 > 861) { ChangeTo(K0_b); }
	  	  else if (a0 > 615) { ChangeTo(K1_b); }
	  	  else if (a0 > 371) { ChangeTo(K2_b); }
	  	  else if (a0 > 125) { ChangeTo(K3_b); }
	  	  else { ChangeTo(K4_b); }
	  	  if ( (Current > K0_b) && ( ( millis() - PressR_time) > PressR_wait ) ) {
	  	  	  LongPress(Current);
	  	  	  PressR_time = millis();
	  	  }
	  }
}
uint8_t ADCKeybClass::GetKey(){
	return Current;
}
void ADCKeybClass::ChangeTo(uint8_t b) {
	Current = b;
	if ( Prev != Current ) {
		ChangeTo_time  = millis();
		PressR_time = ChangeTo_time;
		if (Current > Prev) {
			Key[Current] = 1;
			Press(Current);
		} else {  // Prev > Current (Release)
			for (uint8_t i=Prev;i>Current;i--) {
				if (Key[i]) {
					Key[i]=0;
					Release(i);
				}
			}
		}
		Prev = Current;
	}
}
void ADCKeybClass::Press(uint8_t b) {
	if ( ( LastRelease == b ) && ( (millis() - ReleaseTime) < 500 ) ) {
		DoublePress(b);
	} else {
		keymes t;
		t.key = b;
		t.f = bm_press;
		queue_put(t);
		//if (debug) Serialprintln("P=%d [%d,%d,%d,%d,%d]",b,Key[0],Key[1],Key[2],Key[3],Key[4]);
		if (OnPressFunc != NULL) OnPressFunc(b);
	}
}

void ADCKeybClass::Release(uint8_t b) {
	//if (debug) Serialprintln("R=%d [%d,%d,%d,%d,%d]",b,Key[0],Key[1],Key[2],Key[3],Key[4]);
	keymes t;
	t.key = b;
	t.f = bm_release;
	queue_put(t);
	LastRelease = b;
	ReleaseTime = millis();
	if (OnReleaseFunc != NULL) OnReleaseFunc(b);
}

void ADCKeybClass::LongPress(uint8_t b) {
	//if (debug) Serialprintln("L=%d [%d,%d,%d,%d,%d]",b,Key[0],Key[1],Key[2],Key[3],Key[4]);
	keymes t;
	t.key = b;
	t.f = bm_lpress;
	queue_put(t);
	if (OnLongPressFunc != NULL) OnLongPressFunc(b);
}

void ADCKeybClass::DoublePress(uint8_t b) {
	//if (debug) Serialprintln("D=%d [%d,%d,%d,%d,%d]",b,Key[0],Key[1],Key[2],Key[3],Key[4]);
	keymes t;
	t.key = b;
	t.f = bm_dpress;
	queue_put(t);
	if (OnDoublePressFunc != NULL) OnDoublePressFunc(b);
}

//ADCKeybClass AdcKeyb;
