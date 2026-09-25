/*****************************************************************************
 *
 *   z80.c
 *   Portable Z80 emulator V3.5
 *
 *   Copyright Juergen Buchmueller, all rights reserved.
 *
 *   - This source code is released as freeware for non-commercial purposes.
 *   - You are free to use and redistribute this code in modified or
 *     unmodified form, provided you list me in the credits.
 *   - If you modify this source code, you must add a notice to each modified
 *     source file that it has been changed.  If you're a nice person, you
 *     will clearly mark each change too.  :)
 *   - If you wish to use this for commercial purposes, please contact me at
 *     pullmoll@t-online.de
 *   - The author of this copywritten work reserves the right to change the
 *     terms of its usage and license at any time, including retroactively
 *   - This entire notice must remain in the source code.
 *
 *   Changes in 3.7 [Aaron Giles]
 *   - Changed NMI handling. NMIs are now latched in set_irq_state
 *     but are not taken there. Instead they are taken at the start of the
 *     execute loop.
 *   - Changed IRQ handling. IRQ state is set in set_irq_state but not taken
 *     except during the inner execute loop.
 *   - Removed x86 assembly hacks and obsolete timing loop catchers.
 *   Changes in 3.6
 *   - Got rid of the code that would inexactly emulate a Z80, i.e. removed
 *     all the #if Z80_EXACT #else branches.
 *   - Removed leading underscores from local register name shortcuts as
 *     this violates the C99 standard.
 *   - Renamed the registers inside the Z80 context to lower case to avoid
 *     ambiguities (shortcuts would have had the same names as the fields
 *     of the structure).
 *   Changes in 3.5
 *   - Implemented OTIR, INIR, etc. without look-up table for PF flag.
 *     [Ramsoft, Sean Young]
 *   Changes in 3.4
 *   - Removed Z80-MSX specific code as it's not needed any more.
 *   - Implemented DAA without look-up table [Ramsoft, Sean Young]
 *   Changes in 3.3
 *   - Fixed undocumented flags XF & YF in the non-asm versions of CP,
 *     and all the 16 bit arithmetic instructions. [Sean Young]
 *   Changes in 3.2
 *   - Fixed undocumented flags XF & YF of RRCA, and CF and HF of
 *     INI/IND/OUTI/OUTD/INIR/INDR/OTIR/OTDR [Sean Young]
 *   Changes in 3.1
 *   - removed the REPEAT_AT_ONCE execution of LDIR/CPIR etc. opcodes
 *     for readabilities sake and because the implementation was buggy
 *     (and I was not able to find the difference)
 *   Changes in 3.0
 *   - 'finished' switch to dynamically overrideable cycle count tables
 *   Changes in 2.9:
 *   - added methods to access and override the cycle count tables
 *   - fixed handling and timing of multiple DD/FD prefixed opcodes
 *   Changes in 2.8:
 *   - OUTI/OUTD/OTIR/OTDR also pre-decrement the B register now.
 *     This was wrong because of a bug fix on the wrong side
 *     (astrocade sound driver).
 *   Changes in 2.7:
 *    - removed z80_vm specific code, it's not needed (and never was).
 *   Changes in 2.6:
 *    - BUSY_LOOP_HACKS needed to call change_pc() earlier, before
 *      checking the opcodes at the new address, because otherwise they
 *      might access the old (wrong or even NULL) banked memory region.
 *      Thanks to Sean Young for finding this nasty bug.
 *   Changes in 2.5:
 *    - Burning cycles always adjusts the ICount by a multiple of 4.
 *    - In REPEAT_AT_ONCE cases the R register wasn't incremented twice
 *      per repetition as it should have been. Those repeated opcodes
 *      could also underflow the ICount.
 *    - Simplified TIME_LOOP_HACKS for BC and added two more for DE + HL
 *      timing loops. I think those hacks weren't endian safe before too.
 *   Changes in 2.4:
 *    - z80_reset zaps the entire context, sets IX and IY to 0xffff(!) and
 *      sets the Z flag. With these changes the Tehkan World Cup driver
 *      _seems_ to work again.
 *   Changes in 2.3:
 *    - External termination of the execution loop calls z80_burn() and
 *      z80_vm_burn() to burn an amount of cycles (R adjustment)
 *    - Shortcuts which burn CPU cycles (BUSY_LOOP_HACKS and TIME_LOOP_HACKS)
 *      now also adjust the R register depending on the skipped opcodes.
 *   Changes in 2.2:
 *    - Fixed bugs in CPL, SCF and CCF instructions flag handling.
 *    - Changed variable EA and ARG16() function to UINT32; this
 *      produces slightly more efficient code.
 *    - The DD/FD XY CB opcodes where XY is 40-7F and Y is not 6/E
 *      are changed to calls to the X6/XE opcodes to reduce object size.
 *      They're hardly ever used so this should not yield a speed penalty.
 *   New in 2.0:
 *    - Optional more exact Z80 emulation (#define Z80_EXACT 1) according
 *      to a detailed description by Sean Young which can be found at:
 *      http://www.msxnet.org/tech/z80-documented.pdf
 *****************************************************************************/

#include "burnint.h"
#include "z80.h"
#include <stddef.h>
/* Modified for Teenage Mutant Ninja Turtles: generated fixed-PC native execution.
 * scripts/compile_z80.py removes all runtime instruction decoding and the
 * unused Spectrum contention interpreter. Original semantics and notices kept. */
extern "C" void tmnt_native_fault(const char *, unsigned, unsigned);
extern "C" int tmnt_native_failed(void);
static int tmnt_z80_bad();
static int tmnt_z80_unsupported() { tmnt_native_fault("Z80 configuration", 0, 0); return 0; }


#define	FALSE			0
#define TRUE			1
#define Z80_INLINE		static
#define change_pc(newpc)	
//Z80.pc.w.l = (newpc)

static Z80ReadIoHandler Z80IORead;
static Z80WriteIoHandler Z80IOWrite;
static Z80ReadProgHandler Z80ProgramRead;
static Z80WriteProgHandler Z80ProgramWrite;
static Z80ReadOpHandler Z80CPUReadOp;
static Z80ReadOpArgHandler Z80CPUReadOpArg;

#define Z80Vector Z80.vector

#define VERBOSE 0

#define LOG(x)	//do { if (VERBOSE) logerror x; } while (0)

/* execute main opcodes inside a big switch statement */
#ifndef BIG_SWITCH
#define BIG_SWITCH			1
#endif

/* big flags array for ADD/ADC/SUB/SBC/CP results */
#define BIG_FLAGS_ARRAY		1

/* on JP and JR opcodes check for tight loops */
#define BUSY_LOOP_HACKS     0

// TMNT has no Spectrum bus contention. Keep diagnostic t-states.
static void eat_cycles(int, int);
static const int m_ula_variant = ULA_VARIANT_NONE;
static const int m_cycles_per_line = 0;
static int m_tstate_counter;
static int m_selected_bank;
static UINT8 store_rwinfo(UINT16, UINT8, UINT16, const char *) { return 0; }
static UINT8 run_script() { return 0; }
static int Z80lastop; // for snow effect
/****************************************************************************/
/* The Z80 registers. HALT is set to 1 when the CPU is halted, the refresh  */
/* register is calculated as follows: refresh=(Z80.r&127)|(Z80.r2&128)      */
/****************************************************************************/
#define CF	0x01
#define NF	0x02
#define PF	0x04
#define VF	PF
#define XF	0x08
#define HF	0x10
#define YF	0x20
#define ZF	0x40
#define SF	0x80

