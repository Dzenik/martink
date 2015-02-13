
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

extern int __init_array_start;
extern int __init_array_end;

static void ConstructorsInit(void)
{
	int *s, *e;

	// call the constructorts of global objects
	s = &__init_array_start;
	e = &__init_array_end;
	while (s != e)
	{
		(*((void (**)(void)) s++))();
	}
}

extern int main(void); 
//extern void SystemInit(void); 

void c_startup(void); 
extern char _ramstart, _estack, _etext, _sdata, _sbss, _ebss, _edata; 
	
void c_startup(void)
{
	char *src, *dst;
	
	dst = &_ramstart; 
	while(dst < &_estack)
		*(dst++) = 0; 
		
	src = &_etext;
	dst = &_sdata;
	while(dst < &_edata) 
		*(dst++) = *(src++);

	src = &_sbss;
	while(src < &_ebss) 
		*(src++) = 0;
	
	SystemInit(); 
	
	ConstructorsInit(); 
	
	RCC_HCLKConfig(RCC_SYSCLK_Div1);
	RCC_PCLK1Config(RCC_HCLK_Div4); 
	RCC_PCLK2Config(RCC_HCLK_Div4);
	
	SystemCoreClock = 72000000UL; 
	
	main();
}


