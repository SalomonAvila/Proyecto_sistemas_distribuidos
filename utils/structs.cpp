#include <iostream>
#include <cstdint>


//ENUMERACIONES
enum struct RequestType{
    LOAN,
    RENEWAL,
    RETURN
};


struct Request{
    RequestType requestType;
    std::int32_t code;
    std::int8_t location;
};