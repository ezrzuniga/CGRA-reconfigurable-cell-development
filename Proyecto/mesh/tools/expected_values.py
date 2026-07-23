#!/usr/bin/env python3
"""Calculadora de los valores "esperado" usados en los testbenches de mesh/.

Por que existe: los pipelines de CGRA_Mesh_ComplexTest__TB.cpp encadenan 3
operaciones (algunas con SUB/OR/XOR y operandos negativos) sobre enteros de
32 bits con signo -- exactamente la semantica de sc_int<32> en SystemC
(complemento a 2, wraparound silencioso en overflow). Calcular eso a mano es
facil de errar (un bit mal en un AND/XOR no se nota a simple vista). Este
script es la fuente de verdad: cualquier literal "esperado" que aparezca en
un test de mesh/ debe poder reproducirse corriendo esto, no solo confiarse a
ojo.

Uso: correr `python3 mesh/tools/expected_values.py` desde la raiz del repo (o
desde cualquier lado, no tiene dependencias). Imprime, seccion por seccion,
los mismos valores que hoy estan escritos como literales en
CGRA_Mesh_SmokeTest__TB.cpp y CGRA_Mesh_ComplexTest__TB.cpp. Si se agrega un
caso nuevo a esos tests, agregar aca la llamada correspondiente ANTES de
escribir el literal en C++, y pegar el resultado de esta corrida -- no
calcular el opcode a mano.
"""

MASK32 = 0xFFFFFFFF
SIGN32 = 0x80000000


def s32(x: int) -> int:
    """Trunca a 32 bits y reinterpreta con signo (misma semantica que
    sc_int<32>: complemento a 2, sin excepcion en overflow)."""
    x &= MASK32
    return x - 0x100000000 if x >= SIGN32 else x


def add(a: int, b: int) -> int: return s32(a + b)
def sub(a: int, b: int) -> int: return s32(a - b)
def mul(a: int, b: int) -> int: return s32(a * b)
def AND(a: int, b: int) -> int: return s32((a & MASK32) & (b & MASK32))
def OR(a: int, b: int) -> int:  return s32((a & MASK32) | (b & MASK32))
def XOR(a: int, b: int) -> int: return s32((a & MASK32) ^ (b & MASK32))


def pipeline3(op1, op2, op3, a: int, k1: int, k2: int, k3: int):
    """3 etapas encadenadas SRC_WEST->DST_EAST (fila 0/1 de ComplexTest):
    c = op1(a,k1); e = op2(c,k2); g = op3(e,k3). Devuelve (c, e, g)."""
    c = op1(a, k1)
    e = op2(c, k2)
    g = op3(e, k3)
    return c, e, g


def lanes(op1, op2, op3, lane_values, k1: int, k2: int, k3: int):
    """pipeline3 aplicado lane por lane (fila 1, vectorial: mismas
    constantes k1/k2/k3 para las 4 lanes, "a" varia por lane)."""
    return [pipeline3(op1, op2, op3, a, k1, k2, k3)[2] for a in lane_values]


def section(title: str) -> None:
    print(f"\n---- {title} ----")


if __name__ == "__main__":
    section("SmokeTest: fila 0 (ADD), fila 1 (MUL k=3), fila 2 (MAC k=10)")
    print("estimulo inicial   : fila0 c=", add(5, 7),
          "| fila1 e=", [mul(a, 3) for a in (1, 2, 3, 4)],
          "| fila2 delta=", mul(2, 10), mul(3, 10), mul(4, 10), mul(5, 10))
    print("2da transicion     : fila0 c=", add(10, -3),
          "| fila1 e=", [mul(a, 3) for a in (4, 5, 6, 7)],
          "| fila2 delta(a=1)=", mul(1, 10))
    print("3ra transicion     : fila0 c=", add(20, 5),
          "| fila1 e=", [mul(a, 3) for a in (8, 6, 4, 2)],
          "| fila2 delta(a=3)=", mul(3, 10))

    section("SmokeTest reprogramado: fila 0 (SUB), fila 1 (AND k=5), fila 2 (MAC k=5)")
    print("post-reprog (a=20,5)     :", sub(20, 5))
    print("post-reprog fila1 (stale):", [AND(a, 5) for a in (8, 6, 4, 2)])
    print("post-reprog fila2 delta(a=3,k=5):", mul(3, 5))
    print("transicion 1 : fila0=", sub(15, 4),
          "| fila1=", [AND(a, 5) for a in (9, 10, 11, 12)],
          "| fila2 delta(a=2)=", mul(2, 5))
    print("transicion 2 : fila0=", sub(0, -8),
          "| fila1=", [AND(a, 5) for a in (6, 7, 13, 20)],
          "| fila2 delta(a=4)=", mul(4, 5))
    print("transicion 3 : fila0=", sub(-5, -5),
          "| fila1=", [AND(a, 5) for a in (31, 16, 3, 0)],
          "| fila2 delta(a=1)=", mul(1, 5))

    section("ComplexTest: fila 0/1 pipeline v1 (ADD->MUL->AND), fila 2 (MAC k=2)")
    print("estimulo inicial: fila0(a=5,b=7,d=3,f=15) g=",
          pipeline3(add, mul, AND, 5, 7, 3, 15)[2])
    print("estimulo inicial: fila1(k1=7,k2=3,k3=15) g=",
          lanes(add, mul, AND, (5, 9, 13, 17), 7, 3, 15))
    print("2da transicion  : fila0(a=8) g=", pipeline3(add, mul, AND, 8, 7, 3, 15)[2])
    print("2da transicion  : fila1(k1=7,k2=3,k3=15) g=",
          lanes(add, mul, AND, (2, 4, 6, 8), 7, 3, 15))
    print("3ra transicion  : fila0(a=10) g=", pipeline3(add, mul, AND, 10, 7, 3, 15)[2])
    print("3ra transicion  : fila1(k1=7,k2=3,k3=15) g=",
          lanes(add, mul, AND, (3, 12, 20, 1), 7, 3, 15))

    section("ComplexTest reprogramado: fila 0/1 pipeline v2 (SUB->OR->XOR), fila 2 (MAC k=3)")
    K1, K2, K3 = 2, 6, 9  # constantes nuevas de la fila 1 (v2)
    print("post-reprog fila0 (stale a=10,b=7,d=3,f=15) g=",
          pipeline3(sub, OR, XOR, 10, 7, 3, 15)[2])
    print("post-reprog fila1 (stale) g=",
          lanes(sub, OR, XOR, (3, 12, 20, 1), K1, K2, K3))
    print("transicion 1: fila0(a=20) g=", pipeline3(sub, OR, XOR, 20, 7, 3, 15)[2])
    print("transicion 1: fila1 g=", lanes(sub, OR, XOR, (5, 15, 25, 35), K1, K2, K3))
    print("transicion 2: fila0(a=0) g=", pipeline3(sub, OR, XOR, 0, 7, 3, 15)[2])
    print("transicion 2: fila1 g=", lanes(sub, OR, XOR, (-2, -10, -18, -26), K1, K2, K3))
    print("transicion 3: fila0(a=-15) g=", pipeline3(sub, OR, XOR, -15, 7, 3, 15)[2])
    print("transicion 3: fila1 g=", lanes(sub, OR, XOR, (40, 50, 60, 70), K1, K2, K3))
