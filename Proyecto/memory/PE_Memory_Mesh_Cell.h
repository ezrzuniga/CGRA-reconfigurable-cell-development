// PE_Memory_Mesh_Cell.h
// Envoltorio que expone el contrato uniforme de PE_Base (in/out N/S/E/W tipados
// sobre Link, instr_in de 1 parametro) sobre un PE_Memory_Cell interno sin
// modificar, para que una celda de memoria pueda ocupar una posicion del grid en
// CGRA_Mesh_Heterogeneous igual que las demas celdas.
//
// PE_Memory_Cell habla TLM-2.0 puro (csr_socket para configurar contextos de DMA,
// noc_target_socket/noc_initiator_socket para el trafico de datos), mientras que el
// resto de la malla habla senales planas (Link = PE_VectorData<DATA_W,VLEN>). Este
// envoltorio es el puente entre ambos mundos -- mismo rol que PE_Scalar_Cell hace
// entre sc_int<DATA_W> y Link, pero cruzando ademas TLM<->senales.
//
// Simplificaciones conscientes (documentadas, no bugs):
//  - Puerto NoC unico: PE_Memory_Cell fue disenado con UN solo par de sockets NoC
//    (no uno por direccion de malla), asi que este envoltorio los ata al borde
//    OESTE (in_W/out_W) unicamente -- en el layout 2x2 del diagrama de nivel 2 esa
//    es la celda de Enrutamiento, el hub logico de la malla. Norte/Sur/Este quedan
//    fisicamente cableados (para que la posicion en el grid sea uniforme) pero
//    inertes: nunca se leen ni se escriben.
//  - Cada palabra de Link mapea a 4 bytes en la lane 0 unicamente (dato escalar de
//    32 bits por transaccion), igual que el "TLM word" que ya asume LocalDMA
//    (LocalDMA::read_word_tlm/write_word_tlm siempre mueve 4 bytes).
//  - Configuracion via instr_in (reusa PE_InstrIn<DATA_W> en vez de inventar un
//    puerto nuevo): addr[1:0] selecciona el contexto (0..3, mismo mapa de 4
//    contextos que ya expone PE_Memory_Cell::contexts). instr.reg_dst selecciona
//    el campo dentro de ese contexto (0=src_addr,1=dst_addr,2=stride,3=count,
//    4=mode,5=dir); instr.imm es el valor a escribir. reg_dst=31 (0x1F) es el
//    comando especial "START": imm bit0 dispara el DMA de LocalDMA sobre el
//    contexto seleccionado por addr, exactamente igual que escribir el registro de
//    control 0x80 de PE_Memory_Cell.
//  - Estas escrituras se traducen 1:1 a transacciones TLM sobre csr_socket (mismo
//    mapa de registros ya validado por memory/PE_Memory_Cell_unit_test.cpp), asi
//    que PE_Memory_Cell en si no se toca.

#ifndef PE_MEMORY_MESH_CELL_H
#define PE_MEMORY_MESH_CELL_H

#include "../pe/PE_Base.h"
#include "PE_Memory_Cell.h"
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

// Selectores de campo de contexto para instr.reg_dst (ver comentario de arriba).
enum MemCellField {
    MEM_FIELD_SRC_ADDR = 0,
    MEM_FIELD_DST_ADDR = 1,
    MEM_FIELD_STRIDE   = 2,
    MEM_FIELD_COUNT    = 3,
    MEM_FIELD_MODE     = 4,
    MEM_FIELD_DIR      = 5,
    MEM_FIELD_START    = 31
};

// Construye la PE_Instruction<DATA_W> que, pasada a mesh.load_instr(row,col,ctx,...),
// escribe `value` en el campo `field` (ver MemCellField) del contexto `ctx` de una
// PE_Memory_Mesh_Cell. Helper compartido para que cualquier orquestador (testbenches,
// MeshWrapper) programe una celda de memoria sin reimplementar el mapeo de campos.
template <int DATA_W = 32>
inline PE_Instruction<DATA_W> make_memory_field_instr(int field, int32_t value) {
    PE_Instruction<DATA_W> instr;
    instr.reg_dst = field;
    instr.imm = value;
    return instr;
}

