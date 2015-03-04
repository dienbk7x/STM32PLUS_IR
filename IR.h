#ifndef IR_H_INCLUDED
#define IR_H_INCLUDED

#include "config/stm32plus.h"
#include "config/gpio.h"
#include "config/timing.h"
#include "config/timer.h"
#include "config/debug.h"
#include "config/exti.h"

#include "IR_SAMSUNG.h"
#include "IR_RC5.h"

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
        _pulse pulses[40];
        uint8_t total_pulses;
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
        _pulse *pulses;
        uint32_t code;
        bool toggle;

    };

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

    _ir_pulses ir_pulses;

    IR(){
        complete_read=false;
        first_read=true;  //inform the IRQ that the next signal is the start

        ir_pulses.total_pulses=0;

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

    void decode_pwm(IR_PACKAGE *ir_pck,const PROT_TIME *protocol_time){
        uint32_t code;
        bool decode_ok;


        decode_ok=false;
        code=0;

        for (uint8_t i=1; i< ir_pulses.total_pulses; i++){
                if( (protocol_time->One_high_min<=ir_pulses.pulses[i].on && ir_pulses.pulses[i].on <= protocol_time->One_high_max) &&
                    (protocol_time->One_low_min<=ir_pulses.pulses[i].off && ir_pulses.pulses[i].off <= protocol_time->One_low_max) ){
                            code|=1<<(i-1);
                    }
                else if( (protocol_time->Zero_high_min<=ir_pulses.pulses[i].on && ir_pulses.pulses[i].on <= protocol_time->Zero_high_max) &&
                    (protocol_time->Zero_low_min<=ir_pulses.pulses[i].off && ir_pulses.pulses[i].off <= protocol_time->Zero_low_max) ){
                            code|=0<<(i-1);
                            if (i==protocol_time->lenght+1){
                                decode_ok=true;
                                break;
                            }

                    }

        }
        shost<<"IR DECODE="<<decode_ok<<ir_pulses.total_pulses<<"\n";
        shost<<code;
        ir_pck->code=code;


    }

    void decode(IR_PACKAGE *ir_pck){


        PROT_TIME protocol_time;


        if(SAMSUNG_START_MIN<=ir_pulses.pulses[0].on && ir_pulses.pulses[0].on <= SAMSUNG_START_MAX){
                shost<<"SAMSUNG\n";
                ir_pck->protocol=SAMSUNG;
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

        if( (RC5_MIN<=ir_pulses.pulses[0].on && ir_pulses.pulses[0].on <= RC5_MAX) &&
            (RC5_MIN<=ir_pulses.pulses[0].off && ir_pulses.pulses[0].off <= RC5_MAX)){
                    shost<<"RC5\n";
                    ir_pck->protocol=RC5;
                    RC5_DECODE(ir_pck);
            }


        //-------------------------Decode stuff ----------------------------------------

        if(ir_pck->protocol!=RC5)
            decode_pwm(ir_pck, &protocol_time);

        //delete all the raw input IR
         ir_pulses.total_pulses=0;

        return;



    }

    void RC5_DECODE(IR_PACKAGE *final ){
        uint64_t bitstream=0;
        uint32_t code;
        uint8_t place=0;

    //--CONSTANTS -----------------------------

        //START BIT 1
        bitstream|=0<<(place);
        bitstream|=1<<(++place);
        //START BIT 2
        bitstream|=0<<(++place);
        bitstream|=1<<(++place);

    //--VARIABLE -------------------------------


        for(uint8_t i=1; i<40; i++){
            if(RC5_MIN<ir_pulses.pulses[i].on && ir_pulses.pulses[i].on < RC5_MAX)
                bitstream|=1<<(++place);
            else if(RC5_2_MIN<ir_pulses.pulses[i].on && ir_pulses.pulses[i].on < RC5_2_MAX){
                bitstream|=1<<(++place);
                if(i!=1)
                    bitstream|=1<<(++place);
            }

            if(RC5_MIN<ir_pulses.pulses[i].off && ir_pulses.pulses[i].off < RC5_MAX)
                bitstream|=0<<(++place);
            else if(RC5_2_MIN<ir_pulses.pulses[i].off && ir_pulses.pulses[i].off < RC5_2_MAX){
                bitstream|=0<<(++place);
                bitstream|=0<<(++place);
            }
        }

        for(uint8_t b=0;b<RC5_N_BITS;b++){
            uint8_t n_bitstream=2*b;

            if(bitstream && (1<<n_bitstream) )
                code|=1<<(b);
        }

        final->code=(code && 0b11111111111000); //remove the start and toggle bit
        final->toggle=(code && 0b100); //just the toggle bit


    }

    void Semihost_print_pck(void){

        for(uint8_t i=0; i< ir_pulses.total_pulses; i++){
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
      uint8_t n = ir_pulses.total_pulses;
      if(n>=40)
          return;

      //uint16_t counter;

      if(!pa[IR_IN].read()){            //off - Falling edge
          if(!first_read){
            timer.disablePeripheral();
            ThisPulse.off=timer.getCounter(); //the signal is inverted by the receptor
            timer.setCounter(0);
            ir_pulses.pulses[n] =ThisPulse;
            ir_pulses.total_pulses++;
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

#endif /* IR_H_INCLUDED */
