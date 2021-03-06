/*
 *----------------------------------------------------------------------
 *    T2EX Software Package
 *
 *    Copyright 2015 by Nina Petipa.
 *    This software is distributed under the latest version of T-License 2.x.
 *----------------------------------------------------------------------
 *
 *----------------------------------------------------------------------
 */

#include <tk/typedef.h>
#include <tk/task.h>
#include <bk/uapi/ldt.h>
#include <tstdlib/bitop.h>
#include <cpu.h>
#include <sys/sysinfo.h>

#include <debug/vdebug.h>

/*
==================================================================================

	PROTOTYPE

==================================================================================
*/
LOCAL INLINE void setGdt(int gdt_num, uint32_t hi, uint32_t low);
LOCAL INLINE void setGateDesc(int idt_num, uint32_t dpl, uint32_t gate_type,
				uint32_t sel, unsigned long *handler);
LOCAL char* alloc_io_bitmap(unsigned long bitmap_size);

#define	setSysTaskGate(idt_num, tss_addr)					\
do {										\
	setGateDesc(idt_num, GATEDESC_DPL_0, GATEDESC_TASK,			\
			SEG_KERNEL_CS, tss_addr );				\
} while(0)

#define	setSysIntGate(idt_num, handler)						\
do {										\
	setGateDesc(idt_num, GATEDESC_DPL_0, GATEDESC_INT,			\
			SEG_KERNEL_CS, handler );				\
} while(0)

#define	setUserIntGate(idt_num, handler)					\
do {										\
	setGateDesc(idt_num, GATEDESC_DPL_3, GATEDESC_INT,			\
			SEG_KERNEL_CS, handler );				\
} while(0)

#define	setSysTrapGate(idt_num, handler)					\
do {										\
	setGateDesc(idt_num, GATEDESC_DPL_0, GATEDESC_TRAP,			\
			SEG_KERNEL_CS, handler );				\
} while(0)

#define	setUserTrapGate(idt_num, handler)					\
do {										\
	setGateDesc(idt_num, GATEDESC_DPL_3, GATEDESC_TRAP,			\
			SEG_KERNEL_CS, handler );				\
} while(0)


IMPORT unsigned long interrupt_0;
IMPORT unsigned long interrupt_1;
IMPORT unsigned long interrupt_2;
IMPORT unsigned long interrupt_3;
IMPORT unsigned long interrupt_4;
IMPORT unsigned long interrupt_5;
IMPORT unsigned long interrupt_6;
IMPORT unsigned long interrupt_7;
IMPORT unsigned long interrupt_8;
IMPORT unsigned long interrupt_9;
IMPORT unsigned long interrupt_10;
IMPORT unsigned long interrupt_11;
IMPORT unsigned long interrupt_12;
IMPORT unsigned long interrupt_13;
IMPORT unsigned long interrupt_14;

IMPORT unsigned long interrupt_16;
IMPORT unsigned long interrupt_17;
IMPORT unsigned long interrupt_18;
IMPORT unsigned long interrupt_19;

IMPORT unsigned long interrupt_32;
IMPORT unsigned long interrupt_33;
IMPORT unsigned long interrupt_34;
IMPORT unsigned long interrupt_35;
IMPORT unsigned long interrupt_36;
IMPORT unsigned long interrupt_37;
IMPORT unsigned long interrupt_38;
IMPORT unsigned long interrupt_39;
IMPORT unsigned long interrupt_40;
IMPORT unsigned long interrupt_41;
IMPORT unsigned long interrupt_42;
IMPORT unsigned long interrupt_43;
IMPORT unsigned long interrupt_44;
IMPORT unsigned long interrupt_45;
IMPORT unsigned long interrupt_46;
IMPORT unsigned long interrupt_47;

IMPORT unsigned long int_syscall;

IMPORT unsigned long int_syscall_mon;
IMPORT unsigned long int_tksyscall_svc;
IMPORT unsigned long int_tksyscall_retint;
IMPORT unsigned long int_tksyscall_dispatch;
IMPORT unsigned long int_tksyscall_debug;
IMPORT unsigned long int_tksyscall_rettex;


