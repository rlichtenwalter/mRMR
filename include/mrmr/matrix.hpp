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
#include <stdexcept>
#include <valarray>
#include <vector>

template <typename T> class matrix {
  template <typename U> friend bool operator==(matrix<U> const &lhs, matrix<U> const &rhs);

public:
  using value_type = T;
  matrix();
  matrix(std::size_t num_rows, std::size_t num_columns);
  matrix(std::size_t num_rows, std::size_t num_columns, T const &value);
  std::size_t num_rows() const;
  std::size_t num_columns() const;
  T const &operator()(std::size_t row, std::size_t column) const;
  T &operator()(std::size_t row, std::size_t column);
  matrix<T> transpose() const;

  void set_delimiter(char delim);
  char delimiter() const;

  void read_from(std::istream &is);
  void write_to(std::ostream &os) const;

private:
  std::size_t _num_rows;
  std::size_t _num_columns;
  std::valarray<T> _data;
  char _delimiter = '\t';
};

template <typename T> matrix<T>::matrix() : matrix(0, 0) {}

template <typename T>
matrix<T>::matrix(std::size_t num_rows, std::size_t num_columns)
    : _num_rows(num_rows), _num_columns(num_columns),
      _data(std::valarray<T>(num_rows * num_columns)) {}

template <typename T>
matrix<T>::matrix(std::size_t num_rows, std::size_t num_columns, T const &value)
    : _num_rows(num_rows), _num_columns(num_columns),
      _data(std::valarray<T>(value, num_rows * num_columns)) {}

template <typename T> std::size_t matrix<T>::num_rows() const { return _num_rows; }

template <typename T> std::size_t matrix<T>::num_columns() const { return _num_columns; }

template <typename T> T const &matrix<T>::operator()(std::size_t row, std::size_t column) const {
  assert(row < num_rows());
  assert(column < num_columns());
  return _data[row * num_columns() + column];
}

template <typename T> T &matrix<T>::operator()(std::size_t row, std::size_t column) {
  assert(row < num_rows());
  assert(column < num_columns());
  return _data[row * num_columns() + column];
}

template <typename T> matrix<T> matrix<T>::transpose() const {
  matrix<T> result(num_columns(), num_rows());
  result._delimiter = _delimiter;
  for (std::size_t row = 0; row < num_rows(); ++row) {
    result._data[std::slice(row, num_columns(), num_rows())] =
        _data[std::slice(row * num_columns(), num_columns(), 1)];
  }
  return result;
}

template <typename T> void matrix<T>::set_delimiter(char delim) { _delimiter = delim; }

template <typename T> char matrix<T>::delimiter() const { return _delimiter; }

template <typename T> bool operator==(matrix<T> const &lhs, matrix<T> const &rhs) {
  if (lhs.num_rows() == rhs.num_rows() && lhs.num_columns() == rhs.num_columns()) {
    for (std::size_t i = 0; i < lhs.num_rows() * lhs.num_columns(); ++i) {
      if (lhs._data[i] != rhs._data[i]) {
        return false;
      }
    }
    return true;
  } else {
    return false;
  }
}

template <typename T> void matrix<T>::write_to(std::ostream &os) const {
  std::string delim_str(1, _delimiter);
  for (std::size_t row = 0; row < num_rows(); ++row) {
    if (std::is_same<T, unsigned char>::value) {
      std::copy(&_data[row * num_columns()], &_data[(row + 1) * num_columns() - 1],
                std::ostream_iterator<unsigned int>(os, delim_str.c_str()));
      os << static_cast<unsigned int>(_data[(row + 1) * num_columns() - 1]);
    } else {
      std::copy(&_data[row * num_columns()], &_data[(row + 1) * num_columns() - 1],
                std::ostream_iterator<T>(os, delim_str.c_str()));
      os << _data[(row + 1) * num_columns() - 1];
    }
    os << '\n';
  }
}

template <typename T> void matrix<T>::read_from(std::istream &is) {
  // Parse delimited matrix from stream. Dimensions are determined from the data.
  // Uses vector for efficient dynamic growth, converts to valarray at end.
  std::vector<T> buffer;
  buffer.reserve(256);
  _num_rows = 0;
  _num_columns = 0;
  std::size_t column_num = 0;

  // Read values until EOF
  T d;
  while (is >> d) {
    if (column_num == 0) {
      ++_num_rows;
    }
    if (_num_rows == 1) {
      _num_columns = column_num + 1;
    }
    buffer.push_back(d);

    // Read the separator character following the value
    char c = is.get();
    if (c == _delimiter) {
      ++column_num;
    } else if (c == '\n') {
      if (column_num + 1 != _num_columns) {
        throw std::runtime_error("inconsistent number of columns at matrix row " +
                                 std::to_string(_num_rows));
      }
      column_num = 0;
    } else if (is.eof()) {
      // Value was the last in the stream with no trailing separator
      if (_num_columns > 0 && column_num + 1 != _num_columns) {
        throw std::runtime_error("inconsistent number of columns at matrix row " +
                                 std::to_string(_num_rows));
      }
      break;
    } else {
      throw std::runtime_error("unexpected character after value at row " +
                               std::to_string(_num_rows));
    }

    // Peek to detect EOF before the next extraction attempt
    is.peek();
  }

  // Convert buffer to valarray for slice-based operations (transpose)
  _data.resize(buffer.size());
  std::copy(buffer.begin(), buffer.end(), std::begin(_data));
}

// Convenience stream operators that delegate to member functions
template <typename T> std::ostream &operator<<(std::ostream &os, matrix<T> const &m) {
  m.write_to(os);
  return os;
}

template <typename T> std::istream &operator>>(std::istream &is, matrix<T> &m) {
  m.read_from(is);
  return is;
}

#endif
