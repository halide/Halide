.global _enable_neon
_enable_neon:
    MRC p15,0,r0,c1,c0,2    // Read CP Access register
    ORR r0,r0,#0x00f00000   // Enable full access to NEON/VFP by enabling access to Coprocessors 10 and 11
    MCR p15,0,r0,c1,c0,2    // Write CP Access register
    ISB
    MOV r0,#0x40000000      // Switch on the VFP and NEON hardware
    VMSR FPEXC,r0           // Set EN bit in FPEXC

    // Jump to C runtime, you may need to change the label name
    // depending on the boot up sequence and what runtime you linked.
    BL _mainCRTStartup
