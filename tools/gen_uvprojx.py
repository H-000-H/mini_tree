#!/usr/bin/env python3
"""Auto-discover sources and generate Keil MDK (.uvprojx) project file.

Usage:
    python tools/gen_uvprojx.py --platform arm_cm3 --toolchain keil5 --osal RTTHREAD
    python tools/gen_uvprojx.py --platform arm_cm4f --toolchain keil6 --osal FREERTOS
    python tools/gen_uvprojx.py --platform arm_cm7 --toolchain keil5 --osal NULL

Both keil5 and keil6 generate UVprojx targeting ARMCLANG (AC6) — not ARMCC v5.
No hardcoded file lists — scans directories for .c/.cpp/.S files.
"""

import sys
import os
import glob
import xml.dom.minidom as md
from xml.etree import ElementTree as ET

# ---------------------------------------------------------------------------
# platform -> Keil CPU/FPU mapping
# ---------------------------------------------------------------------------
PLATFORMS = {
    "arm_cm3":  {"cpu": "Cortex-M3",   "fpu": 0, "core": "STM32F103C8"},
    "arm_cm4f": {"cpu": "Cortex-M4FP", "fpu": 2, "core": "STM32F407VG"},
    "arm_cm7":  {"cpu": "Cortex-M7",   "fpu": 2, "core": "STM32F746NG"},
}

# arch port directory names (relative to lib/freeRTOS/portable/ or lib/rtthread/libcpu/)
ARM_PORTS = {
    "arm_cm3":  "ARM_CM3",
    "arm_cm4f": "ARM_CM4F",
    "arm_cm7":  "ARM_CM7",
}
RTTHREAD_ARCH = {
    "arm_cm3":  "arm/cortex-m3",
    "arm_cm4f": "arm/cortex-m4",
    "arm_cm7":  "arm/cortex-m7",
}

# FreeRTOS memory allocator
FREERTOS_HEAP = 4

# ---------------------------------------------------------------------------
# Source discovery — scan directories, no hardcoded lists
# ---------------------------------------------------------------------------
def scan_src(root_dir, pattern):
    """Scan recursively for files matching pattern, return relative paths."""
    root = os.path.abspath(root_dir)
    result = []
    for f in sorted(glob.glob(os.path.join(root, pattern), recursive=True)):
        rel = os.path.relpath(f, os.getcwd())
        result.append(rel.replace("\\", "/"))
    return result

def discover_sources(platform, toolchain, osal):
    """Auto-discover all source files for the given configuration."""
    source_groups = {}

    # framework modules: scan src/ directories
    source_groups["core"] = scan_src("core/src", "*.c") + scan_src("core/src", "*.cpp")
    source_groups["board"] = scan_src("board/src", "*.c")
    source_groups["hal_if"] = scan_src("hal_if/src", "*.c")
    source_groups["system"] = scan_src("system_cpp/src", "*.c") + scan_src("system_cpp/src", "*.cpp")
    source_groups["algorithm"] = scan_src("algorithm", "*.c") + scan_src("algorithm", "*.cpp")

    # exclude OSAL backends from system_cpp/src — they're in osal/src
    source_groups["osal"] = [f"osal/src/osal_{osal.lower()}.c"]

    # OSAL backend
    if osal == "NULL":
        # null backend has no extra kernel lib
        pass
    elif osal == "FREERTOS":
        port_dir = ARM_PORTS[platform]
        # FreeRTOS kernel sources
        srcs = scan_src("lib/freeRTOS/src", "*.c") + scan_src("lib/freeRTOS", "*.c")
        # exclude portable/ (architecture-specific)
        srcs = [s for s in srcs if not s.startswith("lib/freeRTOS/portable/")]
        # add common portable files
        srcs += [s for s in scan_src("lib/freeRTOS/portable/Common", "*.c")]
        # add memmang
        srcs += [f"lib/freeRTOS/portable/MemMang/heap_{FREERTOS_HEAP}.c"]
        # add arch port
        port_path = f"lib/freeRTOS/portable/GCC/{port_dir}"
        if os.path.isdir(port_path):
            srcs += scan_src(port_path, "*.c")
        source_groups["freertos_kernel"] = srcs
    elif osal == "RTTHREAD":
        # RT-Thread kernel sources
        srcs = scan_src("lib/rtthread/src", "*.c")
        # common arch files
        srcs += scan_src("lib/rtthread/libcpu/arm/common", "*.c")
        # arch-specific port
        arch_dir = RTTHREAD_ARCH[platform]
        arch_path = f"lib/rtthread/libcpu/{arch_dir}"
        if os.path.isdir(arch_path):
            srcs += scan_src(arch_path, "*.c")
            srcs += scan_src(arch_path, "*.S")
        source_groups["rtthread_kernel"] = srcs

    # DTS-generated sources (if pre-generated)
    gen_srcs = scan_src("build_make/board/generated", "*.c")
    if gen_srcs:
        source_groups["board_gen"] = gen_srcs

    return source_groups


# ---------------------------------------------------------------------------
# XML helpers
# ---------------------------------------------------------------------------
def make_sub(parent, tag, text=None):
    el = ET.SubElement(parent, tag)
    if text is not None:
        el.text = text
    return el