#define INT_IRQ 0x01
#define NMI_IRQ 0x02

#define PRVPC Z80.prvpc.d		/* previous program counter */

#define PCD	Z80.pc.d
#define PC Z80.pc.w.l

#define SPD Z80.sp.d
#define SP Z80.sp.w.l

#define AFD Z80.af.d
#define AF Z80.af.w.l
#define A Z80.af.b.h
#define F Z80.af.b.l

#define BCD Z80.bc.d
#define BC Z80.bc.w.l
#define B Z80.bc.b.h
#define C Z80.bc.b.l

#define DED Z80.de.d
#define DE Z80.de.w.l
#define D Z80.de.b.h
#define E Z80.de.b.l

#define HLD Z80.hl.d
#define HL Z80.hl.w.l
#define H Z80.hl.b.h
#define L Z80.hl.b.l

#define IXD Z80.ix.d
#define IX Z80.ix.w.l
#define HX Z80.ix.b.h
#define LX Z80.ix.b.l

#define IYD Z80.iy.d
#define IY Z80.iy.w.l
#define HY Z80.iy.b.h
#define LY Z80.iy.b.l

#define WZ     Z80.wz.w.l
#define WZ_H    Z80.wz.b.h
#define WZ_L    Z80.wz.b.l


#define I Z80.i
#define R Z80.r
#define R2 Z80.r2
#define IM Z80.im
#define IFF1 Z80.iff1
#define IFF2 Z80.iff2
#define HALT Z80.halt

static Z80_Regs Z80;
#define EA Z80.EA

void (*z80edfe_callback)(Z80_Regs *Regs) = NULL;

static UINT8 SZ[256];		/* zero and sign flags */
static UINT8 SZ_BIT[256];	/* zero, sign and parity/overflow (=zero) flags for BIT opcode */
static UINT8 SZP[256];		/* zero, sign and parity flags */
static UINT8 SZHV_inc[256]; /* zero, sign, half carry and overflow flags INC r8 */
static UINT8 SZHV_dec[256]; /* zero, sign, half carry and overflow flags DEC r8 */

#if BIG_FLAGS_ARRAY
static UINT8 *SZHVC_add = 0;
static UINT8 *SZHVC_sub = 0;
#endif

static const UINT8 cc_op[0x100] = {
 4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
 8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
 7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
 7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
 5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
 5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
 5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
 5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11};

static const UINT8 cc_cb[0x100] = {
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,12, 8, 8, 8, 8, 8, 8, 8,12, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8,
 8, 8, 8, 8, 8, 8,15, 8, 8, 8, 8, 8, 8, 8,15, 8};

static const UINT8 cc_ed[0x100] = {
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
12,12,15,20, 8,14, 8, 9,12,12,15,20, 8,14, 8, 9,
12,12,15,20, 8,14, 8, 9,12,12,15,20, 8,14, 8, 9,
12,12,15,20, 8,14, 8,18,12,12,15,20, 8,14, 8,18,
12,12,15,20, 8,14, 8, 8,12,12,15,20, 8,14, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
16,16,16,16, 8, 8, 8, 8,16,16,16,16, 8, 8, 8, 8,
16,16,16,16, 8, 8, 8, 8,16,16,16,16, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};

static const UINT8 cc_xy[0x100] = {
	4+4,10+4, 7+4, 6+4, 4+4, 4+4, 7+4, 4+4, 4+4,11+4, 7+4, 6+4, 4+4, 4+4, 7+4, 4+4,
	8+4,10+4, 7+4, 6+4, 4+4, 4+4, 7+4, 4+4,12+4,11+4, 7+4, 6+4, 4+4, 4+4, 7+4, 4+4,
	7+4,10+4,16+4, 6+4, 4+4, 4+4, 7+4, 4+4, 7+4,11+4,16+4, 6+4, 4+4, 4+4, 7+4, 4+4,
	7+4,10+4,13+4, 6+4,23  ,23  ,19  , 4+4, 7+4,11+4,13+4, 6+4, 4+4, 4+4, 7+4, 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	19 ,19  ,19  ,19  ,19  ,19  , 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4, 4+4, 4+4, 4+4, 4+4, 4+4, 4+4,19  , 4+4,
	5+4,10+4,10+4,10+4,10+4,11+4, 7+4,11+4, 5+4,10+4,10+4, 0  ,10+4,17+4, 7+4,11+4, /* cb -> cc_xycb */
	5+4,10+4,10+4,11+4,10+4,11+4, 7+4,11+4, 5+4, 4+4,10+4,11+4,10+4, 4  , 7+4,11+4, /* dd -> cc_xy again */
	5+4,10+4,10+4,19+4,10+4,11+4, 7+4,11+4, 5+4, 4+4,10+4, 4+4,10+4, 4  , 7+4,11+4, /* ed -> cc_ed */
	5+4,10+4,10+4, 4+4,10+4,11+4, 7+4,11+4, 5+4, 6+4,10+4, 4+4,10+4, 4  , 7+4,11+4  /* fd -> cc_xy again */
};

static const UINT8 cc_xycb[0x100] = {
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23};

/* extra cycles if jr/jp/call taken and 'interrupt latency' on rst 0-7 */
static const UINT8 cc_ex[0x100] = {
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* DJNZ */
 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,	/* JR NZ/JR Z */
 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,	/* JR NC/JR C */
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 5, 5, 5, 5, 0, 0, 0, 0, 5, 5, 5, 5, 0, 0, 0, 0,	/* LDIR/CPIR/INIR/OTIR LDDR/CPDR/INDR/OTDR */
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2,
 6, 0, 0, 0, 7, 0, 0, 2, 6, 0, 0, 0, 7, 0, 0, 2};

static const UINT8 *cc[6];
#define Z80_TABLE_dd	Z80_TABLE_xy
#define Z80_TABLE_fd	Z80_TABLE_xy

#define CC(prefix,opcode) do { eat_cycles(CYCLES_EXEC, cc[Z80_TABLE_##prefix][opcode]); } while (0)

#define ENTER_HALT {											\
	PC--;														\
	HALT = 1;													\
}

/***************************************************************
 * Leave HALT state; write 0 to fake port
 ***************************************************************/
#define LEAVE_HALT {											\
	if( HALT )													\
	{															\
		HALT = 0;												\
		PC++;													\
	}															\
}

/***************************************************************
 * Input a byte from given I/O port
 ***************************************************************/
Z80_INLINE UINT8 IN(INT16 port)
{
	// For floating bus support, the read_byte triggers the
	// 'spectrum_port_ula_r' callback which will require the tstate
	// counter to be up-to-date.
	if (m_ula_variant != ULA_VARIANT_NONE) {
		return store_rwinfo(port, -1, RWINFO_READ|RWINFO_IO_PORT, "in port");
	} else {
		return Z80IORead(port);
	}
}

/***************************************************************
 * Output a byte to given I/O port
 ***************************************************************/
Z80_INLINE void OUT(UINT16 port, UINT8 value)
{
	if (m_ula_variant != ULA_VARIANT_NONE) {
		store_rwinfo(port, value, RWINFO_WRITE|RWINFO_IO_PORT, "out port");
	} else {
		Z80IOWrite(port, value);
	}
}

