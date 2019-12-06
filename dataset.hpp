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

#ifndef MRMR_DATASET_HPP
#define MRMR_DATASET_HPP

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <valarray>
#include <vector>
#include "attribute_information.hpp"
#include "delimiter_ctype.hpp"
#include "matrix.hpp"
#include "typedef.hpp"

extern char DELIMITER;

template <typename T>
class dataset {
	template <typename U> friend std::ostream & operator<<( std::ostream & os, dataset<U> const & m );
	public:
		using value_type = T;
		enum discretization_method : char {
			ROUND = 0,
			FLOOR = 1,
			CEILING = 2
		};
		dataset();
		dataset( std::istream &, discretization_method dm = ROUND );
		std::size_t num_instances() const;
		std::size_t num_attributes() const;
		std::string attribute_name( std::size_t attribute_num ) const;
		double attribute_entropy( std::size_t attribute_num ) const;
		double mutual_information( std::size_t attribute1, std::size_t attribute2 ) const;
	private:
		std::vector<std::string> _names;
		std::vector<attribute_information<T> > _attr_info;
		matrix<T> _data;
};

template <typename T>
dataset<T>::dataset() : _data( 0, 0 ) {
}

template <typename T>
dataset<T>::dataset( std::istream & is, discretization_method dm ) {
	// the pointer below is managed via the interface
	is.imbue( std::locale( is.getloc(), new delimiter_ctype( DELIMITER ) ) );

	// read header line with attribute names
	std::string name;
	while( is.good() && is.peek() != '\n' ) {
		is >> name;
		_names.push_back( name );
	}
	if( is.peek() != '\n' ) {
		std::cerr << "error: missing required newline after header\n";
		exit( 2 );
	}

	// read data matrix
	matrix<double> temp;
	is >> temp;

	// prepare matrix for computation by transposing and performing requested discretization procedure
	_data = matrix<T>( num_attributes(), temp.num_rows() );
	switch( dm ) {
		case ROUND:
			for( std::size_t instance_num = 0; instance_num < num_instances(); ++instance_num ) {
				for( std::size_t attribute_num = 0; attribute_num < num_attributes(); ++attribute_num ) {
					_data( attribute_num, instance_num ) = std::round( temp( instance_num, attribute_num ) );
				}
			}
			break;
		case FLOOR:
			for( std::size_t instance_num = 0; instance_num < num_instances(); ++instance_num ) {
				for( std::size_t attribute_num = 0; attribute_num < num_attributes(); ++attribute_num ) {
					_data( attribute_num, instance_num ) = std::floor( temp( instance_num, attribute_num ) );
				}
			}
			break;
		case CEILING:
			for( std::size_t instance_num = 0; instance_num < num_instances(); ++instance_num ) {
				for( std::size_t attribute_num = 0; attribute_num < num_attributes(); ++attribute_num ) {
					_data( attribute_num, instance_num ) = std::ceil( temp( instance_num, attribute_num ) );
				}
			}
			break;	
		default: // truncate (equivalent to FLOOR method above)
			for( std::size_t instance_num = 0; instance_num < num_instances(); ++instance_num ) {
				for( std::size_t attribute_num = 0; attribute_num < num_attributes(); ++attribute_num ) {
					_data( attribute_num, instance_num ) = temp( instance_num, attribute_num );
				}
			}
			break;
	}

	// perform basic attribute computations and cache results
	_attr_info.reserve( num_attributes() );
	for( std::size_t attribute_num = 0; attribute_num < num_attributes(); ++attribute_num ) {
		auto attribute_begin = &_data( attribute_num, 0 );
		auto attribute_end = attribute_begin + num_instances();
		_attr_info.emplace_back( attribute_begin, attribute_end );
	}
}

template <typename T>
std::size_t dataset<T>::num_instances() const {
	return _data.num_columns();
}

template <typename T>
std::size_t dataset<T>::num_attributes() const {
	return _names.size();
}

template <typename T>
std::string dataset<T>::attribute_name( std::size_t attribute_num ) const {
	return _names[ attribute_num ];
}

template <typename T>
double dataset<T>::attribute_entropy( std::size_t attribute_num ) const {
	return _attr_info[ attribute_num ].entropy();
}

template <typename T>
double dataset<T>::mutual_information( std::size_t attribute1, std::size_t attribute2 ) const {
	std::size_t a1_num_values = _attr_info.at( attribute1 ).num_values();
	std::size_t a2_num_values = _attr_info.at( attribute2 ).num_values();
	if( a1_num_values == 1 || a2_num_values == 1 ) {
		return 0.0;
	}
	std::valarray<std::size_t> histogram( static_cast<std::size_t>( 0 ), a1_num_values * a2_num_values );
	for( std::size_t i = 0; i < num_instances(); ++i ) {
		++histogram[ _data( attribute1, i ) * a2_num_values + _data( attribute2, i ) ];
	}
	std::valarray<probability> joint_probabilities( histogram.size() );
	std::copy( std::cbegin( histogram ), std::cend( histogram ), std::begin( joint_probabilities ) );
	joint_probabilities /= static_cast<double>( num_instances() );

	double mutual_information = 0.0;
	for( std::size_t i = 0; i < a1_num_values; ++i ) {
		for( std::size_t j = 0; j < a2_num_values; ++j ) {
			probability joint_probability = joint_probabilities[ i * a2_num_values + j ];
			if( joint_probability != 0 ) {
				probability marginal_probability_i = _attr_info.at( attribute1 ).marginal_probability( i );
				probability marginal_probability_j = _attr_info.at( attribute2 ).marginal_probability( j );
				mutual_information += joint_probability * std::log2( joint_probability / ( marginal_probability_i * marginal_probability_j ) );
			}
		}
	}
	return mutual_information;
}

template <typename T>
std::ostream & operator<<( std::ostream & os, dataset<T> const & data ) {
	if( data.num_attributes() > 0 ) {
		os << data._names.at( 0 );
		for( std::size_t i = 1; i < data.num_attributes(); ++i ) {
			os << DELIMITER << data._names.at( i );
		}
		os << '\n';
		os << data._data.transpose();
	}
	return os;
}



#endif
