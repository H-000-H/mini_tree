#!/usr/bin/env python3
"""Auto-discover sources and generate Keil MDK (.uvprojx) project file.

Usage:
    python tools/gen_uvprojx.py --platform arm_cm3 --toolchain keil5 --osal RTTHREAD
    python tools/gen_uvprojx.py --platform arm_cm4f --toolchain keil6 --osal FREERTOS
    python tools/gen_uvprojx.py --platform arm_cm7 --toolchain keil5 --osal NULL

Both keil5 and keil6 generate UVprojx targeting ARMCLANG (AC6) — not ARMCC v5.
No hardcoded file lists — scans directories for .c/.cpp/.S files.
"""

from __future__ import annotations

import glob
import os
import sys
import xml.dom.minidom as md
from typing import Dict, List, Optional
from xml.etree import ElementTree as ET


# ---------------------------------------------------------------------------
# platform -> Keil CPU/FPU mapping
# ---------------------------------------------------------------------------
PLATFORMS: Dict[str, Dict[str, object]] = {
    "arm_cm3":  {"cpu": "Cortex-M3",   "fpu": 0},
    "arm_cm4f": {"cpu": "Cortex-M4FP", "fpu": 2},
    "arm_cm7":  {"cpu": "Cortex-M7",   "fpu": 2},
}

ARM_PORTS: Dict[str, str] = {
    "arm_cm3":  "ARM_CM3",
    "arm_cm4f": "ARM_CM4F",
    "arm_cm7":  "ARM_CM7",
}

RTTHREAD_ARCH: Dict[str, str] = {
    "arm_cm3":  "arm/cortex-m3",
    "arm_cm4f": "arm/cortex-m4",
    "arm_cm7":  "arm/cortex-m7",
}

FREERTOS_HEAP: int = 4


# ---------------------------------------------------------------------------
# Source discovery — scan directories, no hardcoded lists
# ---------------------------------------------------------------------------
def scan_src(root_dir: str, pattern: str) -> List[str]:
    """Scan recursively for files matching pattern, return relative paths."""
    root: str = os.path.abspath(root_dir)
    result: List[str] = []
    for f in sorted(glob.glob(os.path.join(root, pattern), recursive=True)):
        rel: str = os.path.relpath(f, os.getcwd())
        result.append(rel.replace("\\", "/"))
    return result


def discover_sources(platform: str, toolchain: str, osal: str) -> Dict[str, List[str]]:
    """Auto-discover all source files for the given configuration."""
    source_groups: Dict[str, List[str]] = {}

    source_groups["core"] = scan_src("core/src", "*.c") + scan_src("core/src", "*.cpp")
    source_groups["board"] = scan_src("board/src", "*.c")
    source_groups["hal_if"] = scan_src("hal_if/src", "*.c")
    source_groups["system"] = scan_src("system_cpp/src", "*.c") + scan_src("system_cpp/src", "*.cpp")
    source_groups["algorithm"] = scan_src("algorithm", "*.c") + scan_src("algorithm", "*.cpp")

    source_groups["osal"] = [f"osal/src/osal_{osal.lower()}.c"]

    if osal == "FREERTOS":
        port_dir: str = ARM_PORTS[platform]
        srcs: List[str] = scan_src("lib/freeRTOS/src", "*.c") + scan_src("lib/freeRTOS", "*.c")
        srcs = [s for s in srcs if not s.startswith("lib/freeRTOS/portable/")]
        srcs += [s for s in scan_src("lib/freeRTOS/portable/Common", "*.c")]
        srcs += [f"lib/freeRTOS/portable/MemMang/heap_{FREERTOS_HEAP}.c"]
        port_path: str = f"lib/freeRTOS/portable/GCC/{port_dir}"
        if os.path.isdir(port_path):
            srcs += scan_src(port_path, "*.c")
        source_groups["freertos_kernel"] = srcs

    elif osal == "RTTHREAD":
        srcs = scan_src("lib/rtthread/src", "*.c")
        srcs += scan_src("lib/rtthread/libcpu/arm/common", "*.c")
        arch_dir: str = RTTHREAD_ARCH[platform]
        arch_path: str = f"lib/rtthread/libcpu/{arch_dir}"
        if os.path.isdir(arch_path):
            srcs += scan_src(arch_path, "*.c")
            srcs += scan_src(arch_path, "*.S")
        source_groups["rtthread_kernel"] = srcs

    gen_srcs: List[str] = scan_src("build_make/board/generated", "*.c")
    if gen_srcs:
        source_groups["board_gen"] = gen_srcs

    return source_groups


# ---------------------------------------------------------------------------
# XML helpers
# ---------------------------------------------------------------------------
def make_sub(parent: ET.Element, tag: str, text: Optional[str] = None) -> ET.Element:
    el: ET.Element = ET.SubElement(parent, tag)
    if text is not None:
        el.text = text
    return el