/***************************************************************
 * Read a byte from given memory location
 ***************************************************************/
//#define RM(addr) (UINT8)Z80ProgramRead(addr)
Z80_INLINE UINT8 RM(UINT16 addr)
{
	UINT8 res = Z80ProgramRead(addr);
	store_rwinfo(addr, res, RWINFO_READ|RWINFO_MEMORY, "rm");
	return res;
}

/***************************************************************
 * Read a word from given memory location
 ***************************************************************/
Z80_INLINE void RM16( UINT32 addr, Z80_PAIR *r )
{
	r->b.l = RM(addr);
	r->b.h = RM((addr+1)&0xffff);
}

/***************************************************************
 * Write a byte to given memory location
 ***************************************************************/
//#define WM(addr,value) Z80ProgramWrite(addr,value)
Z80_INLINE void WM(UINT16 addr, UINT8 value)
{
	store_rwinfo(addr, value, RWINFO_WRITE|RWINFO_MEMORY, "wm");
	Z80ProgramWrite(addr,value);
}

#define cpu_readop(n) Z80CPUReadOp(n)
#define cpu_readop_arg(n) Z80CPUReadOpArg(n)

/***************************************************************
 * Write a word to given memory location
 ***************************************************************/
Z80_INLINE void WM16( UINT32 addr, Z80_PAIR *r )
{
	WM(addr,r->b.l);
	WM((addr+1)&0xffff,r->b.h);
}

/***************************************************************
 * ROP() is identical to RM() except it is used for
 * reading opcodes. In case of system with memory mapped I/O,
 * this function can be used to greatly speed up emulation
 ***************************************************************/


/****************************************************************
 * ARG() is identical to ROP() except it is used
 * for reading opcode arguments. This difference can be used to
 * support systems that use different encoding mechanisms for
 * opcodes and opcode arguments
 ***************************************************************/
Z80_INLINE UINT8 ARG(void)
{
	unsigned pc = PCD;
	PC++;

	UINT8 res = cpu_readop_arg(pc);
	store_rwinfo(((PAIR*)(&pc))->w.l, res, RWINFO_READ|RWINFO_MEMORY, "arg");

	return res;
}

Z80_INLINE UINT16 ARG16(void)
{
	unsigned pc1 = PCD;
	unsigned pc2 = (pc1+1)&0xffff;
	PC += 2;
	UINT8 res1 = cpu_readop_arg(pc1);
	store_rwinfo(((PAIR*)(&pc1))->w.l, res1, RWINFO_READ|RWINFO_MEMORY, "arg16 byte1");
	UINT8 res2 = cpu_readop_arg(pc2);
	store_rwinfo(((PAIR*)(&pc2))->w.l, res2, RWINFO_READ|RWINFO_MEMORY, "arg16 byte2");

	return res1 | (res2 << 8);
}

/***************************************************************
 * Calculate the effective address EA of an opcode using
 * IX+offset resp. IY+offset addressing.
 ***************************************************************/
#define EAX EA = (UINT32)(UINT16)(IX + (INT8)ARG()); WZ = EA;
#define EAY EA = (UINT32)(UINT16)(IY + (INT8)ARG()); WZ = EA;

/***************************************************************
 * POP
 ***************************************************************/
#define POP(DR) do { RM16( SPD, &Z80.DR ); SP += 2; } while (0)

/***************************************************************
 * PUSH
 ***************************************************************/
#define PUSH(SR) do { SP --; WM( SPD, Z80.SR.b.h ); SP --; WM( SPD, Z80.SR.b.l ); } while (0)

/***************************************************************
 * JP
 ***************************************************************/
#define JP {													\
	PCD = ARG16();												\
	WZ = PCD;													\
	change_pc(PCD);												\
}

/***************************************************************
 * JP_COND
 ***************************************************************/

#define JP_COND(cond)											\
	if( cond )													\
	{															\
		PCD = ARG16();											\
		WZ = PCD;												\
		change_pc(PCD);											\
	}															\
	else														\
	{															\
		WZ = ARG16(); /* implicit do PC += 2 */                 \
	}

/***************************************************************
 * JR
 ***************************************************************/
#define JR()													\
{																\
	INT8 arg = (INT8)ARG(); /* ARG() also increments PC */		\
	PC += arg;				/* so don't do PC += ARG() */		\
	WZ = PC;													\
	change_pc(PCD);												\
}

/***************************************************************
 * JR_COND
 ***************************************************************/
#define JR_COND(cond,opcode)									\
	if( cond )													\
	{															\
		CC(ex,opcode);											\
		                    \
		run_script();                                           \
																\
		JR()                                                    \
	}															\
	else                                                        \
	{                                                           \
		ARG();													\
	}

/***************************************************************
 * CALL
 ***************************************************************/
#define CALL()													\
	EA = ARG16();												\
	WZ = EA;													\
	PUSH( pc );													\
	PCD = EA;													\
	change_pc(PCD)

/***************************************************************
 * CALL_COND
 ***************************************************************/
#define CALL_COND(cond,opcode)									\
	if( cond )													\
	{															\
		CC(ex,opcode);											\
		                    \
		run_script();                                           \
		EA = ARG16();											\
	    WZ = EA;												\
		PUSH( pc );												\
		PCD = EA;												\
		change_pc(PCD);											\
	}															\
	else														\
	{															\
		WZ = ARG16(); /* implicit call PC+=2; */				\
	}

/***************************************************************
 * RET_COND
 ***************************************************************/
#define RET_COND(cond,opcode)									\
	if( cond )													\
	{															\
		CC(ex,opcode);											\
		                    \
		run_script();                                           \
		POP( pc );												\
		WZ = PC;												\
		change_pc(PCD);											\
	}

// zx spectrum tapeload bios callback for opcode 0xc0 (ret nz) -dink sept.29 2020


/***************************************************************
 * RETN
 ***************************************************************/
#define RETN	{												\
	LOG(("Z80 #%d RETN IFF1:%d IFF2:%d\n", cpu_getactivecpu(), IFF1, IFF2)); \
	POP( pc );													\
	WZ = PC;													\
	change_pc(PCD);												\
	Z80.after_retn = TRUE;  									\
}

/***************************************************************
 * RETI
 ***************************************************************/
#define RETI	{												\
	POP( pc );													\
	WZ = PC;													\
	change_pc(PCD);												\
/* according to http://www.msxnet.org/tech/z80-documented.pdf */\
	IFF1 = IFF2;												\
    if (Z80.daisy)												\
		tmnt_z80_unsupported();					\
}

/***************************************************************
 * LD   R,A
 ***************************************************************/
#define LD_R_A {												\
	R = A;														\
	R2 = A & 0x80;				/* keep bit 7 of R */			\
}

/***************************************************************
 * LD   A,R
 ***************************************************************/
#define LD_A_R {												\
	A = (R & 0x7f) | R2;										\
	F = (F & CF) | SZ[A] | ( IFF2 << 2 );						\
}

/***************************************************************
 * LD   I,A
 ***************************************************************/
#define LD_I_A {												\
	I = A;														\
}

/***************************************************************
 * LD   A,I
 ***************************************************************/
#define LD_A_I {												\
	A = I;														\
	F = (F & CF) | SZ[A] | ( IFF2 << 2 );						\
}

