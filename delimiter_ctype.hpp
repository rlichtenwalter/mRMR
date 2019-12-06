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

#ifndef DELIMITER_CTYPE_HPP
#define DELIMITER_CTYPE_HPP

#include <locale>
#include <string>
#include <vector>

class delimiter_ctype : public std::ctype<char> {
	public: 
		delimiter_ctype( char delimiter, std::size_t refs = 0 );
		delimiter_ctype( std::string delimiters, std::size_t refs = 0 );
	private: 
		static mask const * make_table( std::string delimiters );
};

std::ctype<char>::mask const * delimiter_ctype::make_table( std::string delimiters ) {
	static std::vector<mask> stream_table( classic_table(), classic_table() + table_size );
	for( auto m : stream_table ) {
		m &= ~space;
	}
	for( auto delimiter : delimiters ) {
		stream_table[ delimiter ] |= space;
	}
	return &stream_table[0];
}

delimiter_ctype::delimiter_ctype( char delimiter, std::size_t refs ) : ctype( make_table( std::string( 1, delimiter ) ), false, refs ) {
}

delimiter_ctype::delimiter_ctype( std::string delimiters, std::size_t refs ) : ctype( make_table( delimiters ), false, refs ) {
}

#endif