IMPORT unsigned long interrupt_255;
/*
==================================================================================

	DEFINE 

==================================================================================
*/
/*
----------------------------------------------------------------------------------
	Default Segment Descriptors
----------------------------------------------------------------------------------
*/
/* [0:NULL segment]								*/
#define	SEGDESC_NULL_LOW		0x00000000
#define	SEGDESC_NULL_HI			0x00000000

/* [1:Code segment]
   base address : 0x00000000
   segment limit: 0xFFFFF
   flags        : G=1, D/B=1, AVL=0, P=1, DPL=0, S=1, type=0xA
   */
#define	SEGDESC_CODE_LOW		0x0000FFFF
#define	SEGDESC_CODE_HI			0x00CF9A00

/* [2:Data segment]
   base address : 0x00000000
   segment limit: 0xFFFFF
   flags        : G=1, D/B=1, AVL=0, P=1, DPL=0, S=1, type=0x2
   */
#define	SEGDESC_DATA_LOW		0x0000FFFF
#define	SEGDESC_DATA_HI			0x00CF9200

/* [3:Kernel TSS]
   base address : address of tss
   segment limit: size of tss
   flags        : G=0, D/B=0, AVL=0, P=1, DPL=0, S=0, type=0x0
   */
#define	SEGDESC_TSS_HI_P		0x00008000
#define	SEGDESC_TSS_HI_DPL		(USER_RPL<<13)
#define	SEGDESC_TSS_HI_TYPE		0x00000900

#define	SEGDESC_TSS_LOW			0x00000000
#define	SEGDESC_TSS_HI			(SEGDESC_TSS_HI_P | SEGDESC_TSS_HI_DPL	\
						| SEGDESC_TSS_HI_TYPE)

/* [4:Task Code segment]
   base address : 0x00000000
   segment limit: 0xFFFFF
   flags        : G=1, D/B=1, AVL=0, P=1, DPL=0x3, S=1, type=0x2
   */
#define	SEGDESC_TASK_CODE_LOW		0x0000FFFF
#define	SEGDESC_TASK_CODE_HI		0x00CFFE00

/* [5:Task Data segment]
   base address : 0x00000000
   segment limit: 0xFFFFF
   flags        : G=1, D/B=1, AVL=0, P=1, DPL=0x3, S=1, type=0x2
   */
#define	SEGDESC_TASK_DATA_LOW		0x0000FFFF
#define	SEGDESC_TASK_DATA_HI		0x00CFF200

/* [6-8:Default TLS segment]
   base address : 0x00000000
   segment limit: 0xFFFFF
   flags        : G=1, D/B=1, AVL=0, P=1, DPL=0x3, S=1, type=0x2
   */
#define	SEGDESC_TLS_LOW			0x0000FFFF
#define	SEGDESC_TLS_HI			0x00CFF200


/*
----------------------------------------------------------------------------------
	Gate Descriptor
----------------------------------------------------------------------------------
*/
struct gate_desc {
	uint32_t	low;
	uint32_t	hi;
};

/* hi definitions								*/
#define	GATEDESC_MASK_OFFSET_HI		MAKE_MASK32(16, 31)
#define	GATEDESC_MASK_DPL		MAKE_MASK32(13, 14)
#define	GATEDESC_DPL_SHIFT		8

#define	GATEDESC_BIT_P			MAKE_BIT32(15)
#define	GATEDESC_BIT_D			MAKE_BIT32(11)

#define	GATEDESC_DPL_0			(0UL)
#define	GATEDESC_DPL_3			GATEDESC_MASK_DPL

#define	GATEDESC_TASK			0x00000500UL
#define	GATEDESC_INT			0x00000600UL
#define	GATEDESC_TRAP			0x00000700UL

