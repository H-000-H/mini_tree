/*
 * FreeRTOS Kernel V11.3.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * mini_tree: single file supporting all RISC-V extension configurations.
 * Selected by the FREERTOS_RISCV_EXTENSION compile definition.
 */

#ifndef FREERTOS_RISC_V_CHIP_SPECIFIC_EXTENSIONS_H
#define FREERTOS_RISC_V_CHIP_SPECIFIC_EXTENSIONS_H

/* ── RISCV_no_extensions ────────────────────────────────────── */
#ifdef RISCV_no_extensions

    #define portasmHAS_MTIME              0
    #define portasmHAS_SIFIVE_CLINT       0
    #define portasmADDITIONAL_CONTEXT_SIZE 0
    #define portasmSAVE_ADDITIONAL_REGISTERS
    #define portasmRESTORE_ADDITIONAL_REGISTERS

/* ── RISCV_MTIME_CLINT_no_extensions ────────────────────────── */
#elif defined(RISCV_MTIME_CLINT_no_extensions)

    #define portasmHAS_MTIME              1
    #define portasmHAS_SIFIVE_CLINT       0
    #define portasmADDITIONAL_CONTEXT_SIZE 0
    #define portasmSAVE_ADDITIONAL_REGISTERS
    #define portasmRESTORE_ADDITIONAL_REGISTERS

/* ── RV32I_CLINT_no_extensions ──────────────────────────────── */
#elif defined(RV32I_CLINT_no_extensions)

    #define portasmHAS_MTIME              1
    #define portasmHAS_SIFIVE_CLINT       1
    #define portasmADDITIONAL_CONTEXT_SIZE 0
    #define portasmSAVE_ADDITIONAL_REGISTERS
    #define portasmRESTORE_ADDITIONAL_REGISTERS

#else
    #error "Unknown FREERTOS_RISCV_EXTENSION. Supported: RISCV_no_extensions, RISCV_MTIME_CLINT_no_extensions, RV32I_CLINT_no_extensions"
#endif

#endif /* FREERTOS_RISC_V_CHIP_SPECIFIC_EXTENSIONS_H */
