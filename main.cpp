#include <miosix.h>

using namespace miosix;

// class SysTickTimer
// {
// public:
//     // Function to initialize the SysTick timer with the desired frequency
//     static void init() {
//         // Configure SysTick to generate interrupts at the desired frequency
//         SysTick->LOAD = 0x00F423FF;
//         SysTick->VAL = 0;
//         SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
//   
//     }
// };

// void delay() {
  
//     //SysTick Initialization sequence:
//     /*1. Program the value in the STRELOAD register.*/
//     SysTick->LOAD = 0x00F423FF;        //Value for 1sec delay giving a system clock of 16 MHz.
//     /*2. Clear the STCURRENT register by writing to it with any value.*/
//     SysTick->VAL = 0x0;         //Reset the SysTick counter value.
//     /*3. Configure the STCTRL register for the required operation.*/
//     SysTick->CTRL = 0x5;         //Enable SysTick, no interrupt, use system clock
  
//     while((SysTick->CTRL & 0x10000) == 0){} //Here is the delay: wait until the Count flag is set
//     SysTick->CTRL = 0x0;         //Stop the Counter
  
// }

// extern "C" void SysTick_Handler() {
//     // SysTick interrupt handler code
//     // This function will be called each time the SysTick timer interrupts
//     // You can add your code here if needed

//     printf("SysTick interrupt\n");
// }

int main() {
    // Initialize the SysTick timer with a frequency of 1 second interval
    //SysTickTimer::init();

    // Your application code here

    for(;;)
    {
        ledOn();
        Thread::sleep(1000);
        ledOff();
        Thread::sleep(1000);
    }

    return 0;
}
