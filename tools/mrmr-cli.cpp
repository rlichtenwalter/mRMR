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
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <mrmr/mrmr.hpp>
#include <mrmr/mrmre.hpp>
#include <stack>
#include <string>

#ifndef MRMR_VERSION
#define MRMR_VERSION "unknown"
#endif

enum verbosity_level : char { QUIET = 0, WARNING = 1, INFO = 2, DEBUG = 3 };

enum message_type : char { STANDARD = 0, START = 1, FINISH = 2 };

verbosity_level VERBOSITY = WARNING;

void short_usage(char const *program) {
  std::cerr << "Usage: " << program << " [OPTION]... [FILE]                                     \n";
  std::cerr << "Try '" << program << " --help' for more information.                            \n";
}

void usage(char const *program) {
  std::cerr << "Usage: " << program << " [OPTION]... [FILE]                                     \n";
  std::cerr << "Compute mRMR feature ranking for attributes in a data set, either from          \n";
  std::cerr << "standard input or from a file. Supports both standard mRMR and mRMRe            \n";
  std::cerr << "ensemble feature selection.                                                     \n";
  std::cerr << "                                                                                \n";
  std::cerr << "Options:                                                                        \n";
  std::cerr << "  -t, --delimiter=CHAR      use CHAR for field separator (default: TAB)         \n";
  std::cerr << "  -c, --class=NUM           1-indexed class attribute (default: 1)              \n";
  std::cerr << "  -d, --discretize=VALUE    {round,floor,ceiling,truncate} (default: truncate)  \n";
  std::cerr << "  -v, --verbosity=VALUE     {0,1,2,3,quiet,warning,info,debug} (default: 1)    \n";
  std::cerr << "  -w, --write-data          output parsed/discretized data and exit             \n";
  std::cerr << "  -h, --help                display this help and exit                          \n";
  std::cerr << "  -V, --version             output version information and exit                 \n";
  std::cerr << "                                                                                \n";
  std::cerr << "Ensemble options (mRMRe):                                                       \n";
  std::cerr << "  -e, --ensemble=METHOD     {exhaustive,bootstrap} — enable ensemble mode       \n";
  std::cerr << "  -n, --solutions=NUM       number of ensemble solutions (default: 10)          \n";
  std::cerr << "  -k, --features=NUM        features to select per solution (default: all)      \n";
  std::cerr << "  -s, --seed=NUM            random seed for bootstrap (default: 42)             \n";
}

void log_message(char const *message, verbosity_level verbosity, message_type mtype) {
  using time_type = std::chrono::time_point<std::chrono::high_resolution_clock>;
  static std::stack<time_type, std::list<time_type>> time_stack;
  if (VERBOSITY >= verbosity) {
    if (mtype == STANDARD && time_stack.size() > 0) {
      std::cerr << '\n';
    }
    if (mtype == STANDARD || mtype == START) {
      std::time_t time = std::time(nullptr);
      std::cerr << std::string(time_stack.size(), '\t')
                << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << " - " << message;
    }
    if (mtype == STANDARD) {
      std::cerr << '\n';
    } else if (mtype == START) {
      time_stack.emplace(std::chrono::high_resolution_clock::now());
    } else if (mtype == FINISH) {
      auto start_time = time_stack.top();
      std::chrono::duration<double> time_span =
          std::chrono::duration_cast<std::chrono::duration<double>>(
              std::chrono::high_resolution_clock::now() - start_time);
      time_stack.pop();
      std::cerr << std::string(time_stack.size(), '\t') << "DONE (" << time_span.count()
                << " seconds)\n";
    }
  }
}