/***************************************************************
 * RST
 ***************************************************************/
#define RST(addr)												\
	PUSH( pc );													\
	PCD = addr;													\
	WZ = PC;													\
	change_pc(PCD)

/***************************************************************
 * INC  r8
 ***************************************************************/
Z80_INLINE UINT8 INC(UINT8 value)
{
	UINT8 res = value + 1;
	F = (F & CF) | SZHV_inc[res];
	return (UINT8)res;
}

/***************************************************************
 * DEC  r8
 ***************************************************************/
Z80_INLINE UINT8 DEC(UINT8 value)
{
	UINT8 res = value - 1;
	F = (F & CF) | SZHV_dec[res];
	return res;
}

/***************************************************************
 * RLCA
 ***************************************************************/
#define RLCA													\
	A = (A << 1) | (A >> 7);									\
	F = (F & (SF | ZF | PF)) | (A & (YF | XF | CF))

/***************************************************************
 * RRCA
 ***************************************************************/
#define RRCA													\
	F = (F & (SF | ZF | PF)) | (A & CF);						\
	A = (A >> 1) | (A << 7);									\
	F |= (A & (YF | XF) )

/***************************************************************
 * RLA
 ***************************************************************/
#define RLA {													\
	UINT8 res = (A << 1) | (F & CF);							\
	UINT8 c = (A & 0x80) ? CF : 0;								\
	F = (F & (SF | ZF | PF)) | c | (res & (YF | XF));			\
	A = res;													\
}

/***************************************************************
 * RRA
 ***************************************************************/
#define RRA {													\
	UINT8 res = (A >> 1) | (F << 7);							\
	UINT8 c = (A & 0x01) ? CF : 0;								\
	F = (F & (SF | ZF | PF)) | c | (res & (YF | XF));			\
	A = res;													\
}

/***************************************************************
 * RRD
 ***************************************************************/
#define RRD {													\
	UINT8 n = RM(HL);											\
	WZ = HL+1;													\
	WM( HL, (n >> 4) | (A << 4) );								\
	A = (A & 0xf0) | (n & 0x0f);								\
	F = (F & CF) | SZP[A];										\
}

/***************************************************************
 * RLD
 ***************************************************************/
#define RLD {													\
	UINT8 n = RM(HL);											\
	WZ = HL+1;													\
	WM( HL, (n << 4) | (A & 0x0f) );							\
	A = (A & 0xf0) | (n >> 4);									\
	F = (F & CF) | SZP[A];										\
}

/***************************************************************
 * ADD  A,n
 ***************************************************************/
#if BIG_FLAGS_ARRAY
#define ADD(value)												\
{																\
	UINT32 ah = AFD & 0xff00;									\
	UINT32 res = (UINT8)((ah >> 8) + value);					\
	F = SZHVC_add[ah | res];									\
	A = res;													\
}
#else
#define ADD(value)												\
{																\
	unsigned val = value;										\
	unsigned res = A + val;										\
	F = SZ[(UINT8)res] | ((res >> 8) & CF) |					\
		((A ^ res ^ val) & HF) |								\
		(((val ^ A ^ 0x80) & (val ^ res) & 0x80) >> 5);			\
	A = (UINT8)res;												\
}
#endif

/***************************************************************
 * ADC  A,n
 ***************************************************************/
#if BIG_FLAGS_ARRAY
#define ADC(value)												\
{																\
	UINT32 ah = AFD & 0xff00, c = AFD & 1;						\
	UINT32 res = (UINT8)((ah >> 8) + value + c);				\
	F = SZHVC_add[(c << 16) | ah | res];						\
	A = res;													\
}
#else
#define ADC(value)												\
{																\
	unsigned val = value;										\
	unsigned res = A + val + (F & CF);							\
	F = SZ[res & 0xff] | ((res >> 8) & CF) |					\
		((A ^ res ^ val) & HF) |								\
		(((val ^ A ^ 0x80) & (val ^ res) & 0x80) >> 5);			\
	A = res;													\
}
#endif

/***************************************************************
 * SUB  n
 ***************************************************************/
#if BIG_FLAGS_ARRAY
#define SUB(value)												\
{																\
	UINT32 ah = AFD & 0xff00;									\
	UINT32 res = (UINT8)((ah >> 8) - value);					\
	F = SZHVC_sub[ah | res];									\
	A = res;													\
}
#else
#define SUB(value)												\
{																\
	unsigned val = value;										\
	unsigned res = A - val;										\
	F = SZ[res & 0xff] | ((res >> 8) & CF) | NF |				\
		((A ^ res ^ val) & HF) |								\
		(((val ^ A) & (A ^ res) & 0x80) >> 5);					\
	A = res;													\
}
#endif

/***************************************************************
 * SBC  A,n
 ***************************************************************/
#if BIG_FLAGS_ARRAY
#define SBC(value)												\
{																\
	UINT32 ah = AFD & 0xff00, c = AFD & 1;						\
	UINT32 res = (UINT8)((ah >> 8) - value - c);				\
	F = SZHVC_sub[(c<<16) | ah | res];							\
	A = res;													\
}
#else
#define SBC(value)												\
{																\
	unsigned val = value;										\
	unsigned res = A - val - (F & CF);							\
	F = SZ[res & 0xff] | ((res >> 8) & CF) | NF |				\
		((A ^ res ^ val) & HF) |								\
		(((val ^ A) & (A ^ res) & 0x80) >> 5);					\
	A = res;													\
}
#endif

/***************************************************************
 * NEG
 ***************************************************************/
#define NEG {													\
	UINT8 value = A;											\
	A = 0;														\
	SUB(value);													\
}

/***************************************************************
 * DAA
 ***************************************************************/
#define DAA {													\
	UINT8 a = A;                                                \
	if (F & NF) {                                               \
		if ((F&HF) | ((A&0xf)>9)) a-=6;                         \
		if ((F&CF) | (A>0x99)) a-=0x60;                         \
	}                                                           \
	else {                                                      \
		if ((F&HF) | ((A&0xf)>9)) a+=6;                         \
		if ((F&CF) | (A>0x99)) a+=0x60;                         \
	}                                                           \
																\
	F = (F&(CF|NF)) | (A>0x99) | ((A^a)&HF) | SZP[a];           \
	A = a;                                                      \
}

/***************************************************************
 * AND  n
 ***************************************************************/
#define AND(value)												\
	A &= value;													\
	F = SZP[A] | HF

/***************************************************************
 * OR   n
 ***************************************************************/
#define OR(value)												\
	A |= value;													\
	F = SZP[A]

/***************************************************************
 * XOR  n
 ***************************************************************/
#define XOR(value)												\
	A ^= value;													\
	F = SZP[A]

/***************************************************************
 * CP   n
 ***************************************************************/
#if BIG_FLAGS_ARRAY
#define CP(value)												\
{																\
	unsigned val = value;										\
	UINT32 ah = AFD & 0xff00;									\
	UINT32 res = (UINT8)((ah >> 8) - val);						\
	F = (SZHVC_sub[ah | res] & ~(YF | XF)) |					\
		(val & (YF | XF));										\
}
#else
#define CP(value)												\
{																\
	unsigned val = value;										\
	unsigned res = A - val;										\
	F = (SZ[res & 0xff] & (SF | ZF)) |							\
		(val & (YF | XF)) | ((res >> 8) & CF) | NF |			\
		((A ^ res ^ val) & HF) |								\
		((((val ^ A) & (A ^ res)) >> 5) & VF);					\
}
#endif

