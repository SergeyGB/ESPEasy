/*
 * ADCKeyb.h
 *
 *      Author: s
 */

#ifndef ADCKEYB_H_
#define ADCKEYB_H_

#include <Arduino.h>
//#include <common.h>

#define none_b K0_b

#define state_b K1_b
#define up_b	K2_b
#define down_b  K3_b
#define gerc_b  K4_b

#define QMAX 5

 enum button_t  {
     K0_b,    // не нажата
     K1_b,
     K2_b,
     K3_b,
     K4_b,  // самая близкая к земле
	KN_b
  };

 enum button_mess  {
     bm_empty,    // не нажата
     bm_press,
     bm_lpress,
     bm_dpress,
     bm_release
  };

struct keymes {   
    uint8_t key : 3; // 0b111 =  empty  
    uint8_t f : 3;   // 0 - empty; 1 press, 2 long press, 3 doble press; 4 release
}; 

typedef void (*OnAdcButton_f)(uint8_t);

class ADCKeybClass {
private:
     keymes queue[QMAX];
     uint8_t queuel=0;
     uint8_t Key[KN_b] = {0,0,0,0,0};
     uint8_t Prev    = K0_b;
     uint8_t Current = K0_b;
     uint8_t LastRelease = K0_b;
     ulong ReleaseTime = 0;
	 ulong ChangeTo_time  = 0;    // когда нажали
     ulong PressR_time = 0;   // когда Repeat
     ulong pooltime = 0;   // через сколько  ms опрашивать

     //ulong PressD_time = 0;   //
     ulong PressR_wait = 1000;   //
     OnAdcButton_f OnPressFunc = NULL;
     OnAdcButton_f OnDoublePressFunc = NULL;
     OnAdcButton_f OnLongPressFunc = NULL;
     OnAdcButton_f OnReleaseFunc = NULL;
     uint8_t debug=0;
     int a0=0;

public:
	 ADCKeybClass();
     void Pool();
     void ChangeTo(uint8_t);
     void Press(uint8_t);
     void LongPress(uint8_t);
     void DoublePress(uint8_t);
     void Release(uint8_t);
     void SetOnPress(OnAdcButton_f);
     void SetOnDoublePress(OnAdcButton_f);
     void SetOnLongPress(OnAdcButton_f);
     void SetOnRelease(OnAdcButton_f);
     uint8_t GetKey();
     int GetA0() { return a0; };
     bool queue_put(keymes p);
     bool queue_notempty();
     keymes queue_get();
};

#endif /* ADCKEYB_H_ */