int main(int argc, char *argv[]) {
  std::cout << std::scientific;
  std::cerr << std::scientific;

  using storage_type = unsigned char;
  using dataset_type = dataset<storage_type>;
  std::ifstream ifs;
  std::size_t class_attribute = 0;
  dataset_type::discretization_method discretize = dataset_type::TRUNCATE;
  bool discretization_chosen = false;
  bool just_write = false;
  char delimiter = '\t';

  // Ensemble options
  bool ensemble_mode = false;
  mrmre_method ensemble_method = mrmre_method::EXHAUSTIVE;
  std::size_t solution_count = 10;
  std::size_t feature_count = 0; // 0 = all non-class attributes
  unsigned ensemble_seed = 42;

  int c;
  int option_index = 0;
  while (true) {
    static struct option long_options[] = {{"delimiter", required_argument, 0, 't'},
                                           {"class", required_argument, 0, 'c'},
                                           {"discretize", required_argument, 0, 'd'},
                                           {"verbosity", required_argument, 0, 'v'},
                                           {"write", no_argument, 0, 'w'},
                                           {"ensemble", required_argument, 0, 'e'},
                                           {"solutions", required_argument, 0, 'n'},
                                           {"features", required_argument, 0, 'k'},
                                           {"seed", required_argument, 0, 's'},
                                           {"help", no_argument, 0, 'h'},
                                           {"version", no_argument, 0, 'V'},
                                           {nullptr, 0, nullptr, 0}};
    c = getopt_long(argc, argv, "t:c:d:v:we:n:k:s:hV", long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
    case 't':
      if (strcmp(optarg, "\\t") == 0) {
        delimiter = '\t';
      } else if (strlen(optarg) != 1) {
        std::cerr << argv[0] << ":  -t, --delimiter=CHAR  must be a single character\n";
        return 1;
      } else {
        delimiter = optarg[0];
      }
      break;
    case 'c': {
      char *endptr = nullptr;
      errno = 0;
      class_attribute = std::strtoul(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || class_attribute == 0 || errno == ERANGE) {
        std::cerr << argv[0] << ":  -c, --class=NUM  must be a positive integer\n";
        return 1;
      }
      --class_attribute;
    } break;
    case 'd':
      if (strcmp(optarg, "round") == 0) {
        discretize = dataset_type::ROUND;
      } else if (strcmp(optarg, "floor") == 0) {
        discretize = dataset_type::FLOOR;
      } else if (strcmp(optarg, "ceiling") == 0) {
        discretize = dataset_type::CEILING;
      } else if (strcmp(optarg, "truncate") == 0) {
        discretize = dataset_type::TRUNCATE;
      } else {
        std::cerr << argv[0]
                  << ": -d --discretize  must be one of {round,floor,ceiling,truncate}\n";
        return 1;
      }
      discretization_chosen = true;
      break;
    case 'v':
      if (strcmp(optarg, "0") == 0 || strcmp(optarg, "quiet") == 0) {
        VERBOSITY = QUIET;
      } else if (strcmp(optarg, "1") == 0 || strcmp(optarg, "warning") == 0) {
        VERBOSITY = WARNING;
      } else if (strcmp(optarg, "2") == 0 || strcmp(optarg, "info") == 0) {
        VERBOSITY = INFO;
      } else if (strcmp(optarg, "3") == 0 || strcmp(optarg, "debug") == 0) {
        VERBOSITY = DEBUG;
      } else {
        std::cerr << argv[0] << ": -v --verbosity  must be one of "
                  << "{0,1,2,3,quiet,warning,info,debug}\n";
        short_usage(argv[0]);
        return 1;
      }
      break;
    case 'w':
      just_write = true;
      break;
    case 'e':
      ensemble_mode = true;
      if (strcmp(optarg, "exhaustive") == 0) {
        ensemble_method = mrmre_method::EXHAUSTIVE;
      } else if (strcmp(optarg, "bootstrap") == 0) {
        ensemble_method = mrmre_method::BOOTSTRAP;
      } else {
        std::cerr << argv[0] << ": -e --ensemble  must be one of {exhaustive,bootstrap}\n";
        return 1;
      }
      break;
    case 'n': {
      char *endptr = nullptr;
      errno = 0;
      solution_count = std::strtoul(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || solution_count == 0 || errno == ERANGE) {
        std::cerr << argv[0] << ": -n --solutions  must be a positive integer\n";
        return 1;
      }
    } break;
    case 'k': {
      char *endptr = nullptr;
      errno = 0;
      feature_count = std::strtoul(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || feature_count == 0 || errno == ERANGE) {
        std::cerr << argv[0] << ": -k --features  must be a positive integer\n";
        return 1;
      }
    } break;
    case 's': {
      char *endptr = nullptr;
      errno = 0;
      unsigned long seed_val = std::strtoul(optarg, &endptr, 10);
      if (endptr == optarg || *endptr != '\0' || errno == ERANGE) {
        std::cerr << argv[0] << ": -s --seed  must be a non-negative integer\n";
        return 1;
      }
      ensemble_seed = static_cast<unsigned>(seed_val);
    } break;
    case 'h':
      usage(argv[0]);
      return 0;
    case 'V':
      std::cout << "mrmr " << MRMR_VERSION << "\n";
      return 0;
    default:
      short_usage(argv[0]);
      return 1;
    }
  }
  if (optind < argc) {
    if (optind == argc - 1) {
      ifs = std::ifstream(argv[optind]);
      if (!ifs.is_open()) {
        std::cerr << argv[0] << ": " << argv[optind] << ": No such file or directory\n";
        return 1;
      }
      log_message((std::string("FILE = ") + std::string(argv[optind])).c_str(), DEBUG, STANDARD);
    } else {
      std::cerr << argv[0] << ": " << "too many arguments\n";
      short_usage(argv[0]);
      return 1;
    }
  }

  // Read data
  log_message("Reading and transforming dataset and computing attribute information...", INFO,
              START);
  dataset_type data;
  try {
    if (ifs.is_open()) {
      log_message("Reading from file...", DEBUG, STANDARD);
      data = dataset_type(ifs, discretize, delimiter);
    } else {
      log_message("Reading from standard input...", DEBUG, STANDARD);
      data = dataset_type(std::cin, discretize, delimiter);
    }
  } catch (std::exception const &e) {
    std::cerr << argv[0] << ": " << e.what() << "\n";
    return 2;
  }
  log_message("DONE", INFO, FINISH);

  if (!discretization_chosen) {
    log_message("No discretization method chosen. Default 'truncate' used...", WARNING, STANDARD);
  }

  if (just_write) {
    log_message("Writing dataset to standard output...", INFO, START);
    std::cout << data;
    log_message("DONE", INFO, FINISH);
    return 0;
  }

  if (ensemble_mode) {
    // --- mRMRe ensemble mode ---
    if (feature_count == 0) {
      feature_count = data.num_attributes() - 1; // all non-class
    }

    log_message(
        ("Computing mRMRe ensemble (" +
         std::string(ensemble_method == mrmre_method::EXHAUSTIVE ? "exhaustive" : "bootstrap") +
         ", " + std::to_string(solution_count) + " solutions, " + std::to_string(feature_count) +
         " features)...")
            .c_str(),
        INFO, START);

    auto result =
        mrmre(data, class_attribute, feature_count, solution_count, ensemble_method, ensemble_seed);

    log_message("DONE", INFO, FINISH);

    // Output consensus ranking
    std::cout << "# Consensus Ranking\n";
    std::cout << "Rank\tIndex\tName\tFrequency\n";
    std::size_t rank = 1;
    for (auto attr_index : result.consensus_ranking) {
      if (attr_index == class_attribute) {
        continue;
      }
      std::size_t freq = result.feature_frequencies[attr_index];
      if (freq == 0 && rank > feature_count) {
        break; // stop after features that appeared in at least one solution
      }
      std::cout << rank++ << '\t' << attr_index << '\t' << data.attribute_name(attr_index) << '\t'
                << freq << '\n';
    }

    // Output individual solutions if verbose
    if (VERBOSITY >= INFO) {
      std::cerr << "\n# Individual Solutions\n";
      for (std::size_t s = 0; s < result.solutions.size(); ++s) {
        std::cerr << "Solution " << s + 1 << ":";
        for (std::size_t i = 0; i < result.solutions[s].selected_indices.size(); ++i) {
          std::size_t idx = result.solutions[s].selected_indices[i];
          std::cerr << " " << data.attribute_name(idx);
        }
        std::cerr << "\n";
      }
    }

  } else {
    // --- Standard mRMR mode ---
    log_message("Computing mRMR feature ranking...", INFO, START);
    std::cout << "Rank\tIndex\tName\tEntropy\tMutual Information\tmRMR Score\n";

    mrmr(data, class_attribute,
         [](std::size_t rank, std::size_t index, std::string const &name, double entropy, double mi,
            double score) {
           std::cout << rank << '\t' << index << '\t' << name << '\t' << entropy << '\t' << mi
                     << '\t' << score << std::endl;
         });

    log_message("DONE", INFO, FINISH);
  }

  return 0;
}
