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

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <getopt.h>
#include <forward_list>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <stack>
#include <string>
#include <utility>
#include "dataset.hpp"

std::string VERSION_STRING = "0.91 (beta)";

enum verbosity_level : char {
	QUIET = 0,
	WARNING = 1,
	INFO = 2,
	DEBUG = 3
};

enum message_type : char {
	STANDARD = 0,
	START = 1,
	FINISH = 2
};

char DELIMITER = '\t';
verbosity_level VERBOSITY = WARNING;

void short_usage( char const * program ) {
	std::cerr << "Usage: " << program << " [OPTION]... [FILE]                                     \n";
	std::cerr << "Try '" << program << " --help' for more information.                            \n";
}

void usage( char const * program ) {
	std::cerr << "Usage: " << program << " [OPTION]... [FILE]                                     \n";
	std::cerr << "Compute mRMR values for attributes in data set, either taking input from        \n";
	std::cerr << "standard input or from a file. Named pipes and process substitution may also be \n";
	std::cerr << "used as the file argument.                                                      \n";
	std::cerr << "                                                                                \n";
	std::cerr << "  -t, --delimiter=CHAR      use CHAR for field separator                        \n";
	std::cerr << "                            defaults to TAB if not provided                     \n";
	std::cerr << "  -c, --class=NUM           1-indexed class attribute selection;                \n";
	std::cerr << "                            defaults to 1 if not provided                       \n";
	std::cerr << "  -d, --discretize=VALUE    one of {round,floor,ceiling,truncate};              \n";
	std::cerr << "                            defaults to truncate if not provided                \n";
	std::cerr << "  -v, --verbosity=VALUE     one of {0,1,2,3,quiet,warning,info,debug};          \n";
	std::cerr << "                            defaults to 1=warning if not provided               \n";
	std::cerr << "  -w, --write-data          read, transform, and write data set to stdout       \n";
	std::cerr << "                            output respects -t option if specified              \n";
	std::cerr << "  -h, --help                display this help and exit                          \n";
	std::cerr << "  -V, --version             output version information and exist                \n";
}

void log_message( char const * message, verbosity_level verbosity, message_type mtype ) {
	using time_type = std::chrono::time_point<std::chrono::high_resolution_clock>;
	static std::stack<time_type,std::list<time_type>> time_stack;
	if( VERBOSITY >= verbosity ) {
		if( mtype == STANDARD && time_stack.size() > 0 ) {
			std::cerr << '\n';
		}
		if( mtype == STANDARD || mtype == START ) {
			std::time_t time = std::time( nullptr );
			std::cerr << std::string( time_stack.size(), '\t' ) << std::put_time( std::localtime( &time ), "%Y-%m-%d %H:%M:%S" ) << " - " << message;
		}
		if( mtype == STANDARD ) {
			std::cerr << '\n';
		} else if( mtype == START ) {
			time_stack.emplace( std::chrono::high_resolution_clock::now() );
		} else if( mtype == FINISH ) {
			auto start_time = time_stack.top();
			std::chrono::duration<double> time_span = std::chrono::duration_cast< std::chrono::duration<double> >( std::chrono::high_resolution_clock::now() - start_time );
			time_stack.pop();
			std::cerr << std::string( time_stack.size(), '\t' ) << "DONE (" << time_span.count() << " seconds)\n";
		}
	}
}

