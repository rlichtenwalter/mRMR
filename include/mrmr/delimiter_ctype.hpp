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

#ifndef MRMR_DELIMITER_CTYPE_HPP
#define MRMR_DELIMITER_CTYPE_HPP

#include <algorithm>
#include <locale>
#include <string>

class delimiter_ctype : public std::ctype<char> {
public:
  delimiter_ctype(char delimiter, std::size_t refs = 0);
  delimiter_ctype(std::string delimiters, std::size_t refs = 0);

private:
  static mask const *make_table(std::string delimiters);
};

inline std::ctype<char>::mask const *delimiter_ctype::make_table(std::string delimiters) {
  auto *table = new mask[table_size];
  std::copy(classic_table(), classic_table() + table_size, table);
  for (std::size_t i = 0; i < table_size; ++i) {
    table[i] &= ~space;
  }
  for (auto delimiter : delimiters) {
    table[static_cast<unsigned char>(delimiter)] |= space;
  }
  table['\n'] |= space;
  return table;
}

inline delimiter_ctype::delimiter_ctype(char delimiter, std::size_t refs)
    : ctype(make_table(std::string(1, delimiter)), true, refs) {}

inline delimiter_ctype::delimiter_ctype(std::string delimiters, std::size_t refs)
    : ctype(make_table(delimiters), true, refs) {}

#endif
