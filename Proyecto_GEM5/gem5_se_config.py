"""
gem5_se_config.py
Corre sum_reduction_workload en modo SE (syscall emulation) de gem5, sobre
un solo core, con el modelo de CPU y la ISA que elijas por linea de
comandos. Default: RISC-V (RV64GC), para que coincida con el core real del
proyecto CGRA (RISC-V RV32IMAF como puente CSR/DMA).

Uso (desde la raiz del repo de gem5 ya compilado):
    build/RISCV/gem5.opt Proyecto_gem5/gem5_se_config.py \
        --binary /ruta/a/sum_reduction_workload_riscv64 \
        --cpu atomic
    build/RISCV/gem5.opt Proyecto_gem5/gem5_se_config.py \
        --binary /ruta/a/sum_reduction_workload_riscv64 \
        --cpu o3

Modelos de CPU disponibles (gem5.components.processors.cpu_types.CPUTypes):
    atomic  -- AtomicSimpleCPU:  1 instr/ciclo asumido, no modela memoria en
               detalle. numCycles ~= numInsts, util solo para funcional, NO
               para comparar rendimiento entre disenos.
    timing  -- TimingSimpleCPU:  in-order, SI modela latencia de memoria
               (timing accesses), pero sigue siendo 1 instr en vuelo.
    o3      -- DerivO3CPU:       out-of-order, superscalar, la comparacion
               mas representativa de una CPU real moderna.
    minor   -- MinorCPU:         in-order con pipeline mas detallado que
               TimingSimpleCPU (fetch/decode/execute/writeback en etapas).

Para comparar de verdad, correr el mismo binario con --cpu atomic, --cpu
timing, --cpu minor y --cpu o3 y compará board.processor.cores[0].core.numCycles
en cada m5out/stats.txt (ver mas abajo donde encontrarlo).
"""

import argparse
import os

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes, get_cpu_type_from_str
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA, get_isa_from_str
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

parser = argparse.ArgumentParser(
    description="Corre sum_reduction_workload en gem5 modo SE."
)
parser.add_argument(
    "--binary",
    type=str,
    required=True,
    help="Ruta al binario compilado para la ISA elegida con --isa.",
)
parser.add_argument(
    "--isa",
    type=str,
    default="riscv",
    choices=["riscv", "x86", "arm"],
    help="ISA simulada (default: riscv, para que coincida con el core "
         "RISC-V real del proyecto CGRA)."
         "y con la ISA para la que compilaste --binary.",
)
parser.add_argument(
    "--cpu",
    type=str,
    default="atomic",
    choices=["atomic", "timing", "minor", "o3"],
    help="Modelo de CPU a simular (default: atomic).",
)
args = parser.parse_args()

if not os.path.isfile(args.binary):
    raise FileNotFoundError(
        f"No se encontro el binario en '{args.binary}'. "
        "Compilalo primero (ver README.md de esta carpeta segun --isa)."
    )

cpu_type = get_cpu_type_from_str(args.cpu)
isa = get_isa_from_str(args.isa)

# ---- Un solo core, sin cache (el kernel es minusculo, no hace falta
#      modelar jerarquia de memoria para este experimento) -----------------
processor = SimpleProcessor(cpu_type=cpu_type, num_cores=1, isa=isa)
memory = SingleChannelDDR3_1600(size="256MiB")
cache_hierarchy = NoCache()

board = SimpleBoard(
    clk_freq="1GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

board.set_se_binary_workload(
    binary=BinaryResource(local_path=args.binary),
)

simulator = Simulator(board=board)
simulator.run()

print(
    f"\n=== gem5 SE termino: causa de salida = "
    f"{simulator.get_last_exit_event_cause()} ==="
)
print(
    "Ciclos/instrucciones: ver 'board.processor.cores[0].core.numCycles' "
    "y '...numInsts' en m5out/stats.txt (se genera en el directorio desde "
    "donde corriste gem5.opt)."
)
