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

#include <cmath>
#include <cstdlib>
#include <forward_list>
#include <limits>
#include <list>
#include <stack>
#include <string>
#include <utility>
#include "dataset.hpp"

// return type for mRMR call is tuple containing:
// 1. Ranks
// 2. Attribute Indices (0-indexed)
// 3. Attribute Names
// 4. Attribute Entropies
// 5. Mutual Informations with Class Attribute
// 6. mRMR Scores
using mrmr_return_type = std::tuple<
		std::vector<std::size_t>,
		std::vector<std::size_t>,
		std::vector<std::string>,
		std::vector<double>,
		std::vector<double>,
		std::vector<double>
		>;

template <typename T> mrmr_return_type mrmr( dataset<T> const & data, std::size_t class_attribute_index ) {
	using dataset_type = dataset<T>;
	
	mrmr_return_type retval;
	std::get<0>( retval ).reserve( data.num_attributes() );
	std::get<1>( retval ).reserve( data.num_attributes() );
	std::get<2>( retval ).reserve( data.num_attributes() );
	std::get<3>( retval ).reserve( data.num_attributes() );
	std::get<4>( retval ).reserve( data.num_attributes() );
	std::get<5>( retval ).reserve( data.num_attributes() );
	
	// compute mRMR prerequisites
	std::vector<double> mutual_informations( data.num_attributes() );
	std::vector<double> redundance( data.num_attributes(), 0.0 );
	std::forward_list<std::size_t> unselected;
	std::vector<std::size_t> useless;
	for( std::size_t i = 0; i < data.num_attributes(); ++i ) {
		if( i != class_attribute_index ) {
			if( data.attribute_entropy( i ) > 0 ) {
				mutual_informations[ i ] = data.mutual_information( class_attribute_index, i );
				unselected.push_front( i );
			} else {
				mutual_informations[ i ] = 0;
				useless.push_back( i );
			}
		}
	}
	unselected.reverse();
	mutual_informations[ class_attribute_index ] = -std::numeric_limits<double>::infinity();
	
	// output class information
	double class_entropy = data.attribute_entropy( class_attribute_index );

	std::get<0>( retval ).push_back( 0 );
	std::get<1>( retval ).push_back( class_attribute_index );
	std::get<2>( retval ).push_back( data.attribute_name( class_attribute_index ) );
	std::get<3>( retval ).push_back( class_entropy );
	std::get<4>( retval ).push_back( class_entropy );
	std::get<5>( retval ).push_back( std::numeric_limits<double>::quiet_NaN() );
	
	// handle special case of first attribute with highest mutual information
	std::size_t best_attribute_index = std::max_element( mutual_informations.begin(), mutual_informations.end() ) - mutual_informations.begin();
	std::size_t last_attribute_index = best_attribute_index;
	unselected.remove( best_attribute_index );
	double mrmr_score = mutual_informations.at( best_attribute_index );

	std::get<0>( retval ).push_back( 1 );
	std::get<1>( retval ).push_back( best_attribute_index );
	std::get<2>( retval ).push_back( data.attribute_name( best_attribute_index ) );
	std::get<3>( retval ).push_back( data.attribute_entropy( best_attribute_index ) );
	std::get<4>( retval ).push_back( mrmr_score );
	std::get<5>( retval ).push_back( mrmr_score );
	
	// main mRMR computation loop
	std::size_t rank = 2;
	while( !unselected.empty() ) {
		double best_mrmr_score = -std::numeric_limits<double>::infinity();
		auto it = std::cbegin( unselected );
		auto last_it = unselected.before_begin();
		auto erase_it = last_it;
		while( it != std::cend( unselected ) ) {
			std::size_t attribute_index = *it;
			redundance.at( attribute_index ) += data.mutual_information( last_attribute_index, attribute_index );
			mrmr_score = mutual_informations.at( attribute_index ) - redundance.at( attribute_index ) / (rank - 1);
			if( mrmr_score - best_mrmr_score > std::numeric_limits<double>::epsilon() ) {
				best_mrmr_score = mrmr_score;
				best_attribute_index = attribute_index;
				erase_it = last_it;
			}
			++it;
			++last_it;
		}

		std::get<0>( retval ).push_back( rank++ );
		std::get<1>( retval ).push_back( best_attribute_index );
		std::get<2>( retval ).push_back( data.attribute_name( best_attribute_index ) );
		std::get<3>( retval ).push_back( data.attribute_entropy( best_attribute_index ) );
		std::get<4>( retval ).push_back( mutual_informations.at( best_attribute_index ) );
		std::get<5>( retval ).push_back( best_mrmr_score );
		
		unselected.erase_after( erase_it );
		last_attribute_index = best_attribute_index;
	}
	
	// finish by outputting useless features
	std::sort( useless.begin(), useless.end() );
	for( auto attribute_index : useless ) {
		std::get<0>( retval ).push_back( rank++ );
		std::get<1>( retval ).push_back( attribute_index );
		std::get<2>( retval ).push_back( data.attribute_name( attribute_index ) );
		std::get<3>( retval ).push_back( 0 );
		std::get<4>( retval ).push_back( 0 );
		std::get<5>( retval ).push_back( -std::numeric_limits<double>::infinity() );
	}

	return retval;
}

