#ifndef NETWORKEXCEPTION_H
#define NETWORKEXCEPTION_H

#include <stdexcept>

class NetworkException : public std::runtime_error
{
public:
    NetworkException(const char *error);
};

#endif
