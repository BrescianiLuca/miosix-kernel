#include "kernel/kernel.h"
#include "kernel/logging.h"

#define SYSTICK_FREQUENCY 10000 

//TODO Need to use TimeConversion class to convert tick in ns 

namespace miosix {


// METODI DELLA MACRO

long long getTime() noexcept {  
    return 0;
}

long long IRQgetTime() noexcept {                                                  
    return 0;                   
} 

void IRQosTimerInit(){}

void IRQosTImerSetInterrupt(long long ns) noexcept {}   

void IRQosTimerSetTime(long long ns) noexcept {}

unsigned int osTimerGetFrequency() {
    return 0;
}



// METODI OS_TIMER.H INTERNAL

namespace internal {
    
void IRQosTimerInit() {
    printf("TIMER STARTED  "); 
    SysTick->LOAD = SystemCoreClock/SYSTICK_FREQUENCY - 1;     
    SysTick->VAL = 0x0;        
    SysTick->CTRL = 0x5; 
    printf("INITIALIZED  ");
};

void IRQosTimerSetInterrupt(long long ns) noexcept {
    // Convert nanoseconds to ticks
    // Use TimerConversion
    // Set the reload value
    //SysTick->LOAD = ticks;
}

void IRQosTimerSetTime(long long ns) noexcept {};

unsigned int osTimerGetFrequency() {
    return SYSTICK_FREQUENCY;
};

} //namespace internal



// METODI DELLA CLASSE TIMERADAPTER

class SysTickTimer
{
public:
    //static inline SysTick_Type *get() {return SysTick;}
    //static inline IRQn_Type getIRQn() {return SysTick_IRQn;}

    inline long long IRQgetTimeTick(){
        return 0;
    }

    inline long long IRQgetIrqTick(){
        return 0;
    }

    inline long long IRQgetTimeNs(){
        // Convert ticks to nanoseconds
        // Need to use TimeConversion class
        //return SysTick->VAL * 1000000000 / SYSTICK_FREQUENCY;
        return 0;
    }

    inline long long IRQgetIrqNs(){
        return 0;
    }

    void IRQsetTimeNs(long long ns){
        // Convert nanoseconds to ticks and set the reload value
        // Use TimerConversion
        //SysTick->LOAD = ticks;
    }   //in os_timer.h il timer viene fatto startare qua

    inline void IRQsetIrqTick(long long tick){}

    inline void IRQsetIrqNs(long long ns){}

    inline void IRQhandler(){}  

    void IRQinit() {} //in os_timer.h il timer viene inizializzato qua


// PARTE DEI METODI DELLA CLASSE STM32_TIMER (DA RICONTROLLARE)

    static inline unsigned int IRQgetClock()
    {
        unsigned int timerInputFreq=SystemCoreClock;
        if(RCC->CFGR & RCC_CFGR_PPRE2_2) timerInputFreq/=1<<((RCC->CFGR>>RCC_CFGR_PPRE2_Pos) & 0x3);
        return timerInputFreq;
    }
    static inline void IRQenable()
    {
        RCC->APB2ENR |= RCC_APB2ENR_TIM22EN;
        RCC_SYNC();
        DBGMCU->APB2FZ |= DBGMCU_APB2_FZ_DBG_TIM22_STOP; //Stop while debugging
    }

    static inline unsigned int IRQgetTimerCounter() {return SysTick->VAL;};
    static inline void IRQsetTimerCounter(unsigned int val) {SysTick->LOAD = val;};

    static inline void IRQforcePendingIrq() {__NVIC_SetPendingIRQ(SysTick_IRQn);};    
};
} // namespace miosix

void __attribute__((naked)) SysTick_Handler() {
    printf("INTERRUPT RECEIVED  ");
}