/* low definitions								*/
#define	GATEDESC_MASK_SEGMENT_SEL	MAKE_MASK32(16, 31)
#define	GATEDESC_SEL_SHIFT		16
#define	GATEDESC_MASK_OFFSET_LOW	MAKE_MASK32(0, 15)

/*
----------------------------------------------------------------------------------
	Global Descriptor table
----------------------------------------------------------------------------------
*/
struct gdt_t {
	uint16_t		size;
	struct segment_desc	*addr;
}__attribute__ ((packed));

/*
----------------------------------------------------------------------------------
	Interrupt Descriptor table
----------------------------------------------------------------------------------
*/
struct idt_t {

	uint16_t		size;
	struct gate_desc	*addr;
}__attribute__ ((packed));

/*
----------------------------------------------------------------------------------
	Task-state segment
----------------------------------------------------------------------------------
*/
struct tss_t {
	uint16_t		prev_tss;
	uint16_t		reserved1;
	uint32_t		esp0;
	uint16_t		ss0;
	uint16_t		reserved2;
	uint32_t		esp1;
	uint16_t		ss1;
	uint16_t		reserved3;
	uint32_t		esp2;
	uint16_t		ss2;
	uint16_t		reserved4;
	uint32_t		cr3;
	uint32_t		eip;
	uint32_t		eflags;
	uint32_t		eax;
	uint32_t		ecx;
	uint32_t		edx;
	uint32_t		ebx;
	uint32_t		esp;
	uint32_t		ebp;
	uint32_t		esi;
	uint32_t		edi;
	uint16_t		es;
	uint16_t		reserved5;
	uint16_t		cs;
	uint16_t		reserved6;
	uint16_t		ss;
	uint16_t		reserved7;
	uint16_t		ds;
	uint16_t		reserved8;
	uint16_t		fs;
	uint16_t		reserved9;
	uint16_t		gs;
	uint16_t		reserved10;
	uint16_t		ldt_ss;
	uint16_t		reserved11;
	uint16_t		trap;
	uint16_t		io;
};
/*
==================================================================================

	Management 

==================================================================================
*/
LOCAL struct segment_desc segment_descriptor[MAX_NUM_DESCRIPTOR] __attribute__((aligned(4)));
LOCAL struct gate_desc gate_descriptor[NUM_IDT_DESCS] __attribute__((aligned(4)));

LOCAL struct gdt_t gdt __attribute__((aligned(4)));
LOCAL struct idt_t idt __attribute__((aligned(4)));

//LOCAL struct tss_t tss __attribute__((aligned(4)));
LOCAL struct tss_t *tss;
LOCAL char *io_bitmap;

/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	
	< Open Functions >