/***************************************************************
 * EX   AF,AF'
 ***************************************************************/
#define EX_AF {													\
	Z80_PAIR tmp;													\
	tmp = Z80.af; Z80.af = Z80.af2; Z80.af2 = tmp;				\
}

/***************************************************************
 * EX   DE,HL
 ***************************************************************/
#define EX_DE_HL {												\
	Z80_PAIR tmp;													\
	tmp = Z80.de; Z80.de = Z80.hl; Z80.hl = tmp;				\
}

/***************************************************************
 * EXX
 ***************************************************************/
#define EXX {													\
	Z80_PAIR tmp;													\
	tmp = Z80.bc; Z80.bc = Z80.bc2; Z80.bc2 = tmp;				\
	tmp = Z80.de; Z80.de = Z80.de2; Z80.de2 = tmp;				\
	tmp = Z80.hl; Z80.hl = Z80.hl2; Z80.hl2 = tmp;				\
}

/***************************************************************
 * EX   (SP),r16
 ***************************************************************/
#define EXSP(DR)												\
{																\
	Z80_PAIR tmp = { { 0, 0, 0, 0 } };								\
	RM16( SPD, &tmp );											\
	WM16( SPD, &Z80.DR );										\
	Z80.DR = tmp;												\
	WZ = Z80.DR.d;													\
}


/***************************************************************
 * ADD16
 ***************************************************************/
#define ADD16(DR,SR)											\
{																\
	UINT32 res = Z80.DR.d + Z80.SR.d;							\
	WZ = Z80.DR.d + 1;												\
	F = (F & (SF | ZF | VF)) |									\
		(((Z80.DR.d ^ res ^ Z80.SR.d) >> 8) & HF) |				\
		((res >> 16) & CF) | ((res >> 8) & (YF | XF));			\
	Z80.DR.w.l = (UINT16)res;									\
}

/***************************************************************
 * ADC  r16,r16
 ***************************************************************/
#define ADC16(Reg)												\
{																\
	UINT32 res = HLD + Z80.Reg.d + (F & CF);					\
	WZ = HL + 1;												\
	F = (((HLD ^ res ^ Z80.Reg.d) >> 8) & HF) |					\
		((res >> 16) & CF) |									\
		((res >> 8) & (SF | YF | XF)) |							\
		((res & 0xffff) ? 0 : ZF) |								\
		(((Z80.Reg.d ^ HLD ^ 0x8000) & (Z80.Reg.d ^ res) & 0x8000) >> 13); \
	HL = (UINT16)res;											\
}

/***************************************************************
 * SBC  r16,r16
 ***************************************************************/
#define SBC16(Reg)												\
{																\
	UINT32 res = HLD - Z80.Reg.d - (F & CF);					\
	WZ = HL + 1;												\
	F = (((HLD ^ res ^ Z80.Reg.d) >> 8) & HF) | NF |			\
		((res >> 16) & CF) |									\
		((res >> 8) & (SF | YF | XF)) |							\
		((res & 0xffff) ? 0 : ZF) |								\
		(((Z80.Reg.d ^ HLD) & (HLD ^ res) &0x8000) >> 13);		\
	HL = (UINT16)res;											\
}

/***************************************************************
 * RLC  r8
 ***************************************************************/
Z80_INLINE UINT8 RLC(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | (res >> 7)) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RRC  r8
 ***************************************************************/
Z80_INLINE UINT8 RRC(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (res << 7)) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RL   r8
 ***************************************************************/
Z80_INLINE UINT8 RL(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | (F & CF)) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * RR   r8
 ***************************************************************/
Z80_INLINE UINT8 RR(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (F << 7)) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SLA  r8
 ***************************************************************/
Z80_INLINE UINT8 SLA(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = (res << 1) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SRA  r8
 ***************************************************************/
Z80_INLINE UINT8 SRA(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = ((res >> 1) | (res & 0x80)) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SLL  r8
 ***************************************************************/
Z80_INLINE UINT8 SLL(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x80) ? CF : 0;
	res = ((res << 1) | 0x01) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * SRL  r8
 ***************************************************************/
Z80_INLINE UINT8 SRL(UINT8 value)
{
	unsigned res = value;
	unsigned c = (res & 0x01) ? CF : 0;
	res = (res >> 1) & 0xff;
	F = SZP[res] | c;
	return res;
}

/***************************************************************
 * BIT  bit,r8
 ***************************************************************/
#undef BIT
#define BIT(bit,reg)											\
    F = (F & CF) | HF | (SZ_BIT[reg & (1<<bit)] & ~(YF|XF)) | (reg & (YF|XF));
	
	

/***************************************************************
 * BIT  bit,(HL)
 ***************************************************************/
#define BIT_HL(bit,reg)											\
	F = (F & CF) | HF | (SZ_BIT[reg & (1<<bit)] & ~(YF|XF)) | (WZ_H & (YF|XF));

/***************************************************************
 * BIT  bit,(IX/Y+o)
 ***************************************************************/

#define BIT_XY(bit,reg)											\
	F = (F & CF) | HF | (SZ_BIT[reg & (1<<bit)] & ~(YF|XF)) | ((EA>>8) & (YF|XF))

/***************************************************************
 * RES  bit,r8
 ***************************************************************/
Z80_INLINE UINT8 RES(UINT8 bit, UINT8 value)
{
	return value & ~(1<<bit);
}

/***************************************************************
 * SET  bit,r8
 ***************************************************************/
Z80_INLINE UINT8 SET(UINT8 bit, UINT8 value)
{
	return value | (1<<bit);
}

/***************************************************************
 * LDI
 ***************************************************************/
#define LDI {													\
	UINT8 io = RM(HL);											\
	WM( DE, io );												\
	F &= SF | ZF | CF;											\
	if( (A + io) & 0x02 ) F |= YF; /* bit 1 -> flag 5 */		\
	if( (A + io) & 0x08 ) F |= XF; /* bit 3 -> flag 3 */		\
	HL++; DE++; BC--;											\
	if( BC ) F |= VF;											\
}

/***************************************************************
 * CPI
 ***************************************************************/
#define CPI {													\
	UINT8 val = RM(HL);											\
	UINT8 res = A - val;										\
	WZ++;														\
	HL++; BC--;													\
	F = (F & CF) | (SZ[res]&~(YF|XF)) | ((A^val^res)&HF) | NF;	\
	if( F & HF ) res -= 1;										\
	if( res & 0x02 ) F |= YF; /* bit 1 -> flag 5 */				\
	if( res & 0x08 ) F |= XF; /* bit 3 -> flag 3 */				\
	if( BC ) F |= VF;											\
}

/***************************************************************
 * INI
 ***************************************************************/
#define INI {													\
	unsigned t;													\
	UINT8 io = IN(BC);											\
	WZ = BC + 1;												\
	B--;														\
	WM( HL, io );												\
	HL++;														\
	F = SZ[B];													\
	t = (unsigned)((C + 1) & 0xff) + (unsigned)io;				\
	if( io & SF ) F |= NF;										\
	if( t & 0x100 ) F |= HF | CF;								\
	F |= SZP[(UINT8)(t & 0x07) ^ B] & PF;						\
}

