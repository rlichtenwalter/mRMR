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

#ifndef MRMR_MATRIX_HPP
#define MRMR_MATRIX_HPP

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <valarray>

template <typename T>
class matrix {
	template <typename U> friend bool operator==( matrix<U> const & lhs, matrix<U> const & rhs );
	template <typename U> friend std::ostream & operator<<( std::ostream & os, matrix<U> const & m );
	template <typename U> friend std::istream & operator>>( std::istream & is, matrix<U> & m );
	public:
		using value_type = T;
		matrix();
		matrix( std::size_t num_rows, std::size_t num_columns );
		matrix( std::size_t num_rows, std::size_t num_columns, T const & value );
		std::size_t num_rows() const;
		std::size_t num_columns() const;
		T const & operator()( std::size_t row, std::size_t column ) const;
		T & operator()( std::size_t row, std::size_t column );
		matrix<T> transpose() const;
	private:
		std::size_t _num_rows;
		std::size_t _num_columns;
		std::valarray<T> _data;
};

template <typename T>
matrix<T>::matrix() : matrix( 0, 0 ) {
}

template <typename T>
matrix<T>::matrix( std::size_t num_rows, std::size_t num_columns ) : _num_rows(num_rows), _num_columns(num_columns), _data( std::valarray<T>( num_rows * num_columns ) ) {
}

template <typename T>
matrix<T>::matrix( std::size_t num_rows, std::size_t num_columns, T const & value ) : _num_rows(num_rows), _num_columns(num_columns), _data( std::valarray<T>( num_rows * num_columns ) ) {
}

template <typename T>
std::size_t matrix<T>::num_rows() const {
	return _num_rows;
}

template <typename T>
std::size_t matrix<T>::num_columns() const {
	return _num_columns;
}

template <typename T>
T const & matrix<T>::operator()( std::size_t row, std::size_t column ) const {
	assert( row < num_rows() );
	assert( column < num_columns() );
	return _data[ row * num_columns() + column ];
}

template <typename T>
T & matrix<T>::operator()( std::size_t row, std::size_t column ) {
	assert( row < num_rows() );
	assert( column < num_columns() );
	return _data[ row * num_columns() + column ];
}

template <typename T>
matrix<T> matrix<T>::transpose() const {
	matrix<T> result( num_columns(), num_rows() );
	for( std::size_t row = 0; row < num_rows(); ++row ) {
		result._data[ std::slice( row, num_columns(), num_rows() ) ] = _data[ std::slice( row * num_columns(), num_columns(), 1 ) ];
	}
	return result;
}

template <typename T>
bool operator==( matrix<T> const & lhs, matrix<T> const & rhs ) {
	if( lhs.num_rows() == rhs.num_rows() && lhs.num_columns() == rhs.num_columns() ) {
		for( std::size_t i = 0; i < lhs.num_rows() * lhs.num_columns(); ++i ) {
			if( lhs._data[ i ] != rhs._data[ i ] ) {
				return false;
			}
		}
		return true;
	} else {
		return false;
	}
}

template <typename T>
std::ostream & operator<<( std::ostream & os, matrix<T> const & m ) {
	for( std::size_t row = 0; row < m.num_rows(); ++row ) {
		if( std::is_same<T,unsigned char>::value ) {
			std::copy( &m._data[ row * m.num_columns() ], &m._data[ (row + 1) * m.num_columns() - 1 ], std::ostream_iterator<unsigned int>( os, "\t" ) );
			os << static_cast<unsigned int>( m._data[ (row + 1) * m.num_columns() - 1 ] );
		} else {
			std::copy( &m._data[ row * m.num_columns() ], &m._data[ (row + 1) * m.num_columns() - 1 ], std::ostream_iterator<T>( os, "\t" ) );
			os << m._data[ (row + 1) * m.num_columns() - 1 ];
		}
		os << '\n';
	}
	return os;
}

template <typename T>
std::istream & operator>>( std::istream & is, matrix<T> & m ) {
	m._num_rows = 0;
	m._num_columns = 0;
	if( m._data.size() == 0 ) {
		m._data.resize( 256 );
	}
	std::size_t column_num = 0;
	while( !is.eof() ) {
		T d;
		is >> d;
		if( column_num == 0 ) {
			++m._num_rows;
		}
		if( m.num_rows() == 1 ) {
			m._num_columns = column_num + 1;
		}
		if( (m.num_rows() - 1) * m.num_columns() + column_num >= m._data.size() ) {
			std::valarray<T> temp;
			if( m.num_rows() == 1 ) {
				temp.resize( 2 * m._data.size() );
			} else {
				temp.resize( 2 * m.num_rows() * m.num_columns() );
			}
			std::copy( std::cbegin( m._data ), std::cend( m._data ), std::begin( temp ) );
			m._data = std::move( temp );
		}
		m( m.num_rows() - 1, column_num ) = d;
		char c = is.get();
		if( c == '\t' ) {
			++column_num;
		} else if( c == '\n' ) {
			if( column_num + 1 == m.num_columns() ) {
				column_num = 0;
			} else {
				std::cerr << "error: inconsistent number of columns at matrix row " << m.num_rows() << "\n";
				exit( 2 );
			}
		} else {
			std::cerr << "error: invalid value '" << d << c << "' at line " << m.num_rows() << "\n";
			exit( 2 );
		}
		is.peek();
	}
	if( m.num_rows() * m.num_columns() < m._data.size() ) {
		std::valarray<T> temp;
		temp.resize( m.num_rows() * m.num_columns() );
		std::copy( std::cbegin( m._data ), std::cbegin( m._data ) + m.num_rows() * m.num_columns(), std::begin( temp ) );
		m._data = std::move( temp );
	}
	return is;
}

#endif