++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:initGdt
 Input		:void
 Output		:void
 Return		:void
 Description	:initialize global segment descriptor table
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT void initGdt(void)
{
	gdt.size = 0;
	/* -------------------------------------------------------------------- */
	/* null segment descriptor						*/
	/* -------------------------------------------------------------------- */
	setGdt(NULL_DESCRIPTOR, SEGDESC_NULL_HI, SEGDESC_NULL_LOW);
	gdt.size += sizeof(struct segment_desc);
	/* -------------------------------------------------------------------- */
	/* kernel code segment descriptor					*/
	/* -------------------------------------------------------------------- */
	setGdt(CODE_DESCRIPTOR, SEGDESC_CODE_HI, SEGDESC_CODE_LOW);
	gdt.size += sizeof(struct segment_desc);
	/* -------------------------------------------------------------------- */
	/* kernel data segment descriptor					*/
	/* -------------------------------------------------------------------- */
	setGdt(DATA_DESCRIPTOR, SEGDESC_DATA_HI, SEGDESC_DATA_LOW);
	gdt.size += sizeof(struct segment_desc);
	/* -------------------------------------------------------------------- */
	/* kernel tss descriptor						*/
	/* -------------------------------------------------------------------- */
	setGdt(TSS_DESCRIPTOR, SEGDESC_TSS_HI, SEGDESC_TSS_LOW);
	gdt.size += sizeof(struct segment_desc);
	/* -------------------------------------------------------------------- */
	/* user code descriptor							*/
	/* -------------------------------------------------------------------- */
	setGdt(TASK_CODE_DESCRIPTOR, SEGDESC_TASK_CODE_HI, SEGDESC_TASK_CODE_LOW);
	gdt.size += sizeof(struct segment_desc);
	/* -------------------------------------------------------------------- */
	/* user data descriptor							*/
	/* -------------------------------------------------------------------- */
	setGdt(TASK_DATA_DESCRIPTOR, SEGDESC_TASK_DATA_HI, SEGDESC_TASK_DATA_LOW);
	gdt.size += sizeof(struct segment_desc);
	/* -------------------------------------------------------------------- */
	/* tsl descriptors							*/
	/* -------------------------------------------------------------------- */
	setGdt(TLS1_DESCRIPTOR, SEGDESC_TLS_HI, SEGDESC_TLS_LOW);
	gdt.size += sizeof(struct segment_desc);
	setGdt(TLS2_DESCRIPTOR, SEGDESC_TLS_HI, SEGDESC_TLS_LOW);
	gdt.size += sizeof(struct segment_desc);
	setGdt(TLS3_DESCRIPTOR, SEGDESC_TLS_HI, SEGDESC_TLS_LOW);
	gdt.size += sizeof(struct segment_desc);

	/* -------------------------------------------------------------------- */
	/* load gdt								*/
	/* -------------------------------------------------------------------- */
	gdt.addr = &segment_descriptor[0];

	BARRIER();
	loadGdt((unsigned long*)((unsigned long)&gdt.size));
	BARRIER();
	//initKernelTss();
}

/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:initIdt
 Input		:void
 Output		:void
 Return		:void
 Description	:initialize interrupt descriptor table
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT void initIdt(void)
{
	int i;
	
	/* -------------------------------------------------------------------- */
	/* setup idt for exceptions						*/
	/* -------------------------------------------------------------------- */
	setSysTrapGate( 0, &interrupt_0);
	setSysTrapGate( 1, &interrupt_1);
	setSysTrapGate( 2, &interrupt_2);
	setSysTrapGate( 3, &interrupt_3);
	//setUserTrapGate(3, &interrupt_3);
	setSysTrapGate( 4, &interrupt_4);
	setSysTrapGate( 5, &interrupt_5);
	setSysTrapGate( 6, &interrupt_6);
	setSysTrapGate( 7, &interrupt_7);
	setSysTrapGate( 8, &interrupt_8);
	setSysTrapGate( 9, &interrupt_9);
	setSysTrapGate(10, &interrupt_10);
	setSysTrapGate(11, &interrupt_11);
	setSysTrapGate(12, &interrupt_12);
	setSysTrapGate(13, &interrupt_13);
	setSysTrapGate(14, &interrupt_14);

	setSysIntGate(15, &interrupt_255);
	
	setSysTrapGate(16, &interrupt_16);
	setSysTrapGate(17, &interrupt_17);
	setSysTrapGate(18, &interrupt_18);
	setSysTrapGate(19, &interrupt_19);
	/* -------------------------------------------------------------------- */
	/* setup idt for irqs							*/
	/* -------------------------------------------------------------------- */
	setSysIntGate(32, &interrupt_32);
	setSysIntGate(33, &interrupt_33);
	setSysIntGate(34, &interrupt_34);
	setSysIntGate(35, &interrupt_35);
	setSysIntGate(36, &interrupt_36);
	setSysIntGate(37, &interrupt_37);
	setSysIntGate(38, &interrupt_38);
	setSysIntGate(39, &interrupt_39);
	setSysIntGate(40, &interrupt_40);
	setSysIntGate(41, &interrupt_41);
	setSysIntGate(42, &interrupt_42);
	setSysIntGate(43, &interrupt_43);
	setSysIntGate(44, &interrupt_44);
	setSysIntGate(45, &interrupt_45);
	setSysIntGate(46, &interrupt_46);
	setSysIntGate(47, &interrupt_47);

	for (i=48;i<NUM_IDT_DESCS;i++) {
		setSysIntGate(i, &interrupt_255);
	}

	/* -------------------------------------------------------------------- */
	/* setup idt for system call						*/
	/* -------------------------------------------------------------------- */
	setUserTrapGate(SWI_SYSCALL, &int_syscall);
#if 0
	setSysTrapGate(SWI_MONITOR, &int_syscall_mon);
#endif
	setSysTrapGate(SWI_SVC, &int_tksyscall_svc);
#if 0
	setSysTrapGate(SWI_RETINT, &int_tksyscall_retint);
	setSysTrapGate(SWI_DISPATCH, &int_tksyscall_dispatch);
#endif
	setSysTrapGate(SWI_DEBUG, &int_tksyscall_debug);
#if 0
	setSysTrapGate(SWI_RETTEX, &int_tksyscall_rettex);
#endif

	
	/* -------------------------------------------------------------------- */
	/* load idt								*/
	/* -------------------------------------------------------------------- */
	idt.size = sizeof(struct gate_desc)*NUM_IDT_DESCS - 1;
	idt.addr = &gate_descriptor[0];
	
	BARRIER();
	ASM ("lidt	idt	\n\t");
}

