/* bare-metal start for the Blackfin instruction-set simulator (binutils-gdb sim/bfin, --environment operating) */
	.section .text.start
	.global __start
__start:
	SP.H = 0x00F0; SP.L = 0x0000;	/* stack in SDRAM, below 15 MB */
	FP = 0;
	R0 = SYSCFG;			/* enable the cycle counter */
	BITSET (R0, 1);
	SYSCFG = R0;
	CALL _main;
	[--SP] = R0;			/* exit(main()) */
	R1 = SP;
	R0 = 1;
	CALL _sim_syscall;
1:	JUMP 1b;

	.text
	.global _sim_syscall		/* int sim_syscall(int nr, const int *args): P0 = nr, R0 = args, EXCPT 0 */
_sim_syscall:
	P0 = R0;
	R0 = R1;
	EXCPT 0;
	RTS;
	.global _sim_cycles		/* uint32_t sim_cycles(void) */
_sim_cycles:
	R0 = CYCLES;
	RTS;
