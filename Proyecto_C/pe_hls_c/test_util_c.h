// test_util_c.h
// Transliteracion a C/C++ puro de pe_hls/test_util.h: mismo formato de salida
// (encabezado de fase con el ciclo acumulado, PASS/FAIL que siempre muestra
// entrada/esperado/obtenido), sin systemc.h.
//
// Diferencia estructural con el original: advance_cycles() ya no existe aca.
// En SystemC "avanzar un ciclo" era sc_start(), un servicio del kernel de
// simulacion global, asi que tenia sentido que el helper de impresion lo
// envolviera para llevar la cuenta. En C/C++ puro "avanzar un ciclo" es una
// llamada a mesh_step() sobre UNA malla concreta -- el helper no puede (ni
// debe) saber sobre cual. Cada testbench define su propio step()/step_n()
// contra su malla y llama a test_count_cycles(n) para que test_section()
// pueda seguir reportando el ciclo acumulado igual que antes.
//
// test_check comparaba con operator== del tipo; PE_VectorData<DATA_W,VLEN>
// (pe_isa_hls_c.h) no define operator== (el original SI lo hacia, pero solo
// porque sc_signal<T> lo exige para detectar eventos -- fuera de SystemC no
// hace falta y se dejo fuera del header sintetizable). Por eso las
// comparaciones de Link viven aca, en el header de testbench, y no en la ISA.

#ifndef PE_TEST_UTIL_C_H
#define PE_TEST_UTIL_C_H

#include <iostream>
#include <string>
#include "pe_isa_hls_c.h"

inline int& test_cycle_total() { static int c = 0; return c; }
inline int& test_cycle_at_last_section() { static int c = 0; return c; }

// Sustituto de advance_cycles(): el testbench corre los ciclos contra su
// propia malla y solo reporta cuantos corrio (ver comentario de cabecera).
inline void test_count_cycles(int cycles) { test_cycle_total() += cycles; }

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

inline void test_check_bool(bool& ok, const std::string& label,
                             const std::string& inputs, bool pass) {
    std::cout << (pass ? "PASS " : "FAIL ") << label << "\n"
              << "  entrada  : " << inputs << std::endl;
    if (!pass) ok = false;
}

// ---- Comparacion/impresion de Link (ver comentario de cabecera) -----------
template <int DATA_W, int VLEN>
inline bool link_equal(const PE_VectorData<DATA_W, VLEN>& a, const PE_VectorData<DATA_W, VLEN>& b) {
    for (int i = 0; i < VLEN; i++) if (a[i].to_int() != b[i].to_int()) return false;
    return true;
}

template <int DATA_W, int VLEN>
inline std::string link_to_string(const PE_VectorData<DATA_W, VLEN>& v) {
    std::string s = "[";
    for (int i = 0; i < VLEN; i++) {
        s += std::to_string(v[i].to_int());
        if (i + 1 < VLEN) s += ", ";
    }
    return s + "]";
}

template <int DATA_W, int VLEN>
inline void test_check_link(bool& ok, const std::string& label, const std::string& inputs,
                             const PE_VectorData<DATA_W, VLEN>& expected,
                             const PE_VectorData<DATA_W, VLEN>& got) {
    bool pass = link_equal(expected, got);
    std::cout << (pass ? "PASS " : "FAIL ") << label << "\n"
              << "  entrada  : " << inputs << "\n"
              << "  esperado : " << link_to_string(expected) << "\n"
              << "  obtenido : " << link_to_string(got) << std::endl;
    if (!pass) ok = false;
}

// Como test_check_link, pero para celdas escalares dentro de una malla
// heterogenea: compara el vector completo (sigue verificando que el broadcast
// sea consistente en las VLEN lanes) e imprime solo la lane 0, para no
// sugerir un comportamiento por-lane que esa celda no tiene.
template <int DATA_W, int VLEN>
inline void test_check_scalar(bool& ok, const std::string& label, const std::string& inputs,
                               const PE_VectorData<DATA_W, VLEN>& expected,
                               const PE_VectorData<DATA_W, VLEN>& got) {
    bool pass = link_equal(expected, got);
    std::cout << (pass ? "PASS " : "FAIL ") << label << "\n"
              << "  entrada  : " << inputs << "\n"
              << "  esperado : " << expected[0].to_int() << "\n"
              << "  obtenido : " << got[0].to_int() << std::endl;
    if (!pass) ok = false;
}

#endif // PE_TEST_UTIL_C_H