/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:get_tss_esp0
 Input		:void
 Output		:void
 Return		:uint32_t*
 		 < address of esp0 of tss >
 Description	:get esp0 of tss
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT uint32_t *get_tss_esp0(void)
{
	return(&tss->esp0);
}


/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:initKernelTss
 Input		:void
 Output		:void
 Return		:int
 		 < result >
 Description	:initialize tss of kernel and load it to the register
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT int initKernelTss(void)
{
	//uint32_t base = (uint32_t)&tss;
	uint32_t base;
	uint32_t limit;
	uint32_t hi = SEGDESC_TSS_HI;
	uint32_t low;
	
	align_low_memory(4);
	
	tss = (struct tss_t*)allocLowMemory(sizeof(struct tss_t));
	
	if (!tss) {
		return(-1);
	}
	
	io_bitmap = alloc_io_bitmap(IO_BITMAP_SIZE);
	
	if (!io_bitmap) {
		return(-1);
	}
	
	base = (uint32_t)tss;
	limit = sizeof(tss) + IO_BITMAP_SIZE;

	/* -------------------------------------------------------------------- */
	/* setup tss descriptor							*/
	/* -------------------------------------------------------------------- */
	hi |= base & MASK_BASEADDRESS_HI;
	hi |= (base & MASK_BASEADDRESS_MID) >> BASEADDRESS_MID_SHIFT;
	hi |= limit & MASK_LIMIT_HI;

	low = (base & MASK_BASEADDRESS_LOW) << BASEADDRESS_LOW_SHIFT;
	low |= limit & SEGDESC_MASK_LIMIT_LOW;
	
	setGdt(TSS_DESCRIPTOR, hi, low);

	/* -------------------------------------------------------------------- */
	/* setup tss								*/
	/* -------------------------------------------------------------------- */
	tss->cs = SEG_KERNEL_CS;
	tss->ss = SEG_KERNEL_DS;
	tss->ds = SEG_KERNEL_DS;
	tss->fs = SEG_KERNEL_DS;
	tss->gs = SEG_KERNEL_DS;
	tss->ss0 = SEG_KERNEL_DS;
	tss->esp0 = getEsp();
	
	tss->io = (uint16_t)((unsigned long)io_bitmap - (unsigned long)tss);
	
	/* -------------------------------------------------------------------- */
	/* load tss								*/
	/* -------------------------------------------------------------------- */
	BARRIER();
	ASM (
	"movw %[tss_sel], %%ax		\n\t"
	"ltr %%ax			\n\t"
	:
	:[tss_sel]"i"(SEG_TSS_SEL)
	:"eax"
	);
	
	return(0);
}