def indent(elem: ET.Element, level: int = 0) -> None:
    i: str = "\n" + "  " * (level + 1)
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
def generate(platform: str, toolchain: str, osal: str,
             output_dir: str = ".",
             core_model: str = "ARMCM3",
             clock_hz: int = 16000000,
             flash_base: str = "0x08000000",
             flash_size: str = "0x100000") -> int:
    plat: Dict[str, object] = PLATFORMS[platform]
    project_name: str = f"mini_tree_{platform}_{toolchain}_{osal.lower()}"
    source_groups: Dict[str, List[str]] = discover_sources(platform, toolchain, osal)

    includes: List[str] = [
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
    if osal == "FREERTOS":
        port_dir: str = ARM_PORTS[platform]
        inc: str = f"lib\\freeRTOS\\portable\\GCC\\{port_dir}"
        if os.path.isdir(inc.replace("\\", "/")):
            includes.append(inc)

    defines: Dict[str, str] = {
        f"CONFIG_PLATFORM_{platform.upper().replace('ARM_', 'ARM_')}": "y",
        f"CONFIG_OSAL_{osal}": "y",
        "CONFIG_SYS_LOG_USE_PRINTF": "y",
    }
    if osal == "RTTHREAD":
        defines["__RT_KERNEL_SOURCE__"] = ""

    misc_ctrl: str = "-std=c17 -Wall -fno-exceptions -fno-rtti"

    # ======================================================================
    # Build XML
    # ======================================================================
    proj: ET.Element = ET.Element("Project", {
        "xmlns:xsi": "https://www.w3.org/TR/xmlschema-1/",
        "xsi:noNamespaceSchemaLocation": "project_projx.xsd",
    })
    make_sub(proj, "SchemaVersion", "2.1")
    make_sub(proj, "Migrated", "8")

    targets: ET.Element = make_sub(proj, "Targets")
    tgt: ET.Element = make_sub(targets, "Target")
    make_sub(tgt, "TargetName", project_name)

    topt: ET.Element = make_sub(tgt, "TargetOption")
    tard: ET.Element = make_sub(topt, "TargetArmAds")
    make_sub(tard, "Cpu", str(plat["cpu"]))
    make_sub(tard, "FPU", str(plat["fpu"]))
    make_sub(tard, "Core", core_model)
    make_sub(tard, "Clock", str(clock_hz))

    mcx: ET.Element = make_sub(tard, "MemoryAreas")
    mm: ET.Element = make_sub(mcx, "MemoryArea")
    make_sub(mm, "Name", "Flash")
    make_sub(mm, "StartAddress", flash_base)
    make_sub(mm, "Size", flash_size)
    make_sub(mm, "Type", "0")
    make_sub(mm, "Startup", "1")

    cc: ET.Element = make_sub(tard, "Cads")
    make_sub(cc, "interw", "1")
    make_sub(cc, "Optim", "3")
    make_sub(cc, "OneELF", "1")
    make_sub(cc, "Warnings", "2")
    make_sub(cc, "EnumContainer", "1")

    misc: ET.Element = make_sub(cc, "VariousControls")
    make_sub(misc, "Define", ", ".join(f"{k}={v}" if v else k for k, v in defines.items()))
    make_sub(misc, "IncludePath", ";".join(includes))
    make_sub(misc, "MiscControls", misc_ctrl)

    as_: ET.Element = make_sub(tard, "Aads")
    make_sub(as_, "interw", "1")
    make_sub(as_, "Routine", "1")
    make_sub(as_, "Symbols", "1")
    make_sub(make_sub(as_, "VariousControls"), "MiscControls", "")

    ld: ET.Element = make_sub(tard, "Lads")
    larm: ET.Element = make_sub(ld, "LinkerArmAds")
    make_sub(larm, "LinkerType", "0")
    make_sub(larm, "ScatterFile", "")
    make_sub(larm, "UseLinker", "1")

    dgb: ET.Element = make_sub(topt, "DebugOption")
    sim: ET.Element = make_sub(dgb, "Simulator")
    make_sub(sim, "UseSimulator", "0")
    make_sub(sim, "SimDlls", "SARMCM3.DLL")
    make_sub(make_sub(dgb, "Dialog"), "Dialog", "DCM.DLL")

    groups_el: ET.Element = make_sub(tgt, "Groups")
    ngroup: int = 0
    for group_name, srcs in source_groups.items():
        if not srcs:
            continue
        ngroup += 1
        grp: ET.Element = make_sub(groups_el, "Group")
        make_sub(grp, "GroupName", group_name)
        files_el: ET.Element = make_sub(grp, "Files")
        for src in srcs:
            fe: ET.Element = make_sub(files_el, "File")
            make_sub(fe, "FileName", os.path.basename(src))
            file_type: str = (
                "1" if src.endswith(".c")
                else ("2" if src.endswith(".cpp") else "3")
            )
            make_sub(fe, "FileType", file_type)
            make_sub(fe, "FilePath", src)

    if ngroup == 0:
        print("[gen_uvprojx] WARNING: no source files found!", file=sys.stderr)
        return 1

    indent(proj)
    rough: str = ET.tostring(proj, encoding="unicode")
    dom: md.Document = md.parseString(rough)
    pretty: str = dom.toprettyxml(indent="  ")

    output_path: str = os.path.join(output_dir, f"{project_name}.uvprojx")
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(pretty)
    print(
        f"[gen_uvprojx] Created: {output_path}  ({ngroup} groups, "
        f"{sum(len(v) for v in source_groups.values())} files)"
    )
    return 0


# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Generate Keil MDK .uvprojx")
    parser.add_argument("--platform", choices=list(PLATFORMS), default="arm_cm3")
    parser.add_argument("--toolchain", choices=["keil5", "keil6"], default="keil5")
    parser.add_argument("--osal", choices=["FREERTOS", "RTTHREAD", "NULL"], default="RTTHREAD")
    parser.add_argument("--core", default="ARMCM3",
                        help="Keil device/core name (e.g. STM32F407VG)")
    parser.add_argument("--clock", type=int, default=16000000,
                        help="CPU clock in Hz")
    parser.add_argument("--flash-base", default="0x08000000",
                        help="Flash base address (e.g. 0x08000000)")
    parser.add_argument("--flash-size", default="0x100000",
                        help="Flash size in bytes (e.g. 0x100000)")
    parser.add_argument("--output", default=".")
    args = parser.parse_args()
    sys.exit(generate(args.platform, args.toolchain, args.osal, args.output,
                      args.core, args.clock, args.flash_base, args.flash_size))
