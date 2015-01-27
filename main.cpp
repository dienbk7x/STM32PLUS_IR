#include "config/stm32plus.h"
#include "config/gpio.h"
#include "config/timing.h"
#include "config/timer.h"
#include "config/debug.h"
#include "config/exti.h"
#include <vector>

#define SAMSUNG_N_BITS 32

#define SAMSUNG_START_MIN 400 //(450-50)
#define SAMSUNG_START_MAX 500 //(450+50)

#define SAMSUNG_0_HIGH_MIN 45
#define SAMSUNG_0_HIGH_MAX 68
#define SAMSUNG_0_LOW_MIN SAMSUNG_0_HIGH_MIN
#define SAMSUNG_0_LOW_MAX SAMSUNG_0_HIGH_MAX

#define SAMSUNG_1_HIGH_MIN 45
#define SAMSUNG_1_HIGH_MAX 68
#define SAMSUNG_1_LOW_MIN 150
#define SAMSUNG_1_LOW_MAX 180

#define SAMSUNG_STOP_BIT false


//por um vector a C com 40 posiçoes...

using namespace stm32plus;

class IR{
private:
    bool complete_read;
    bool first_read;

protected:
    enum{
        IR_IN=6,
        IR_OUT=0
    };

    Timer3<
        Timer3InternalClockFeature,
        Timer3InterruptFeature            // gain access to interrupt functionality
    > timer;
    //    Timer3<Timer3InternalClockFeature> timer;

    //configure the GPIOA6 as an input with a ext interrupt
    GpioA<DefaultDigitalInputFeature<IR_IN> > pa; //pa6
    Exti6 *exti;






public:

    typedef struct{
        uint32_t on;
        uint32_t off;
    }_pulse;

    typedef struct{
        std::vector<_pulse> pulses;
        uint32_t total_time;
    }_ir_pulses;

    enum Protocol{
        RAW,
        RC5,
        SAMSUNG,
        NEC,
        SIRCS
    };

    struct IR_PACKAGE{
        Protocol protocol;
        std::vector<_pulse> *pulses;
        uint32_t code;
    };

    _ir_pulses ir_pulses;

    IR(){
        complete_read=false;
        first_read=true;  //inform the IRQ that the next signal is the start

        ir_pulses.pulses.reserve(20); //reserve space for 20 pulses(on and off)

        //init a timer. This will be Timer3, hooping to use its ability to do the pwm input
        timer.setTimeBaseByFrequency(100000,20000);   //tick at 10000Hz |1 tick=10us with an auto-reload value of 2000= 20ms
                                                    //when the counter reaches the auto-reload value it triggers an interrupt that will stop the capture.

        //configure the GPIOA6 as an input with a ext interrupt
        exti=new Exti6(EXTI_Mode_Interrupt,EXTI_Trigger_Rising_Falling,pa[IR_IN]);
        exti->ExtiInterruptEventSender.insertSubscriber(ExtiInterruptEventSourceSlot::bind(this,&IR::Exti_onInterrupt));

        //also configure the auto-reload interrupt event
        timer.TimerInterruptEventSender.insertSubscriber(TimerInterruptEventSourceSlot::bind(this,&IR::Timer_onInterrupt));

        //clear any pending interrupts, stop and reset the counter and enable them.
        timer.clearPendingInterruptsFlag(TIM_IT_Update);
        timer.disablePeripheral();
        timer.setCounter(0);
        timer.enableInterrupts(TIM_IT_Update);

//    while(1){
//    asm("nop");
//    }

    }

