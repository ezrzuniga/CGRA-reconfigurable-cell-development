// test_util.h
// Helper minimo de impresion para testbenches: encabezados de fase (con el
// ciclo de reloj acumulado) y un PASS/FAIL que siempre muestra
// entrada/esperado/obtenido, no solo el valor obtenido. No es un framework
// de verificacion, solo formato de salida.

#ifndef PE_TEST_UTIL_H
#define PE_TEST_UTIL_H

#include <systemc.h>
#include <iostream>
#include <string>

inline int& test_cycle_total() { static int c = 0; return c; }
inline int& test_cycle_at_last_section() { static int c = 0; return c; }

// Reemplazo de sc_start(cycles*period_ns, SC_NS) que ademas lleva la cuenta
// acumulada de ciclos, para que test_section() pueda reportarla.
inline void advance_cycles(int cycles, double period_ns = 10.0) {
    sc_start(cycles * period_ns, SC_NS);
    test_cycle_total() += cycles;
}

// Encabezado de fase; incluye el ciclo acumulado y cuantos pasaron desde la
// seccion anterior, para poder ver de un vistazo cuantos ciclos separan una
// transicion de la siguiente sin agregar una linea extra por cada sc_start.
inline void test_section(const std::string& title) {
    int total = test_cycle_total();
    int delta = total - test_cycle_at_last_section();
    std::cout << "\n==== " << title << " (ciclo " << total << ", +" << delta << ") ====" << std::endl;
    test_cycle_at_last_section() = total;
}

template <typename T>
void test_check(bool& ok, const std::string& label, const std::string& inputs,
                 const T& expected, const T& got) {
    bool pass = (got == expected);
    std::cout << (pass ? "PASS " : "FAIL ") << label << "\n"
               << "  entrada  : " << inputs << "\n"
               << "  esperado : " << expected << "\n"
               << "  obtenido : " << got << std::endl;
    if (!pass) ok = false;
}

// Como test_check, pero para celdas escalares dentro de una malla heterogenea:
// el wire de la malla es el mismo vector de VLEN lanes para las 3 variantes de
// celda por igual, aunque una celda escalar solo tenga un valor real (las
// demas lanes son una copia por broadcast). Compara el vector completo (sigue
// verificando que el broadcast sea consistente en todas las lanes) pero
// imprime solo la lane 0, para no sugerir comportamiento por-lane que esa
// celda no tiene.
template <typename T>
void test_check_scalar(bool& ok, const std::string& label, const std::string& inputs,
                        const T& expected, const T& got) {
    bool pass = (got == expected);
    std::cout << (pass ? "PASS " : "FAIL ") << label << "\n"
               << "  entrada  : " << inputs << "\n"
               << "  esperado : " << expected[0] << "\n"
               << "  obtenido : " << got[0] << std::endl;
    if (!pass) ok = false;
}

// Reporta en una sola linea el resultado agregado de una condicion booleana
// (ej. "se sostuvo la misma tasa durante N ciclos seguidos") -- para checks
// donde no tiene sentido mostrar un unico esperado/obtenido porque el
// resultado ya resume varias muestras.
inline void test_check_bool(bool& ok, const std::string& label,
                             const std::string& inputs, bool pass) {
    std::cout << (pass ? "PASS " : "FAIL ") << label << "\n"
               << "  entrada  : " << inputs << std::endl;
    if (!pass) ok = false;
}

#endif // PE_TEST_UTIL_H
