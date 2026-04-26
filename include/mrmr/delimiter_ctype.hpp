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

/**
 * @brief A std::ctype<char> facet that treats specified characters as whitespace delimiters.
 *
 * Replaces the classic locale's whitespace classification so that only the
 * provided delimiter characters (and newline) are treated as field separators
 * during stream extraction. This enables std::istream to tokenize delimited
 * files (e.g., CSV or TSV) using the standard >> operator.
 */
class delimiter_ctype : public std::ctype<char> {
public:
  /**
   * @brief Construct a facet with a single delimiter character.
   *
   * Newline is always treated as a separator in addition to @p delimiter.
   *
   * @param delimiter The character to treat as a field separator.
   * @param refs      Reference count passed to std::locale::facet base.
   */
  delimiter_ctype(char delimiter, std::size_t refs = 0);

  /**
   * @brief Construct a facet with a string of delimiter characters.
   *
   * Every character in @p delimiters is treated as a field separator.
   * Newline is always treated as a separator as well.
   *
   * @param delimiters String of characters each of which acts as a separator.
   * @param refs       Reference count passed to std::locale::facet base.
   */
  delimiter_ctype(std::string const &delimiters, std::size_t refs = 0);

private:
  /**
   * @brief Build a ctype classification table that marks the given characters as whitespace.
   *
   * Starts from the classic table, strips the space classification from all
   * characters, then re-applies it to each character in @p delimiters and to
   * newline. Ownership of the returned heap-allocated array is transferred to
   * the std::ctype<char> base, which deletes it when the facet is destroyed.
   *
   * @param delimiters String of characters to classify as whitespace.
   * @return Pointer to a newly allocated classification table of size table_size.
   */
  static mask const *make_table(std::string const &delimiters);
};

inline std::ctype<char>::mask const *delimiter_ctype::make_table(std::string const &delimiters) {
  auto *table = new mask[table_size];
  std::copy(classic_table(), classic_table() + table_size, table);
  for (std::size_t i = 0; i < table_size; ++i) {
    // `~space` promotes to int; cast back to mask narrows but is
    // value-preserving for the bit-clear operation.
    table[i] &= static_cast<mask>(~space);
  }
  for (auto delimiter : delimiters) {
    table[static_cast<unsigned char>(delimiter)] |= space;
  }
  table['\n'] |= space;
  return table;
}

inline delimiter_ctype::delimiter_ctype(char delimiter, std::size_t refs)
    : ctype(make_table(std::string(1, delimiter)), true, refs) {}

inline delimiter_ctype::delimiter_ctype(std::string const &delimiters, std::size_t refs)
    : ctype(make_table(delimiters), true, refs) {}

#endif