/***************************************************************
 * OUTI
 ***************************************************************/
#define OUTI {													\
	unsigned t;													\
	UINT8 io = RM(HL);											\
	B--;														\
	WZ = BC + 1;												\
	OUT( BC, io );												\
	HL++;														\
	F = SZ[B];													\
	t = (unsigned)L + (unsigned)io;								\
	if( io & SF ) F |= NF;										\
	if( t & 0x100 ) F |= HF | CF;								\
	F |= SZP[(UINT8)(t & 0x07) ^ B] & PF;						\
}

/***************************************************************
 * LDD
 ***************************************************************/
#define LDD {													\
	UINT8 io = RM(HL);											\
	WM( DE, io );												\
	F &= SF | ZF | CF;											\
	if( (A + io) & 0x02 ) F |= YF; /* bit 1 -> flag 5 */		\
	if( (A + io) & 0x08 ) F |= XF; /* bit 3 -> flag 3 */		\
	HL--; DE--; BC--;											\
	if( BC ) F |= VF;											\
}

/***************************************************************
 * CPD
 ***************************************************************/
#define CPD {													\
	UINT8 val = RM(HL);											\
	UINT8 res = A - val;										\
	WZ--;														\
	HL--; BC--;													\
	F = (F & CF) | (SZ[res]&~(YF|XF)) | ((A^val^res)&HF) | NF;	\
	if( F & HF ) res -= 1;										\
	if( res & 0x02 ) F |= YF; /* bit 1 -> flag 5 */				\
	if( res & 0x08 ) F |= XF; /* bit 3 -> flag 3 */				\
	if( BC ) F |= VF;											\
}

/***************************************************************
 * IND
 ***************************************************************/
#define IND {													\
	unsigned t;													\
	UINT8 io = IN(BC);											\
	WZ = BC - 1;												\
	B--;														\
	WM( HL, io );												\
	HL--;														\
	F = SZ[B];													\
	t = ((unsigned)(C - 1) & 0xff) + (unsigned)io;				\
	if( io & SF ) F |= NF;										\
	if( t & 0x100 ) F |= HF | CF;								\
	F |= SZP[(UINT8)(t & 0x07) ^ B] & PF;						\
}

/***************************************************************
 * OUTD
 ***************************************************************/
#define OUTD {													\
	unsigned t;													\
	UINT8 io = RM(HL);											\
	B--;														\
	WZ = BC - 1;												\
	OUT( BC, io );												\
	HL--;														\
	F = SZ[B];													\
	t = (unsigned)L + (unsigned)io;								\
	if( io & SF ) F |= NF;										\
	if( t & 0x100 ) F |= HF | CF;								\
	F |= SZP[(UINT8)(t & 0x07) ^ B] & PF;						\
}

/***************************************************************
 * LDIR
 ***************************************************************/
#define LDIR													\
	LDI;														\
	if( BC )													\
	{															\
		CC(ex,0xb0);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
		WZ = PC + 1;											\
	}

/***************************************************************
 * CPIR
 ***************************************************************/
#define CPIR													\
	CPI;														\
	if( BC && !(F & ZF) )										\
	{															\
		CC(ex,0xb1);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
		WZ = PC + 1;											\
	}

/***************************************************************
 * INIR
 ***************************************************************/
#define INIR													\
	INI;														\
	if( B )														\
	{															\
		CC(ex,0xb2);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
	}

/***************************************************************
 * OTIR
 ***************************************************************/
#define OTIR													\
	OUTI;														\
	if( B )														\
	{															\
		CC(ex,0xb3);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
	}

/***************************************************************
 * LDDR
 ***************************************************************/
#define LDDR													\
	LDD;														\
	if( BC )													\
	{															\
		CC(ex,0xb8);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
		WZ = PC + 1;											\
	}

/***************************************************************
 * CPDR
 ***************************************************************/
#define CPDR													\
	CPD;														\
	if( BC && !(F & ZF) )										\
	{															\
		CC(ex,0xb9);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
		WZ = PC + 1;											\
	}

/***************************************************************
 * INDR
 ***************************************************************/
#define INDR													\
	IND;														\
	if( B )														\
	{															\
		CC(ex,0xba);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
	}

/***************************************************************
 * OTDR
 ***************************************************************/
#define OTDR													\
	OUTD;														\
	if( B )														\
	{															\
		CC(ex,0xbb);											\
		                    \
		run_script();                                           \
		PC -= 2;												\
	}

/***************************************************************
 * EI
 ***************************************************************/
#define EI {													\
	IFF1 = IFF2 = 1;											\
	Z80.after_ei = TRUE;										\
}

/**********************************************************
 * opcodes with CB prefix
 * rotate, shift and bit operations
 **********************************************************/

static int tmnt_z80_bad() { tmnt_native_fault("Z80", PCD, cpu_readop(PCD & 0xffff)); Z80.end_run = 1; return 0; }
#include "generated_z80.inc"
static void take_interrupt(void)
{
	int irq_vector = Z80Vector;

	if (m_ula_variant != ULA_VARIANT_NONE && m_tstate_counter >= ((m_cycles_per_line == 228) ? 36 : 32))
		return;

	/* there isn't a valid previous program counter */
	PRVPC = (UINT32)-1;

	/* Check if processor was halted */
	LEAVE_HALT;

	/* Clear both interrupt flip flops */
	IFF1 = IFF2 = 0;

	/* Daisy chain mode? If so, call the requesting device */
	if (Z80.daisy)
		irq_vector = tmnt_z80_unsupported();

	/* else call back the cpu interface to retrieve the vector */
//	else
//		irq_vector = (*Z80.irq_callback)(0);

	/* "hold_irq" assures that an irq request (with CPU_IRQSTATUS_HOLD) gets
	   acknowleged.  This is designed to get around the following 2 problems:

	   1) Requests made with CPU_IRQSTATUS_AUTO might get skipped in
	   circumstances where IRQs are disabled at the moment it was requested.

	   2) Requests made with CPU_IRQSTATUS_ACK might cause more than 1 irq to
	   get taken if is held in the _ACK state for too long(!) - dink jan.2016
	*/
	if (Z80.hold_irq) {
		Z80.hold_irq = 0;
		Z80.irq_state = 0;
	}

//	LOG(("Z80 #%d single int. irq_vector $%02x\n", cpu_getactivecpu(), irq_vector));
	R++;
	/* Interrupt mode 2. Call [Z80.i:databyte] */
	if( IM == 2 )
	{
		irq_vector = (irq_vector & 0xff) | (I << 8);
		PUSH( pc );
		RM16( irq_vector, &Z80.pc );
//		LOG(("Z80 #%d IM2 [$%04x] = $%04x\n",cpu_getactivecpu() , irq_vector, PCD));
		/* CALL opcode timing */
		//bprintf(0, _T("imm2: opcd: %d  exff: %d\n"),cc[Z80_TABLE_op][0xcd], cc[Z80_TABLE_ex][0xff]);
		eat_cycles(CYCLES_ISR, cc[Z80_TABLE_op][0xcd] + cc[Z80_TABLE_ex][0xff]);
	}
	else
	/* Interrupt mode 1. RST 38h */
	if( IM == 1 )
	{
//		LOG(("Z80 #%d IM1 $0038\n",cpu_getactivecpu() ));
		PUSH( pc );
		PCD = 0x0038;
		/* RST $38 + 'interrupt latency' cycles */
		//bprintf(0, _T("imm1: opff: %d  exff: %d\n"),cc[Z80_TABLE_op][0xff], cc[Z80_TABLE_ex][0xff]);
		eat_cycles(CYCLES_ISR, cc[Z80_TABLE_op][0xff] + cc[Z80_TABLE_ex][0xff]);
	}
	else
	{
        // TMNT leaves the data bus vector at FF. It is a fixed RST38,
        // not an instruction supplied to a decoder. Reject other vectors.
        if (irq_vector != 0xff) { tmnt_z80_bad(); return; }
        PUSH(pc); PCD = 0x0038;
        eat_cycles(CYCLES_ISR, cc[Z80_TABLE_op][0xff] + cc[Z80_TABLE_ex][0xff]);
    }

	WZ=PCD;	
	change_pc(PCD);
}

