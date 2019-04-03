#ifndef UTXOINTERNALERROR_H
#define UTXOINTERNALERROR_H

#include <stdexcept>

class UTXOInternalError : public std::runtime_error
{
public:
    UTXOInternalError(const char *error);
};

#endif
