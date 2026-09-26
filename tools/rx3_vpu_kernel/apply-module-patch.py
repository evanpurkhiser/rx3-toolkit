#!/usr/bin/env python3
"""Apply the RX3 VPU module compatibility changes with strict context checks."""

from pathlib import Path


source_path = Path("/build/mxc_vpu.c.orig")
target_path = Path("/build/mxc_vpu.c")
source = source_path.read_text(encoding="utf-8")

replacements = (
    ("#include <linux/types.h>\n#include <linux/memblock.h>\n", "#include <linux/types.h>\n"),
    (
        "static u32 phy_vpu_base_addr;\nstatic phys_addr_t top_address_DRAM;\n",
        "static u32 phy_vpu_base_addr;\n"
        "static unsigned long top_address_DRAM;\n"
        "module_param_named(dram_top, top_address_DRAM, ulong, 0444);\n"
        'MODULE_PARM_DESC(dram_top, "highest valid RX3 physical RAM address");\n',
    ),
    (
        "static int __init vpu_init(void)\n"
        "{\n"
        "\tint ret = platform_driver_register(&mxcvpu_driver);\n"
        "\n"
        "\tinit_waitqueue_head(&vpu_queue);\n"
        "\n"
        "\n"
        "\tmemblock_analyze();\n"
        "\ttop_address_DRAM = memblock_end_of_DRAM_with_reserved();\n",
        "static int __init vpu_init(void)\n"
        "{\n"
        "\tint ret;\n"
        "\n"
        "\tif (!top_address_DRAM) {\n"
        '\t\tprintk(KERN_ERR "vpu: dram_top module parameter is required\\n");\n'
        "\t\treturn -EINVAL;\n"
        "\t}\n"
        "\n"
        "\tret = platform_driver_register(&mxcvpu_driver);\n"
        "\n"
        "\tinit_waitqueue_head(&vpu_queue);\n",
    ),
)

for old, new in replacements:
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"patch context occurs {count} times; expected exactly once")
    source = source.replace(old, new)

target_path.write_text(source, encoding="utf-8")
