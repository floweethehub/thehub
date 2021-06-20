/*
 * This file is part of the flowee project
 * Copyright (C) 2017-2018 Tom Zander <tom@flowee.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ValidationException.h"

Validation::Exception::Exception(const std::string &error, int punishment)
    : runtime_error(error),
      m_punishment(punishment),
      m_rejectCode(Validation::RejectInvalid)
{
}

Validation::Exception::Exception(const std::string &error, Validation::CorruptionPossible p)
    : runtime_error(error),
      m_punishment(100),
      m_rejectCode(Validation::RejectInvalid),
      m_corruptionPossible(true)
{
}

Validation::Exception::Exception(const std::string &error, Validation::RejectCodes rejectCode, int punishment)
    : Exception(error, punishment)
{
    m_rejectCode = rejectCode;
}


Validation::DoubleSpendException::DoubleSpendException(const Tx &otherTx, int dspProofId)
    : Exception("", 0),
      otherTx(otherTx),
      id(dspProofId)
{
}