/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:set_user_desc_to_seg_desc
 Input		:struct task *task
 		 < task to set its tls >
 		 struct user_desc *udesc
 		 < user descriptor to set >
 Output		:void
 Return		:void
 Description	:set user descriptor to task's tls
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT void set_user_desc_to_seg_desc(struct task *task, struct user_desc *udesc)
{
	unsigned long tls_hi = 0;
	unsigned long tls_low = 0;
	int current_index = udesc->entry_number - TLS_BASE_ENTRY;
	
	tls_hi |= udesc->base_addr & SEGDESC_MASK_BASEADDRESS_HI;
	
	tls_hi |= udesc->seg_32bit << SEGDESC_BIT_DB_SHIFT;
	tls_hi |= udesc->contents << (SEGDESC_BIT_TYPE_SHIFT + 2);
	tls_hi |= (udesc->read_exec_only ^ 1) << (SEGDESC_BIT_TYPE_SHIFT + 1);
	tls_hi |= 1 << SEGDESC_BIT_S_SHIFT;
	tls_hi |= USER_RPL << SEGDESC_BIT_DPL_SHIFT;
	tls_hi |= SEGDESC_BIT_P;
	tls_hi |= udesc->useable << SEGDESC_BIT_AVL_SHIFT;
	tls_hi |= udesc->limit_in_pages << SEGDESC_BIT_G_SHIFT;
	
	tls_hi |= udesc->limit & SEGDESC_MASK_LIMIT_HI;
	tls_hi |= (udesc->base_addr & MASK_BASEADDRESS_MID)
						>> BASEADDRESS_MID_SHIFT;
	
	tls_low |= (udesc->base_addr & MASK_BASEADDRESS_LOW)
						<< BASEADDRESS_LOW_SHIFT;
	tls_low |= udesc->limit & SEGDESC_MASK_LIMIT_LOW;
	
	task->tskctxb.tls_desc[current_index].hi = tls_hi;
	task->tskctxb.tls_desc[current_index].low = tls_low;
}

/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:update_tls_descriptor
 Input		:struct task *task
 		 < user task to update its tls descriptor in gdt >
 		 int gdt_index
 		 < index of tls segment descriptor in ddt to update >
 Output		:void
 Return		:void
 Description	:update user task's tls descriptor in gdt
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT void update_tls_descriptor(struct task *task, int gdt_index)
{
	struct segment_desc *tls;
	
	tls = &task->tskctxb.tls_desc[gdt_index - TLS_BASE_ENTRY];
	
	segment_descriptor[gdt_index].hi = tls->hi;
	segment_descriptor[gdt_index].low = tls->low;
}

/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:get_user_desc_from_seg_desc
 Input		:struct task *task
 		 < task to get its tls >
 		 struct user_desc *udesc
 		 < user descriptor to get >
 Output		:void
 Return		:void
 Description	:get user descriptor to task's tls
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
EXPORT void get_user_desc_from_seg_desc(struct task *task, struct user_desc *udesc)
{
	struct segment_desc *tls;
	
	tls = &task->tskctxb.tls_desc[udesc->entry_number - TLS_BASE_ENTRY];
	
	udesc->base_addr = tls->hi & SEGDESC_MASK_BASEADDRESS_HI;
	udesc->base_addr |= (tls->hi & SEGDESC_MASK_BASEADDRESS_MID)
					<< SEGDESC_BASEADDRESS_MID_SHIFT;
	udesc->base_addr |= tls->low & MASK_BASEADDRESS_LOW;
	
	udesc->limit = tls->hi & MASK_LIMIT_HI;
	udesc->limit |= tls->low & SEGDESC_MASK_LIMIT_LOW;
	
	udesc->seg_32bit = (tls->hi & SEGDESC_BIT_DB)?1:0;
	udesc->contents = (tls->hi & SEGDESC_MASK_TYPE)
					>> (SEGDESC_BIT_TYPE_SHIFT + 2);
	udesc->read_exec_only = (tls->hi & SEGDESC_MASK_TYPE_UPPER2)
					>> SEGDESC_BIT_TYPE_UPPER2_SHIFT;
	udesc->limit_in_pages = (tls->hi &  SEGDESC_BIT_G)?1:0;
	udesc->seg_not_present = (tls->hi & SEGDESC_BIT_P)?1:0;
	udesc->useable = (tls->hi & SEGDESC_BIT_AVL)?1:0;
} 

