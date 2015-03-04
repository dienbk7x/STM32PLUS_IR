#include "config/stm32plus.h"
#include "config/gpio.h"
#include "config/timing.h"
#include "config/timer.h"
#include "config/debug.h"
#include "config/exti.h"

#include "IR.h"


//por um vector a C com 40 posiçoes...

using namespace stm32plus;


/*
 * Main entry point
 */

int main() {

shost << "\fSTART\n";
  // set up SysTick at 1ms resolution
  MillisecondTimer::initialise();

  GpioA<DefaultDigitalOutputFeature<5> > LED;
//  GpioC<DefaultDigitalInputFeature<13> > B;

 Nvic::initialise();

 IR _ir;
 IR::IR_PACKAGE IR_CODE;
 //The interrupts are enable, so it will capture any package

while(1){
     //lets wait for the complete package
     while(!_ir.IsReadComplete()){
        asm("nop");
     }

    // _ir.Semihost_print_pck();

     _ir.decode(&IR_CODE);

     if(IR_CODE.code==SAMSUNG_KEY_1)
         LED[5].setState(!LED[5].read() );

}

  // not reached
  return 0;
}