void z80_set_cycle_tables_msx()
{
  tmnt_z80_unsupported();
}

void z80_set_cycle_tables(const UINT8 *op, const UINT8 *cb, const UINT8 *ed, const UINT8 *xy, const UINT8 *xycb, const UINT8 *ex)
{
  if (op || cb || ed || xy || xycb || ex) tmnt_z80_unsupported();
}

void Z80Init()
{
	int i, p;

	/* setup cycle tables */
	cc[Z80_TABLE_op] = cc_op;
	cc[Z80_TABLE_cb] = cc_cb;
	cc[Z80_TABLE_ed] = cc_ed;
	cc[Z80_TABLE_xy] = cc_xy;
	cc[Z80_TABLE_xycb] = cc_xycb;
	cc[Z80_TABLE_ex] = cc_ex;

#if BIG_FLAGS_ARRAY
	if( !SZHVC_add || !SZHVC_sub )
	{
		int oldval, newval, val;
		UINT8 *padd, *padc, *psub, *psbc;
		/* allocate big flag arrays once */
		SZHVC_add = (UINT8 *)malloc(2*256*256);
		SZHVC_sub = (UINT8 *)malloc(2*256*256);
		if( !SZHVC_add || !SZHVC_sub )
		{
//			fatalerror("Z80: failed to allocate 2 * 128K flags arrays!!!");
		}
		padd = &SZHVC_add[	0*256];
		padc = &SZHVC_add[256*256];
		psub = &SZHVC_sub[	0*256];
		psbc = &SZHVC_sub[256*256];
		for (oldval = 0; oldval < 256; oldval++)
		{
			for (newval = 0; newval < 256; newval++)
			{
				/* add or adc w/o carry set */
				val = newval - oldval;
				*padd = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
				*padd |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
				if( (newval & 0x0f) < (oldval & 0x0f) ) *padd |= HF;
				if( newval < oldval ) *padd |= CF;
				if( (val^oldval^0x80) & (val^newval) & 0x80 ) *padd |= VF;
				padd++;

				/* adc with carry set */
				val = newval - oldval - 1;
				*padc = (newval) ? ((newval & 0x80) ? SF : 0) : ZF;
				*padc |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
				if( (newval & 0x0f) <= (oldval & 0x0f) ) *padc |= HF;
				if( newval <= oldval ) *padc |= CF;
				if( (val^oldval^0x80) & (val^newval) & 0x80 ) *padc |= VF;
				padc++;

				/* cp, sub or sbc w/o carry set */
				val = oldval - newval;
				*psub = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
				*psub |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
				if( (newval & 0x0f) > (oldval & 0x0f) ) *psub |= HF;
				if( newval > oldval ) *psub |= CF;
				if( (val^oldval) & (oldval^newval) & 0x80 ) *psub |= VF;
				psub++;

				/* sbc with carry set */
				val = oldval - newval - 1;
				*psbc = NF | ((newval) ? ((newval & 0x80) ? SF : 0) : ZF);
				*psbc |= (newval & (YF | XF));	/* undocumented flag bits 5+3 */
				if( (newval & 0x0f) >= (oldval & 0x0f) ) *psbc |= HF;
				if( newval >= oldval ) *psbc |= CF;
				if( (val^oldval) & (oldval^newval) & 0x80 ) *psbc |= VF;
				psbc++;
			}
		}
	}
#endif
	for (i = 0; i < 256; i++)
	{
		p = 0;
		if( i&0x01 ) ++p;
		if( i&0x02 ) ++p;
		if( i&0x04 ) ++p;
		if( i&0x08 ) ++p;
		if( i&0x10 ) ++p;
		if( i&0x20 ) ++p;
		if( i&0x40 ) ++p;
		if( i&0x80 ) ++p;
		SZ[i] = i ? i & SF : ZF;
		SZ[i] |= (i & (YF | XF));		/* undocumented flag bits 5+3 */
		SZ_BIT[i] = i ? i & SF : ZF | PF;
		SZ_BIT[i] |= (i & (YF | XF));	/* undocumented flag bits 5+3 */
		SZP[i] = SZ[i] | ((p & 1) ? 0 : PF);
		SZHV_inc[i] = SZ[i];
		if( i == 0x80 ) SZHV_inc[i] |= VF;
		if( (i & 0x0f) == 0x00 ) SZHV_inc[i] |= HF;
		SZHV_dec[i] = SZ[i] | NF;
		if( i == 0x7f ) SZHV_dec[i] |= VF;
		if( (i & 0x0f) == 0x0f ) SZHV_dec[i] |= HF;
	}

	/* Reset registers to their initial values */
	memset(&Z80, 0, sizeof(Z80));
	Z80.hold_irq = 0;
	WZ = PCD;
//	Z80.daisy = config;
//	Z80.irq_callback = irqcallback;
	IX = IY = 0xffff; /* IX and IY are FFFF after a reset! */
	F = ZF;			/* Zero flag is set */
	Z80InitContention(0, NULL);
}

void Z80Contention_set_bank(int bankno)
{
	m_selected_bank = bankno;
}

static void raster_dummy_callback(int)
{
}

void Z80InitContention(int is_on_type, void (*rastercallback)(int))
{
  if (is_on_type || rastercallback) tmnt_z80_unsupported();
  m_tstate_counter = 0; m_selected_bank = 0;
}

void z80_set_spectrum_tape_callback(int (*tape_cb)())
{
  if (tape_cb) tmnt_z80_unsupported();
}

void Z80SetDaisy(void *dptr)
{
  if (dptr) tmnt_z80_unsupported();
}

void Z80Reset()
{
	memset(&Z80, 0, STRUCT_SIZE_HELPER(Z80_Regs, hold_irq)); // don't clear the callback pointers
	Z80.hold_irq = 0;

	PC = 0x0000;
	I = 0;
	R = 0;
	R2 = 0;
	WZ = PCD;
	Z80.nmi_state = Z80_CLEAR_LINE;
	Z80.nmi_pending = FALSE;
	Z80.irq_state = Z80_CLEAR_LINE;
	Z80.after_ei = FALSE;
	Z80.after_retn = FALSE;
	IX = IY = 0xffff; /* IX and IY are FFFF after a reset! */
	IFF1 = 0;
	IFF2 = 0;
	WZ = PCD;
	Z80Vector = 0xff;

	if (Z80.daisy)
		tmnt_z80_unsupported();

	change_pc(PCD);
	m_tstate_counter = 0;
	m_selected_bank = 0;
}