/*
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
 Funtion	:void
 Input		:void
 Output		:void
 Return		:void
 Description	:void
_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
*/
/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	
	< Local Functions >

++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
/*
==================================================================================
 Funtion	:setGdt
 Input		:int gdt_num
 		 < index of entry in gdt >
 		 uint32_t hi
 		 < high 4 byte of the entry >
 		 uint32_t low
 		 < low 4 byte of the entry >
 Output		:void
 Return		:void
 Description	:set segment descriptor in gdt
==================================================================================
*/
LOCAL INLINE void setGdt(int gdt_num, uint32_t hi, uint32_t low)
{
	segment_descriptor[gdt_num].hi = hi;
	segment_descriptor[gdt_num].low = low;
}

/*
==================================================================================
 Funtion	:setGateDesc
 Input		:int idt_num
 		 < index of entry in idt >
 		 uint32_t dpl
 		 < descriptor privilege level >
 		 uint32_t gate_type
 		 < type of gate >
 		 uint32_t sel
 		 < selector >
 		 unsigned long handler
 		 < address of handler >
 Output		:void
 Return		:void
 Description	:set gate descriptor in idt
==================================================================================
*/
LOCAL INLINE void setGateDesc(int idt_num, uint32_t dpl, uint32_t gate_type,
				uint32_t sel, unsigned long *handler)
{
	unsigned long addr = (unsigned long)handler;
	/* -------------------------------------------------------------------- */
	/* set offset								*/
	/* -------------------------------------------------------------------- */
	gate_descriptor[idt_num].hi = addr & GATEDESC_MASK_OFFSET_HI;
	gate_descriptor[idt_num].low = addr & GATEDESC_MASK_OFFSET_LOW;
	/* -------------------------------------------------------------------- */
	/* set flags								*/
	/* -------------------------------------------------------------------- */
	gate_descriptor[idt_num].hi |= gate_type;
	if (dpl) {
		gate_descriptor[idt_num].hi |= GATEDESC_DPL_3;
	}
	gate_descriptor[idt_num].hi |= GATEDESC_BIT_P | GATEDESC_BIT_D;
	/* -------------------------------------------------------------------- */
	/* set selector								*/
	/* -------------------------------------------------------------------- */
	gate_descriptor[idt_num].low |=
			(sel << GATEDESC_SEL_SHIFT) & GATEDESC_MASK_SEGMENT_SEL;
}

/*
==================================================================================
 Funtion	:alloc_io_bitmap
 Input		:unsigned long bitmap_size
 		 < size of bitmap to allocate >
 Output		:void
 Return		:char*
 		 < bitmap address >
 Description	:allocate memory for io bitmap
==================================================================================
*/
LOCAL char* alloc_io_bitmap(unsigned long bitmap_size)
{
	int i;
	char *bitmap;
	
	bitmap = (char*)allocLowMemory(bitmap_size);
	
	if (!bitmap) {
		return(0);
	}
	
	/* -------------------------------------------------------------------- */
	/* 0-clear								*/
	/* -------------------------------------------------------------------- */
	for (i = 0;i < (IO_BITMAP_SIZE - 1);i++) {
		bitmap[i] = 0x00;
	}
	/* -------------------------------------------------------------------- */
	/* last byte is filled with 1						*/
	/* -------------------------------------------------------------------- */
	bitmap[IO_BITMAP_SIZE - 1] = 0xFF;
	
	return(bitmap);
}

/*
==================================================================================
 Funtion	:void
 Input		:void
 Output		:void
 Return		:void
 Description	:void
==================================================================================
*/