template <int DATA_W = 32, int VLEN = 4, int SIZE_BYTES = 2048>
class PE_Memory_Mesh_Cell : public PE_Base<DATA_W, VLEN> {
public:
    typedef PE_Base<DATA_W, VLEN> Base;
    typedef typename Base::Link Link;

    PE_Memory_Cell<SIZE_BYTES> inner;

    SC_HAS_PROCESS(PE_Memory_Mesh_Cell);

    explicit PE_Memory_Mesh_Cell(sc_core::sc_module_name name)
        : Base(name), inner("inner"),
          csr_init_("csr_init"), noc_target_("noc_target")
    {
        csr_init_.bind(inner.csr_socket);
        inner.noc_initiator_socket.bind(noc_target_);
        noc_target_.register_b_transport(this, &PE_Memory_Mesh_Cell::noc_b_transport);

        // inner.noc_target_socket (ingreso NoC directo a SRAM, alternativo al
        // propio LocalDMA de la celda) no se usa en este envoltorio -- ver
        // "Simplificaciones conscientes" arriba. simple_target_socket exige estar
        // bindeado para completar la elaboracion aunque nadie la invoque, asi que
        // se le ata un initiator dummy que nunca se usa.
        unused_noc_target_init_.bind(inner.noc_target_socket);

        SC_METHOD(bridge_instr_in);
        this->sensitive << this->instr_in;
    }

    void trace(sc_core::sc_trace_file* tf) const override {
        inner.trace(tf);
    }

    // Utilidad para el orquestador (MeshWrapper u otro CSR bridge): estado del DMA
    // local, leido directamente via TLM sobre csr_socket (registro de estado 0x84).
    bool dma_done() {
        return read_csr(0x84) & 0x2;
    }
    bool dma_busy() {
        return read_csr(0x84) & 0x1;
    }

private:
    tlm_utils::simple_initiator_socket<PE_Memory_Mesh_Cell> csr_init_;
    tlm_utils::simple_target_socket<PE_Memory_Mesh_Cell>    noc_target_;
    tlm_utils::simple_initiator_socket<PE_Memory_Mesh_Cell> unused_noc_target_init_;

    void write_csr(uint64_t addr, uint32_t value) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        trans.set_data_length(sizeof(uint32_t));
        trans.set_streaming_width(sizeof(uint32_t));
        csr_init_->b_transport(trans, delay);
    }

    uint32_t read_csr(uint64_t addr) {
        uint32_t value = 0;
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        trans.set_data_length(sizeof(uint32_t));
        trans.set_streaming_width(sizeof(uint32_t));
        csr_init_->b_transport(trans, delay);
        return value;
    }

    // Traduce cada escritura de instr_in en una transaccion TLM sobre csr_socket,
    // reusando el mapa de registros de PE_Memory_Cell tal cual (ctx*0x20 + offset
    // para los campos de contexto, 0x80 para el registro de control).
    void bridge_instr_in() {
        typename Base::InstrIn in = this->instr_in.read();
        if (!in.valid) return;

        uint32_t ctx = in.addr.to_uint() & 0x3;
        uint32_t field = in.instr.reg_dst.to_uint();
        uint32_t value = static_cast<uint32_t>(in.instr.imm.to_int());

        if (field == MEM_FIELD_START) {
            write_csr(0x80, (ctx << 1) | (value & 0x1));
            return;
        }
        if (field > MEM_FIELD_DIR) return;

        static const uint32_t kFieldOffset[6] = {0x00, 0x04, 0x08, 0x0c, 0x10, 0x14};
        write_csr(ctx * 0x20 + kFieldOffset[field], value);
    }

    // Unico punto NoC de la celda (ver "Simplificaciones conscientes" arriba):
    // atado al borde oeste. WRITE = LocalDMA empujando un dato hacia la malla
    // (dir=0, SRAM->NoC); READ = LocalDMA levantando un dato de la malla
    // (dir=1, NoC->SRAM).
    void noc_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        uint32_t* data_ptr = reinterpret_cast<uint32_t*>(trans.get_data_ptr());
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            Link out = this->out_W.read();
            out[0] = static_cast<sc_int<DATA_W>>(*data_ptr);
            this->out_W.write(out);
        } else if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            *data_ptr = static_cast<uint32_t>(this->in_W.read()[0].to_int());
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

#endif // PE_MEMORY_MESH_CELL_H
