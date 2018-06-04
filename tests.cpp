/*
Copyright (C) 2018 by Ryan N. Lichtenwalter
Email: rlichtenwalter@gmail.com

This file is part of the Improved mRMR code base.

Improved mRMR is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Improved mRMR is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <array>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include "attribute_information.hpp"
#include "dataset.hpp"
#include "matrix.hpp"

std::string test( bool value ) {
	return value ? "PASSED" : "FAILED";
}

int main( int argc, char * argv[] ) {
	static_cast<void>( argc );
	static_cast<void>( argv );
	using value = unsigned char;
	std::array<value,16> a = { 0, 0, 0, 1, 1, 1, 0, 2, 2, 2, 1, 1, 0, 1, 1, 2 };
	attribute_information<value> ai( std::cbegin( a ), std::cend( a ) );
	std::cerr << "Testing attribute_information.num_values: " << test( ai.num_values() == 3 ) << std::endl;
	std::cerr << "Testing attribute_information.entropy: " << test( std::round( ai.entropy() * 1000000000000 ) == 1546179691947 ) << std::endl;
	std::cerr << "Testing attribute_information.marginal_probability: " << test( ai.marginal_probability( 0 ) == 5.0/16.0 && ai.marginal_probability( 1 ) == 7.0/16.0 && ai.marginal_probability( 2 ) == 4.0 / 16.0 ) << std::endl;
	matrix<double> m( 2, 3 );
	std::cerr << "Testing matrix.set and matrix.get: ";
	m( 0, 0 ) = 0.0;
	m( 0, 1 ) = 0.1;
	m( 0, 2 ) = 0.2;
	m( 1, 0 ) = 1.0;
	m( 1, 1 ) = 1.1;
	m( 1, 2 ) = 1.2;
	std::cerr << test( m( 0, 0 ) == 0.0 && m( 0, 1 ) == 0.1 && m( 0, 2 ) == 0.2 && m( 1, 0 ) == 1.0 && m( 1, 1 ) == 1.1 && m( 1, 2 ) == 1.2 ) << std::endl;
	std::cerr << "Testing operator<<( ostream &, matrix const & ): ";
	std::string s;
	std::stringstream ss( s );
	ss << m;
	std::cerr << test( ss.str() == "0\t0.1\t0.2\n1\t1.1\t1.2\n" ) << std::endl;
	std::cerr << "Testing operator>>( istream &, matrix & ): ";
	matrix<double> n;
	ss >> n;
	std::cerr << test( m == n ) << std::endl;
	std::cerr << "Testing matrix.transpose: ";
	matrix<double> t = m.transpose();
	std::cerr << test( t( 0, 0 ) == 0.0 && t( 0, 1 ) == 1.0 && t( 1, 0 ) == 0.1 && t( 1, 1 ) == 1.1 && t( 2, 0 ) == 0.2 && t( 2, 1 ) == 1.2 ) << std::endl;
	std::cerr << "Testing operator>>( istream &, dataset & ) and operator<<( ostream &, dataset & ): ";
	std::string str( "class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n" );
	std::stringstream dataset_ss( str );
	dataset<unsigned char> ds( dataset_ss, dataset<unsigned char>::ROUND );
	std::string str2;
	std::stringstream output_dataset_ss;
	output_dataset_ss << ds;
	std::cerr << test( str == output_dataset_ss.str() ) << std::endl;
	std::cerr << "Testing dataset.attribute_entropy: " << test( ds.attribute_entropy( 0 ) == 1 && ds.attribute_entropy( 1 ) == 1 && std::round( ds.attribute_entropy( 2 ) * 1000000000000 ) == 650022421648 ) << std::endl;
	std::cerr << "Testing dataset.mutual_information: " << test( round( ds.mutual_information( 0, 1 ) * 10000000 ) == 817042 && round( ds.mutual_information( 0, 2 ) * 10000000 ) == 1908745 )<< std::endl;
	return 0;
}

