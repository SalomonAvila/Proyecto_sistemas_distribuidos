#include <iostream>
#include <cstdint>


//enum for the request type
enum struct RequestType{
    LOAN,
    RENEWAL,
    RETURN
};

//Structure for handling requests
struct Request{
    RequestType requestType;
    std::int32_t code;
    std::int8_t location;
};