void Z80Exit()
{
	Z80.spectrum_tape_cb = NULL;
	Z80.spectrum_mode = 0;

    if (Z80.daisy) {
        tmnt_z80_unsupported();
    }

	if (SZHVC_add) free(SZHVC_add);
	SZHVC_add = NULL;
	if (SZHVC_sub) free(SZHVC_sub);
	SZHVC_sub = NULL;
	z80edfe_callback = NULL;
}

int Z80Execute(int cycles)
{
	Z80.ICount = cycles;
	Z80.cycles_left = cycles;
	Z80.end_run = 0;

	/* check for NMIs on the way in; they can only be set externally */
	/* via timers, and can't be dynamically enabled, so it is safe */
	/* to just check here */
	if (Z80.nmi_pending)
	{
//		LOG(("Z80 #%d take NMI\n", cpu_getactivecpu()));
		PRVPC = (UINT32)-1;			/* there isn't a valid previous program counter */
		LEAVE_HALT;			/* Check if processor was halted */

		IFF1 = 0;
		PUSH( pc );
		PCD = 0x0066;
		WZ=PCD;
		change_pc(PCD);
		eat_cycles(CYCLES_ISR, 11);
		Z80.nmi_pending = FALSE;
	}

	do
	{
		/* check for IRQs before each instruction */
		if (Z80.irq_state != Z80_CLEAR_LINE && IFF1 && !Z80.after_ei)
			take_interrupt();
		Z80.after_ei = FALSE;

		if (Z80.after_retn)
		{
			// https://floooh.github.io/2021/12/17/cycle-stepped-z80.html#the-ei-di-and-retiretn-instructions
			IFF1 = IFF2; // happens during fetch of next opcode
			Z80.after_retn = FALSE;
		}

		PRVPC = PCD;
//		CALL_DEBUGGER(PCD);
		R++;
        if (tmnt_native_failed() || !tmnt_z80_step()) {
            Z80.end_run = 1;
            break;
        }
	} while( Z80.ICount > 0 && !Z80.end_run && !tmnt_native_failed() );

	cycles = cycles - Z80.ICount;

	Z80.cycles_left = Z80.ICount = 0;

    if (Z80.daisy) {
        tmnt_z80_unsupported();
    }

	return cycles;
}

void Z80StopExecute()
{
	Z80.end_run = 1;
}

INT32 z80TotalCycles()
{
	return Z80.cycles_left - Z80.ICount;
}

INT32 z80TstateCounter() // for spectrum ula
{
	return m_tstate_counter;
}

void Z80Burn(int cycles)
{
	Z80.ICount -= cycles;
}

void Z80SetIrqLine(int irqline, int state)
{
	if (irqline == Z80_INPUT_LINE_NMI)
	{
		/* mark an NMI pending on the rising edge */
		if (Z80.nmi_state == Z80_CLEAR_LINE && state != Z80_CLEAR_LINE)
			Z80.nmi_pending = TRUE;
		Z80.nmi_state = state;
	}
	else
	{
		/* update the IRQ state via the daisy chain */
		Z80.irq_state = state;
		if (Z80.daisy)
			Z80.irq_state = tmnt_z80_unsupported();

		/* the main execute loop will take the interrupt */
	}
}

void Z80GetContext (void *dst)
{
	if( dst )
		*(Z80_Regs*)dst = Z80;
}

void Z80SetContext (void *src)
{
	if( src )
		Z80 = *(Z80_Regs*)src;
	change_pc(PCD);
}

int Z80Scan(int nAction)
{
    if (Z80.daisy) {
        tmnt_z80_unsupported();
	}

	if (m_ula_variant != ULA_VARIANT_NONE) {
		SCAN_VAR(m_tstate_counter);
		SCAN_VAR(m_selected_bank);
	}

	return 0;
}

void Z80SetIOReadHandler(Z80ReadIoHandler handler)
{
	Z80IORead = handler;
}

void Z80SetIOWriteHandler(Z80WriteIoHandler handler)
{
	Z80IOWrite = handler;
}

void Z80SetProgramReadHandler(Z80ReadProgHandler handler)
{
	Z80ProgramRead = handler;
}

void Z80SetProgramWriteHandler(Z80WriteProgHandler handler)
{
	Z80ProgramWrite = handler;
}

void Z80SetCPUOpReadHandler(Z80ReadOpHandler handler)
{
	Z80CPUReadOp = handler;
}

void Z80SetCPUOpArgReadHandler(Z80ReadOpArgHandler handler)
{
	Z80CPUReadOpArg = handler;
}

void ActiveZ80EXAF()
{
	EX_AF;
}

int ActiveZ80GetPC()
{
	return Z80.pc.w.l;
}

int ActiveZ80GetPOP()
{
	Z80_PAIR addr;
	RM16( SPD, &addr );
	SP += 2;
	return addr.w.l;
}

void ActiveZ80SetPC(int pc)
{
	Z80.pc.w.l = pc;
}

void ActiveZ80SetCarry(int carry)
{
	if (carry) {
		F |= CF;
	} else {
		F &= ~CF;
	}
}

int ActiveZ80GetCarry()
{
	return F & CF;
}

int ActiveZ80GetA()
{
	return A;
}

void ActiveZ80SetA(int a)
{
	A = a;
}

int ActiveZ80GetF()
{
	return F;
}

void ActiveZ80SetF(int f)
{
	F = f;
}

int ActiveZ80GetIFF1()
{
	return IFF1;
}

int ActiveZ80GetIFF2()
{
	return IFF2;
}

int ActiveZ80GetCarry2()
{
	return Z80.af2.b.l & CF;
}

int ActiveZ80GetAF()
{
	return Z80.af.w.l;
}

int ActiveZ80GetAF2()
{
	return Z80.af2.w.l;
}

void ActiveZ80SetAF2(int af2)
{
	Z80.af2.w.l = af2;
}

int ActiveZ80GetBC()
{
	return Z80.bc.w.l;
}

int ActiveZ80GetDE()
{
	return Z80.de.w.l;
}

void ActiveZ80SetDE(int de)
{
	Z80.de.w.l = de;
}

int ActiveZ80GetHL()
{
	return Z80.hl.w.l;
}

void ActiveZ80SetHL(int hl)
{
	Z80.hl.w.l = hl;
}

int ActiveZ80GetLastOp()
{
	return Z80lastop;
}

int ActiveZ80GetI()
{
	return Z80.i;
}

int ActiveZ80GetR()
{
	return Z80.r;
}

int ActiveZ80GetIX()
{
	return IX;
}

void ActiveZ80SetIX(int ix)
{
	IX = ix;
}

int ActiveZ80GetIM()
{
	return Z80.im;
}

int ActiveZ80GetSP()
{
	return Z80.sp.w.l;
}

void ActiveZ80SetSP(int sp)
{
	Z80.sp.w.l = sp;
}

int ActiveZ80GetPrevPC()
{
	return Z80.prvpc.w.l;
}

void ActiveZ80SetIRQHold()
{
	Z80.hold_irq = 1;
}

void ActiveZ80SetVector(INT32 vector)
{
	Z80Vector = vector;
}

int ActiveZ80GetVector()
{
	return Z80Vector;
}


static void eat_cycles(int, int cycles) { Z80.ICount -= cycles; m_tstate_counter += cycles; }
