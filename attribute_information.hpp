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

#ifndef MRMR_ATTRIBUTE_INFORMATION_HPP
#define MRMR_ATTRIBUTE_INFORMATION_HPP

#include <array>
#include <cassert>
#include <iterator>
#include <limits>
#include <valarray>
#include "typedef.hpp"

template <typename T>
class attribute_information {
	public:
		template <typename ForwardIterator> attribute_information( ForwardIterator first, ForwardIterator last );
		T num_values() const;
		double entropy() const;
		probability marginal_probability( T index ) const;
	private:
		double _entropy;
		std::valarray<probability> _pdf;
};

template <typename T>
template <typename ForwardIterator>
attribute_information<T>::attribute_information( ForwardIterator first, ForwardIterator last ) {
	// determine number of elements
	std::size_t count = last - first;

	// compute temporary histogram on fast integral type
	std::array<unsigned int,std::numeric_limits<T>::max()> temp_histogram = {};
	while( first != last ) {
		++temp_histogram[*first];
		++first;
	}

	// find maximum populated value in histogram and populate storage-optimized final PDF
	auto begin = std::cbegin( temp_histogram );
	auto end = std::cend( temp_histogram );
	while( *(--end) == 0 ) {}
	++end;
	_pdf.resize( end - begin );
	std::copy( begin, end, std::begin( _pdf ) );
	_pdf = _pdf / static_cast<double>( count );

	// compute entropy
	_entropy = -1 * (_pdf * std::log( _pdf )).sum() / std::log( 2 );
}

template <typename T>
T attribute_information<T>::num_values() const {
	return _pdf.size();
}

template <typename T>
double attribute_information<T>::entropy() const {
	return _entropy;
}

template <typename T>
probability attribute_information<T>::marginal_probability( T index ) const {
//	std::cerr << "index: " << index << " - num_values(): " << num_values() << "\n";
	assert( index < num_values() );
	return _pdf[ index ];
}

#endif
