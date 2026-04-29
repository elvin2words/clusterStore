.syntax unified
.cpu cortex-m4
.thumb

.global g_pfnVectors
.global Reset_Handler
.extern SystemInit
.extern main
.extern __libc_init_array

.word _sidata
.word _sdata
.word _edata
.word _sbss
.word _ebss

.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler

.section .text.Reset_Handler,"ax",%progbits
.weak Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
  ldr   sp, =_estack
  bl    SystemInit
  ldr   r0, =_sidata
  ldr   r1, =_sdata
  ldr   r2, =_edata
1:
  cmp   r1, r2
  bcs   2f
  ldr   r3, [r0], #4
  str   r3, [r1], #4
  b     1b
2:
  ldr   r1, =_sbss
  ldr   r2, =_ebss
  movs  r3, #0
3:
  cmp   r1, r2
  bcs   4f
  str   r3, [r1], #4
  adds  r1, r1, #4
  b     3b
4:
  bl    __libc_init_array
  bl    main
5:
  b     5b

.section .text.Default_Handler,"ax",%progbits
.weak NMI_Handler
.weak HardFault_Handler
.weak MemManage_Handler
.weak BusFault_Handler
.weak UsageFault_Handler
.weak SVC_Handler
.weak DebugMon_Handler
.weak PendSV_Handler
.weak SysTick_Handler
.type Default_Handler, %function
Default_Handler:
  b Default_Handler

NMI_Handler:
  b Default_Handler
HardFault_Handler:
  b Default_Handler
MemManage_Handler:
  b Default_Handler
BusFault_Handler:
  b Default_Handler
UsageFault_Handler:
  b Default_Handler
SVC_Handler:
  b Default_Handler
DebugMon_Handler:
  b Default_Handler
PendSV_Handler:
  b Default_Handler
SysTick_Handler:
  b Default_Handler
