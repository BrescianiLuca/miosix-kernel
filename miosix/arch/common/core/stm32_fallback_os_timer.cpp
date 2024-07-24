#include "kernel/kernel.h"
#include "kernel/scheduler/timer_interrupt.h"
#include "kernel/timeconversion.h"
#include "kernel/logging.h"

//TODO Need to use TimeConversion class to convert tick in ns 

namespace miosix {


// METODI OS_TIMER.H INTERNAL

namespace internal {
    
void IRQosTimerInit() 
{
    IRQbootlog("\r\nTimer Started... ");

    //SysTick_Config(SystemCoreClock / 1000);       

    SysTick->CTRL = 0;              //Disable Systick
    SysTick->LOAD = 1000;           //1ms

    //Set interrupt prio to largest priority value (lower priority) --> CHECK
    //NVIC_SetPriority(SysTick_IRQn, (1 <<__NVIC_PRIO_BITS) - 1);

    SysTick->VAL = 0x0;             //Reset val     
    
    //SysTick->CTRL = 0x7; 

    SysTick->CTRL |= SysTick_CTRL_CLKSOURCE_Msk;
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;


    IRQbootlog("Initialized... ");
};

void IRQosTimerSetInterrupt(long long ns) noexcept 
{
    // Convert nanoseconds to ticks
    // Use TimerConversion
    // Set the reload value
    // SysTick->LOAD = ticks;
}

void IRQosTimerSetTime(long long ns) noexcept {};

unsigned int osTimerGetFrequency() 
{
    return 1000;  //1ms
};

} //namespace internal



// METODI DELLA CLASSE TIMERADAPTER

class SysTickTimer
{
public:
    
    long long tick = 0;
    miosix::TimeConversion tc;

    inline long long IRQgetTimeTick()
    {
        //Gestire il tempo simulando un counter che incrementa
        //LOAD - VAL -> time incrementa (prima iterazione)
        //time = old time + (LOAD - VAL) (successive iterazioni)
        //old time va salvato quando ho un 'intterrupt da parte del SysTick

        // Calculate elapsed ticks since the last timer tick
        // static long long oldTick = 0;
        // long long currentTick = SysTick->LOAD - SysTick->VAL;
    
        // // Handle overflow of SysTick timer
        // if(currentTick > oldTick) {
        //     // No overflow occurred
        //     return oldTick + (SysTick->LOAD - SysTick->VAL);
        // } else {
        //     // Overflow occurred
        //     IRQerrorLog("Overflow occurred\n");
        //     return oldTick - currentTick;         
        // }

        if (tick >= 0) {
            IRQerrorLog("\r\nTick >= 0");
        }
        return tick;
    }

    inline long long IRQgetTimeNs()
    {
        return tc.tick2ns(IRQgetTimeTick());
    }

    //IMPMLEMENT
    inline long long IRQgetIrqTick()
    {
        return tick;
    }

    inline long long IRQgetIrqNs()
    {
        return tc.tick2ns(IRQgetIrqTick());
    }

    void IRQsetTimeNs(long long ns)
    {
        // Convert nanoseconds to ticks and set the reload value
        // Use TimerConversion
        //SysTick->LOAD = ticks;
    }   //in os_timer.h il timer viene fatto startare qua

    inline void IRQsetIrqTick(long long tick)
    {
        if(IRQgetTimeTick() >= tick) {
            //force pending IRQ
            NVIC_SetPendingIRQ(SysTick_IRQn);
            //TODO check usage of lateIrq flag (ha a che fare con il pendig bit trick?)
        }
    }

    inline void IRQsetIrqNs(long long ns)
    {
        IRQsetIrqTick(tc.ns2tick(ns));
    }

    // inline void IRQhandler(){
    //     //check IRQtimerInterrupt in timer_interrupt.h
    //     long long tick = IRQgetTimeTick();
    //     if (tick >= IRQgetIrqTick()) {
    //         IRQtimerInterrupt((tc.tick2ns(tick)));
    //     }
    // }  

    void IRQinit() {} //in os_timer.h il timer viene inizializzato qua

    inline void IRQhandler()
    {
        //IRQerrorLog("\r\nGG SysTick Interrupt Received");
        tick = tick + 1;
    }

// PARTE DEI METODI DELLA CLASSE STM32_TIMER (DA RICONTROLLARE)

    // static inline unsigned int IRQgetClock()
    // {
    //     unsigned int timerInputFreq=SystemCoreClock;
    //     if(RCC->CFGR & RCC_CFGR_PPRE2_2) timerInputFreq/=1<<((RCC->CFGR>>RCC_CFGR_PPRE2_Pos) & 0x3);
    //     return timerInputFreq;
    // }
    // static inline void IRQenable()
    // {
    //     RCC->APB2ENR |= RCC_APB2ENR_TIM22EN;
    //     RCC_SYNC();
    //     DBGMCU->APB2FZ |= DBGMCU_APB2_FZ_DBG_TIM22_STOP; //Stop while debugging
    // }

    // static inline unsigned int IRQgetTimerCounter() {return SysTick->VAL;};
    // static inline void IRQsetTimerCounter(unsigned int val) {SysTick->LOAD = val;};

    // static inline void IRQforcePendingIrq() {__NVIC_SetPendingIRQ(SysTick_IRQn);};    
};

// METODI DELLA MACRO

miosix::SysTickTimer timer;

long long getTime() noexcept { 
    return timer.IRQgetTimeNs();
}

long long IRQgetTime() noexcept {                                                  
    return timer.IRQgetTimeNs();                  
} 

// void IRQosTimerInit(){}

// void IRQosTImerSetInterrupt(long long ns) noexcept {}   

// void IRQosTimerSetTime(long long ns) noexcept {}

// unsigned int osTimerGetFrequency() {
//     return SystemCoreClock;
// }

} // namespace miosix

miosix::SysTickTimer timer;

//TODO check implementation with assembly like the others handlers
void SysTick_Handler()
{
    timer.IRQhandler();
}
