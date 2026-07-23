#include "dummy_cgra.h"

#include <cstring>
#include <iostream>

using namespace std;

DummyCGRA::DummyCGRA(sc_module_name name)
    :
    sc_module(name),
    socket("socket")
{
    socket.register_b_transport(
        this,
        &DummyCGRA::b_transport);


    configuration = 0;
    start = 0;
    status = 0;
    done = 0;
}

void DummyCGRA::compute()
{
    status = 1;
    done = 0;


    switch(configuration)
    {
        case VECTOR_ADD:

            vector_add();
            break;


        case FIR_FILTER:

            fir_filter();
            break;


        case FFT_8_POINTS:

            fft();
            break;


        default:

            SC_REPORT_ERROR(
                "DUMMY_CGRA",
                "Unsupported kernel.");
    }


    status = 0;
    done = 1;
}

void DummyCGRA::vector_add()
{
    output_buffer.resize(input_buffer.size());

    for(size_t i = 0; i < input_buffer.size(); ++i)
    {
        output_buffer[i] = 2 * input_buffer[i];
    }
}

void DummyCGRA::fir_filter()
{
    output_buffer.clear();
    output_buffer.resize(input_buffer.size());

    const int h0 = 1;
    const int h1 = 2;
    const int h2 = 1;


    for (size_t i = 0; i < input_buffer.size(); ++i)
    {
        int xn   = input_buffer[i];

        int xn1 = (i >= 1) ? input_buffer[i - 1] : 0;

        int xn2 = (i >= 2) ? input_buffer[i - 2] : 0;


        output_buffer[i] =
            static_cast<uint8_t>(
                h0 * xn +
                h1 * xn1 +
                h2 * xn2);
    }

}

void DummyCGRA::fft()
{
    
}