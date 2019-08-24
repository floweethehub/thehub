/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#ifndef FLOWEE_BOOST_COMPAT
#define FLOWEE_BOOST_COMPAT

#include <boost/version.hpp>

// boost didn't leave much of backwards compatibility in this one.
#if BOOST_VERSION < 106600
# define BoostCompatStrand boost::asio::strand
# include <boost/asio/strand.hpp>
#else
# define BoostCompatStrand boost::asio::io_context::strand
# include <boost/asio/io_context_strand.hpp>
#endif

#endif