    void decode(void){
        struct PROT_TIME{
            uint16_t One_high_max;
            uint16_t One_high_min;
            uint16_t One_low_max;
            uint16_t One_low_min;
            uint16_t Zero_high_max;
            uint16_t Zero_high_min;
            uint16_t Zero_low_max;
            uint16_t Zero_low_min;
            bool stop_bit;
            uint8_t lenght;
        };

        IR_PACKAGE ir_pck;
        PROT_TIME protocol_time;
        uint32_t code;
        bool decode_ok;

        if(SAMSUNG_START_MIN<=ir_pulses.pulses[0].on && ir_pulses.pulses[0].on <= SAMSUNG_START_MAX){
                shost<<"SAMSUNG\n";
                ir_pck.protocol=SAMSUNG;
                protocol_time.lenght=SAMSUNG_N_BITS;

                protocol_time.One_high_max=SAMSUNG_1_HIGH_MAX;
                protocol_time.One_high_min=SAMSUNG_1_HIGH_MIN;
                protocol_time.One_low_max=SAMSUNG_1_LOW_MAX;
                protocol_time.One_low_min=SAMSUNG_1_LOW_MIN;

                protocol_time.Zero_high_max=SAMSUNG_0_HIGH_MAX;
                protocol_time.Zero_high_min=SAMSUNG_0_HIGH_MIN;
                protocol_time.Zero_low_max=SAMSUNG_0_LOW_MAX;
                protocol_time.Zero_low_min=SAMSUNG_0_LOW_MIN;

                protocol_time.stop_bit=SAMSUNG_STOP_BIT;
        }
        decode_ok=false;
        code=0;
        //_pulse test;

        for (uint16_t i=1; i< ir_pulses.pulses.size(); i++){
              //  test.on=ir_pulses.pulses[i].on;
              //  test.off=ir_pulses.pulses[i].off;
                if( (protocol_time.One_high_min<=ir_pulses.pulses[i].on && ir_pulses.pulses[i].on <= protocol_time.One_high_max) &&
                    (protocol_time.One_low_min<=ir_pulses.pulses[i].off && ir_pulses.pulses[i].off <= protocol_time.One_low_max) ){
                            code|=1<<(i-1);
                    }
                else if( (protocol_time.Zero_high_min<=ir_pulses.pulses[i].on && ir_pulses.pulses[i].on <= protocol_time.Zero_high_max) &&
                    (protocol_time.Zero_low_min<=ir_pulses.pulses[i].off && ir_pulses.pulses[i].off <= protocol_time.Zero_low_max) ){
                            code|=0<<(i-1);
                            if (i==protocol_time.lenght+1){
                                decode_ok=true;
                                break;
                            }

                    }

        }
        shost<<"IR DECODE="<<decode_ok<<"\n";
        shost<<code;
        ir_pck.code=code;
        return;



    }

    void Semihost_print_pck(void){

        for(uint16_t i=0; i< ir_pulses.pulses.size(); i++){
                shost<< "OFF: "<< ir_pulses.pulses[i].off<<" ON : "<<  ir_pulses.pulses[i].on<<"\n";
        }

        shost<<"\n\n";

    }




    bool IsReadComplete(void){
        if(complete_read){
            complete_read=0;
            return(1);
        }
        return(0);
    }

    void Exti_onInterrupt(uint8_t /* extiLine */) {
      static _pulse ThisPulse;
      //uint16_t counter;

      if(!pa[IR_IN].read()){            //off - Falling edge
          if(!first_read){
            timer.disablePeripheral();
            ThisPulse.off=timer.getCounter(); //the signal is inverted by the receptor
            timer.setCounter(0);
            ir_pulses.pulses.push_back(ThisPulse);
            ThisPulse.on=ThisPulse.off=0;
          }
            timer.enablePeripheral();
            first_read=false;
            complete_read=false;
      }else{                           //on - Rissing edge
        timer.disablePeripheral();
        ThisPulse.on = timer.getCounter(); //the signal is inverted by the receptor
        timer.setCounter(0);
        timer.enablePeripheral();
      }
    }

    void Timer_onInterrupt(TimerEventType tet,uint8_t /* timerNumber */) {

      if(tet==TimerEventType::EVENT_UPDATE) {
            timer.disablePeripheral();
            timer.setCounter(0);
            complete_read=true; //we finish the read.
            first_read=true; //prepare for the next one;
            shost<<"TimeOUT\n";
      }
    }

};



/*
 * Main entry point
 */

int main() {

shost << "\fSTART\n";
  // set up SysTick at 1ms resolution
  MillisecondTimer::initialise();

//  GpioA<DefaultDigitalOutputFeature<5> > LED;
//  GpioC<DefaultDigitalInputFeature<13> > B;

 Nvic::initialise();

 IR _ir;
 //The interrupts are enable, so it will capture any package

while(1){
 //lets wait for the complete package
 while(!_ir.IsReadComplete()){
    asm("nop");
 }

// _ir.Semihost_print_pck();

 _ir.decode();

}

  // not reached
  return 0;
}