int main( int argc, char* argv[] ) {
	// disable I/O sychronization for better I/O performance
//	std::ios_base::sync_with_stdio( false );
	std::cout << std::scientific;
	std::cerr << std::scientific;

	using storage_type = unsigned char;
	using dataset_type = dataset<storage_type>;
	std::ifstream ifs;
	std::size_t class_attribute = 0;
	dataset_type::discretization_method discretize = dataset_type::TRUNCATE;
	bool discretization_chosen = false;
	bool just_write = false;

	int c;
	int option_index = 0;
	while( true ) {
		static struct option long_options[] = {
				{ "delimiter", required_argument, 0, 't' },
				{ "class", required_argument, 0, 'c' },
				{ "discretize", required_argument, 0, 'd' },
				{ "verbosity", required_argument, 0, 'v' },
				{ "write", no_argument, 0, 'w' },
				{ "help", no_argument, 0, 'h' },
				{ "version", no_argument, 0, 'V' }
				};
		c = getopt_long( argc, argv, "t:c:d:v:whV", long_options, &option_index );
		if( c == -1 ) {
			break;
		}
		switch( c ) {
			case 't':
				if( strcmp( optarg, "\\t" ) == 0 ) {
					DELIMITER = '\t';
				} else if( strlen( optarg ) != 1 ) {
					std::cerr << argv[0] << ":  -t, --delimiter=CHAR  must be a single character\n";
					return 1;
				} else {
					DELIMITER = optarg[0];
				}
				break;
			case 'c':
				class_attribute = std::strtoul( optarg, nullptr, 10 );
				if( class_attribute == 0 || errno == ERANGE ) {
					std::cerr << argv[0] << ":  -c, --class=NUM  class attribute out of range\n";
					return 1;
				}
				--class_attribute;
				break;
			case 'd':
				if( strcmp( optarg, "round" ) == 0 ) {
					discretize = dataset_type::ROUND;
				} else if( strcmp( optarg, "floor" ) == 0 ) {
					discretize = dataset_type::FLOOR;
				} else if( strcmp( optarg, "ceiling" ) == 0 ) {
					discretize = dataset_type::CEILING;
				} else if( strcmp( optarg, "truncate" ) == 0 ) {
					discretize = dataset_type::TRUNCATE;
				} else {
					std::cerr << argv[0] << ": -d --discretize=VALUE  must be one of one of {round,floor,ceiling,truncate}\n";
					return 1;
				}
				discretization_chosen = true;
				break;
			case 'v':
				if( strcmp( optarg, "0" ) == 0 || strcmp( optarg, "quiet" ) == 0 ) {
					VERBOSITY = QUIET;
				} else if( strcmp( optarg, "1" ) == 0 || strcmp( optarg, "warning" ) == 0 ) {
					VERBOSITY = WARNING;
				} else if( strcmp( optarg, "1" ) == 0 || strcmp( optarg, "info" ) == 0 ) {
					VERBOSITY = INFO;
				} else if( strcmp( optarg, "2" ) == 0 || strcmp( optarg, "debug" ) == 0 ) {
					VERBOSITY = DEBUG;
				} else {
					std::cerr << argv[0] << ": " << "  -v, --verbosity=[VALUE]  one of {0,1,2,3,quiet,warning,info,debug}; defaults to 1=warning\n";
					short_usage( argv[0] );
					return 1;
				}
				break;
			case 'w':
				just_write = true;
				break;
			case 'h':
				usage( argv[0] );
				return 0;
			case 'V':
				std::cout << "Improved mRMR by Ryan N. Lichtenwalter v" << VERSION_STRING << "\n";
				return 0;
			default:
				short_usage( argv[0] );
				return 1;
		}
	}
	if( optind < argc ) {
		if( optind == argc - 1 ) {
			ifs = std::ifstream( argv[optind] );
			log_message( (std::string( "FILE = " ) + std::string( argv[optind] )).c_str(), DEBUG, STANDARD );
		} else {
			std::cerr << argv[0] << ": " << "too many arguments\n";
			short_usage( argv[0] );
			return 1;
		}
	}

	// read data
	log_message( "Reading and transforming dataset and computing attribute information...", INFO, START ); 
	using storage_type = unsigned char;
	dataset_type data;
	if( ifs.is_open() ) {
		log_message( "Reading from file...", DEBUG, STANDARD );
		data = dataset_type( ifs, discretize );
	} else {
		log_message( "Reading from standard input...", DEBUG, STANDARD );
		data = dataset_type( std::cin, discretize );
	}
	log_message( "DONE", INFO, FINISH );

	if( !discretization_chosen ) {
		log_message( "No discretization method chosen. Default 'truncate' used...", WARNING, STANDARD );
	}

	if( just_write ) {
		log_message( "Writing dataset out standard output...", INFO, START );
		std::cout << data;
		log_message( "DONE", INFO, FINISH );
		return 0;
	}

	// compute mRMR prerequisites
	log_message( "Calculating mutual information between each attribute and class...", INFO, START );
	std::vector<double> mutual_informations( data.num_attributes() );
	std::vector<double> redundance( data.num_attributes(), 0.0 );
	std::forward_list<std::size_t> unselected;
	std::vector<std::size_t> useless;
	for( std::size_t i = 0; i < data.num_attributes(); ++i ) {
		if( i != class_attribute ) {
			if( data.attribute_entropy( i ) > 0 ) {
				mutual_informations[ i ] = data.mutual_information( class_attribute, i );
				unselected.push_front( i );
			} else {
				mutual_informations[ i ] = 0;
				useless.push_back( i );
			}
		}
	}
	unselected.reverse();
	mutual_informations[ class_attribute ] = -std::numeric_limits<double>::infinity();
	log_message( "DONE", INFO, FINISH );

	log_message( "Performing main mRMR computations...", INFO, START );
	// output header
	std::cout << "Rank\tIndex\tName\tEntropy\tMutual Information\tmRMR Score\n";

	// output class information
	double class_entropy = data.attribute_entropy( class_attribute );
	std::cout << "0\t" << class_attribute << '\t' << data.attribute_name( class_attribute ) << '\t' << class_entropy << '\t' << class_entropy << '\t' << std::numeric_limits<double>::quiet_NaN() << std::endl;

	// handle special case of first attribute with highest mutual information
	std::size_t best_attribute_index = std::max_element( mutual_informations.begin(), mutual_informations.end() ) - mutual_informations.begin();
	std::size_t last_attribute_index = best_attribute_index;
	unselected.remove( best_attribute_index );
	double mrmr_score = mutual_informations.at( best_attribute_index );
	std::cout << "1\t" << best_attribute_index << '\t' << data.attribute_name( best_attribute_index ) << '\t' << data.attribute_entropy( best_attribute_index ) << '\t' << mrmr_score << '\t' << mrmr_score << std::endl;

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
			if( VERBOSITY >= DEBUG ) {
				std::cerr << "\t\t" << attribute_index << '\t' << data.attribute_name( attribute_index ) << '\t' << data.attribute_entropy( attribute_index ) << '\t' << mrmr_score << std::endl;
			}
			if( mrmr_score - best_mrmr_score > std::numeric_limits<double>::epsilon() ) {
				best_mrmr_score = mrmr_score;
				best_attribute_index = attribute_index;
				erase_it = last_it;
			}
			++it;
			++last_it;
		}
		std::cout << rank++ << '\t' << best_attribute_index << '\t' << data.attribute_name( best_attribute_index ) << '\t' << data.attribute_entropy( best_attribute_index ) << '\t' << mutual_informations.at( best_attribute_index ) << '\t' << best_mrmr_score << std::endl;
		unselected.erase_after( erase_it );
		last_attribute_index = best_attribute_index;
	}

	// finish by outputting useless features
	std::sort( useless.begin(), useless.end() );
	for( auto attribute_index : useless ) {
		std::cout << rank++ << '\t' << attribute_index << '\t' << data.attribute_name( attribute_index ) << "\t0\t0\t" << -std::numeric_limits<double>::infinity() << std::endl;
	}

	log_message( "DONE", INFO, FINISH );

	return 0;
}
