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

/**
 * @brief A dense, row-major two-dimensional matrix backed by std::valarray.
 *
 * Provides O(1) element access and an efficient slice-based transpose. The
 * delimiter character controls how rows and columns are separated when reading
 * from or writing to streams.
 *
 * @tparam T Element type stored in the matrix.
 */
template <typename T> class matrix {
  template <typename U> friend bool operator==(matrix<U> const &lhs, matrix<U> const &rhs);

public:
  /** @brief Alias for the element type. */
  using value_type = T;

  /** @brief Construct a 0x0 matrix. */
  matrix();

  /**
   * @brief Construct a value-initialized matrix of the given dimensions.
   *
   * @param num_rows    Number of rows.
   * @param num_columns Number of columns.
   */
  matrix(std::size_t num_rows, std::size_t num_columns);

  /**
   * @brief Construct a matrix filled with a constant value.
   *
   * @param num_rows    Number of rows.
   * @param num_columns Number of columns.
   * @param value       Value assigned to every element.
   */
  matrix(std::size_t num_rows, std::size_t num_columns, T const &value);

  /** @brief Return the number of rows. */
  std::size_t num_rows() const;

  /** @brief Return the number of columns. */
  std::size_t num_columns() const;

  /**
   * @brief Read-only element access.
   *
   * @param row    Row index (must be < num_rows()).
   * @param column Column index (must be < num_columns()).
   * @return Const reference to the element at (row, column).
   */
  T const &operator()(std::size_t row, std::size_t column) const;

  /**
   * @brief Read-write element access.
   *
   * @param row    Row index (must be < num_rows()).
   * @param column Column index (must be < num_columns()).
   * @return Reference to the element at (row, column).
   */
  T &operator()(std::size_t row, std::size_t column);

  /**
   * @brief Return a new matrix that is the transpose of this matrix.
   *
   * The returned matrix has num_columns() rows and num_rows() columns and
   * inherits the same delimiter setting.
   *
   * @return Transposed copy of this matrix.
   */
  matrix<T> transpose() const;

  /**
   * @brief Set the field delimiter used for stream I/O.
   *
   * @param delim Character that separates adjacent values in a row.
   */
  void set_delimiter(char delim);

  /** @brief Return the current field delimiter character. */
  char delimiter() const;

  /** @brief Enable missing value handling: non-numeric fields are stored as NaN. */
  void set_allow_missing(bool allow);

  /**
   * @brief Read matrix dimensions and data from an input stream.
   *
   * Determines the number of rows and columns from the stream content. Each
   * row must contain the same number of delimiter-separated values; rows are
   * terminated by newline characters.
   *
   * @param is Input stream to read from.
   * @throws std::runtime_error If any row has an inconsistent column count or
   *                            an unexpected character is encountered.
   */
  void read_from(std::istream &is);

  /**
   * @brief Write the matrix to an output stream.
   *
   * Outputs each row as delimiter-separated values followed by a newline.
   * Elements of type unsigned char are promoted to unsigned int for
   * human-readable numeric output.
   *
   * @param os Output stream to write to.
   */
  void write_to(std::ostream &os) const;

private:
  std::size_t _num_rows;
  std::size_t _num_columns;
  std::valarray<T> _data;
  bool _allow_missing = false;
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

template <typename T> void matrix<T>::set_allow_missing(bool allow) { _allow_missing = allow; }

template <typename T> char matrix<T>::delimiter() const { return _delimiter; }

/**
 * @brief Compare two matrices for equality.
 *
 * Two matrices are equal if they have the same dimensions and identical elements
 * at every position.
 *
 * @tparam U Element type.
 * @param lhs Left-hand matrix.
 * @param rhs Right-hand matrix.
 * @return true if lhs and rhs have equal dimensions and equal elements; false otherwise.
 */
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

  // Read values until EOF. When _allow_missing is true, recognized missing
  // value tokens (NA, NaN, ?, empty field) are replaced with NaN instead of
  // causing a parse error. Unrecognized tokens still throw.
  T d;
  auto try_read_value = [&](T &val) -> bool {
    if (is >> val) {
      return true;
    }
    if (is.eof()) {
      return false;
    }
    // Extraction failed — check if it's a recognized missing value token
    if (_allow_missing && std::is_floating_point<T>::value) {
      is.clear(); // reset failbit
      std::string token;
      // Read until delimiter or newline
      char ch;
      while (is.get(ch)) {
        if (ch == _delimiter || ch == '\n') {
          is.putback(ch);
          break;
        }
        token += ch;
      }
      if (token == "NA" || token == "NaN" || token == "nan" || token == "?" || token.empty()) {
        val = std::numeric_limits<T>::quiet_NaN();
        return true;
      }
      throw std::runtime_error("invalid value '" + token + "' at row " +
                               std::to_string(_num_rows + 1));
    }
    return false;
  };

  while (try_read_value(d)) {
    if (column_num == 0) {
      ++_num_rows;
    }
    if (_num_rows == 1) {
      _num_columns = column_num + 1;
    }
    buffer.push_back(d);

    // Read the separator character following the value
    auto c = static_cast<char>(is.get());
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

/**
 * @brief Write a matrix to an output stream.
 *
 * Delegates to matrix::write_to().
 *
 * @tparam T Element type.
 * @param os Output stream.
 * @param m  Matrix to write.
 * @return Reference to @p os.
 */
template <typename T> std::ostream &operator<<(std::ostream &os, matrix<T> const &m) {
  m.write_to(os);
  return os;
}

/**
 * @brief Read a matrix from an input stream.
 *
 * Delegates to matrix::read_from().
 *
 * @tparam T Element type.
 * @param is Input stream.
 * @param m  Matrix to populate.
 * @return Reference to @p is.
 */
template <typename T> std::istream &operator>>(std::istream &is, matrix<T> &m) {
  m.read_from(is);
  return is;
}

#endif