def indent(elem, level=0):
    i = "\n" + "  " * (level + 1)
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        for child in elem:
            indent(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = i
    if level and (not elem.tail or not elem.tail.strip()):
        elem.tail = i

# ---------------------------------------------------------------------------
# UVprojx generation
# ---------------------------------------------------------------------------
def generate(platform, toolchain, osal, output_dir="."):
    plat = PLATFORMS[platform]
    project_name = f"mini_tree_{platform}_{toolchain}_{osal.lower()}"

    source_groups = discover_sources(platform, toolchain, osal)

    # ── include paths ──
    includes = [
        "core\\include",
        "board\\include",
        "hal_if\\include",
        "osal\\include",
        "system_cpp\\include",
        "algorithm\\buffer",
        "board\\include",
        "lib\\freeRTOS\\include",
        "lib\\freeRTOS",
        "lib\\rtthread\\include",
        "lib\\rtthread",
        "RTE",
    ]
    # add arch-specific include for FreeRTOS port
    if osal == "FREERTOS":
        port_dir = ARM_PORTS[platform]
        inc = f"lib\\freeRTOS\\portable\\GCC\\{port_dir}"
        if os.path.isdir(inc.replace("\\", "/")):
            includes.append(inc)

    # ── preprocessor defines ──
    defines = {
        f"CONFIG_PLATFORM_{platform.upper().replace('ARM_', 'ARM_')}": "y",
        f"CONFIG_OSAL_{osal}": "y",
        "CONFIG_SYS_LOG_USE_PRINTF": "y",
    }
    if osal == "RTTHREAD":
        defines["__RT_KERNEL_SOURCE__"] = ""

    # ── compiler misc flags (AC6 / ARMCLANG) ──
    # Both keil5 and keil6 in the UVprojx target AC6 (ARMCLANG)
    misc_ctrl = "-std=c17 -Wall -fno-exceptions -fno-rtti"

    # ======================================================================
    # Build XML
    # ======================================================================
    proj = ET.Element("Project", {
        "xmlns:xsi": "https://www.w3.org/TR/xmlschema-1/",
        "xsi:noNamespaceSchemaLocation": "project_projx.xsd",
    })
    make_sub(proj, "SchemaVersion", "2.1")
    make_sub(proj, "Migrated", "8")

    targets = make_sub(proj, "Targets")
    tgt = make_sub(targets, "Target")
    make_sub(tgt, "TargetName", project_name)

    # -- TargetOption --
    topt = make_sub(tgt, "TargetOption")
    tard = make_sub(topt, "TargetArmAds")
    make_sub(tard, "Cpu", plat["cpu"])
    make_sub(tard, "FPU", str(plat["fpu"]))
    make_sub(tard, "Core", plat["core"])
    make_sub(tard, "Clock", "168000000")

    # memory
    mcx = make_sub(tard, "MemoryAreas")
    mm = make_sub(mcx, "MemoryArea")
    make_sub(mm, "Name", "Flash")
    make_sub(mm, "StartAddress", "0x08000000")
    make_sub(mm, "Size", "0x100000")
    make_sub(mm, "Type", "0")
    make_sub(mm, "Startup", "1")

    # C/C++
    cc = make_sub(tard, "Cads")
    make_sub(cc, "interw", "1")
    make_sub(cc, "Optim", "3")
    make_sub(cc, "OneELF", "1")
    make_sub(cc, "Warnings", "2")
    make_sub(cc, "EnumContainer", "1")

    misc = make_sub(cc, "VariousControls")
    make_sub(misc, "Define", ", ".join(f"{k}={v}" if v else k for k, v in defines.items()))
    make_sub(misc, "IncludePath", ";".join(includes))
    make_sub(misc, "MiscControls", misc_ctrl)

    # Assembler
    as_ = make_sub(tard, "Aads")
    make_sub(as_, "interw", "1")
    make_sub(as_, "Routine", "1")
    make_sub(as_, "Symbols", "1")
    make_sub(make_sub(as_, "VariousControls"), "MiscControls", "")

    # Linker
    ld = make_sub(tard, "Lads")
    larm = make_sub(ld, "LinkerArmAds")
    make_sub(larm, "LinkerType", "0")
    make_sub(larm, "ScatterFile", "")
    make_sub(larm, "UseLinker", "1")

    # Debug
    dgb = make_sub(topt, "DebugOption")
    sim = make_sub(dgb, "Simulator")
    make_sub(sim, "UseSimulator", "0")
    make_sub(sim, "SimDlls", "SARMCM3.DLL")
    make_sub(make_sub(dgb, "Dialog"), "Dialog", "DCM.DLL")

    # -- Groups & Files --
    groups_el = make_sub(tgt, "Groups")
    ngroup = 0
    for group_name, srcs in source_groups.items():
        if not srcs:
            continue
        ngroup += 1
        grp = make_sub(groups_el, "Group")
        make_sub(grp, "GroupName", group_name)
        files_el = make_sub(grp, "Files")
        for src in srcs:
            fe = make_sub(files_el, "File")
            make_sub(fe, "FileName", os.path.basename(src))
            make_sub(fe, "FileType", "1" if src.endswith(".c") else ("2" if src.endswith(".cpp") else "3"))
            make_sub(fe, "FilePath", src)

    if ngroup == 0:
        print("[gen_uvprojx] WARNING: no source files found!", file=sys.stderr)
        return 1

    # -- write --
    indent(proj)
    rough = ET.tostring(proj, encoding="unicode")
    dom = md.parseString(rough)
    pretty = dom.toprettyxml(indent="  ")

    output_path = os.path.join(output_dir, f"{project_name}.uvprojx")
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(pretty)
    print(f"[gen_uvprojx] Created: {output_path}  ({ngroup} groups, "
          f"{sum(len(v) for v in source_groups.values())} files)")
    return 0


# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Generate Keil MDK .uvprojx")
    parser.add_argument("--platform", choices=list(PLATFORMS), default="arm_cm3")
    parser.add_argument("--toolchain", choices=["keil5", "keil6"], default="keil5")
    parser.add_argument("--osal", choices=["FREERTOS", "RTTHREAD", "NULL"], default="RTTHREAD")
    parser.add_argument("--output", default=".")
    args = parser.parse_args()
    sys.exit(generate(args.platform, args.toolchain, args.osal, args.output))
