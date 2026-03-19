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

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <list>
#include <mrmr/missing.hpp>
#include <mrmr/mrmr.hpp>
#include <mrmr/mrmre.hpp>
#ifdef MRMR_HAS_CONTINUOUS
#include <mrmr/continuous_dataset.hpp>
#include <mrmr/mixed_dataset.hpp>
#endif
#include <stack>
#include <string>

#ifndef MRMR_VERSION
#define MRMR_VERSION "unknown"
#endif

enum verbosity_level : char { QUIET = 0, WARNING = 1, INFO = 2, DEBUG = 3 };
enum message_type : char { STANDARD = 0, START = 1, FINISH = 2 };

static verbosity_level VERBOSITY = WARNING;

static void short_usage(char const *program) {
  std::cerr << "Usage: " << program << " [OPTION]... [FILE]\n";
  std::cerr << "Try '" << program << " --help' for more information.\n";
}

static void usage(char const *program) {
  std::cerr << "Usage: " << program << " [OPTION]... [FILE]\n";
  std::cerr << "Compute mRMR feature ranking from standard input or a file.\n";
  std::cerr << "\n";
  std::cerr << "Data options:\n";
  std::cerr << "  -t, --delimiter=CHAR       field separator (default: TAB)\n";
  std::cerr << "  -c, --class=NUM            1-indexed class attribute (default: 1)\n";
#ifdef MRMR_HAS_CONTINUOUS
  std::cerr << "  -m, --method={discrete,continuous}\n";
  std::cerr << "                             MI estimation method (default: discrete)\n";
#endif
  std::cerr << "  -d, --discretize=VALUE     {round,floor,ceiling,truncate} (default: truncate)\n";
#ifdef MRMR_HAS_CONTINUOUS
  std::cerr << "                             only used with --method=discrete\n";
  std::cerr << "      --ksg-k=NUM            KSG neighbor count (default: 6)\n";
  std::cerr << "                             only used with --method=continuous\n";
#endif
  std::cerr << "      --missing=STRATEGY     {error,pairwise,impute-mode,impute-median,\n";
  std::cerr << "                              impute-mean} (default: error)\n";
  std::cerr << "\n";
  std::cerr << "Ensemble options (mRMRe):\n";
  std::cerr << "  -e, --ensemble=METHOD      {exhaustive,bootstrap} — enable ensemble mode\n";
  std::cerr << "  -n, --solutions=NUM        ensemble solutions (default: 10)\n";
  std::cerr << "  -k, --features=NUM         features per solution (default: all)\n";
  std::cerr << "  -s, --seed=NUM             random seed for bootstrap (default: 42)\n";
  std::cerr << "\n";
  std::cerr << "Output options:\n";
  std::cerr << "  -w, --write-data           output parsed/discretized data and exit\n";
  std::cerr << "  -i, --info                 show dataset summary and exit\n";
  std::cerr << "  -v, --verbosity=VALUE      {0,1,2,3,quiet,warning,info,debug} (default: 1)\n";
  std::cerr << "\n";
  std::cerr << "General:\n";
  std::cerr << "  -h, --help                 display this help and exit\n";
  std::cerr << "  -V, --version              output version information and exit\n";
}

static void log_message(char const *message, verbosity_level verbosity, message_type mtype) {
  using time_type = std::chrono::time_point<std::chrono::high_resolution_clock>;
  static std::stack<time_type, std::list<time_type>> time_stack;
  if (VERBOSITY >= verbosity) {
    if (mtype == STANDARD && !time_stack.empty()) {
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

// Helper: parse a positive unsigned long from optarg with full validation.
static bool parse_ulong(char const *optarg_str, unsigned long &out) {
  char *endptr = nullptr;
  errno = 0;
  out = std::strtoul(optarg_str, &endptr, 10);
  return endptr != optarg_str && *endptr == '\0' && errno != ERANGE;
}

// Long option indices for options without short flags
enum : std::uint16_t { OPT_KSG_K = 256, OPT_MISSING };

int main(int argc, char *argv[]) try {
  std::cout << std::scientific;
  std::cerr << std::scientific;

  using storage_type = unsigned char;
  using dataset_type = dataset<storage_type>;
  std::ifstream ifs;
  std::size_t class_attribute = 0;
  dataset_type::discretization_method discretize = dataset_type::TRUNCATE;
  bool discretization_chosen = false;
  bool just_write = false;
  bool show_info = false;
  char delimiter = '\t';

  // Method selection
  enum class mi_method : std::uint8_t { DISCRETE, CONTINUOUS };
  mi_method method = mi_method::DISCRETE;
  bool method_chosen = false;
  std::size_t ksg_k = 6;

  // Missing value handling (parsed and validated; strategies other than 'error'
  // are not yet wired into the dataset loading pipeline)
  missing_strategy missing = missing_strategy::ERROR;

  // Ensemble options
  bool ensemble_mode = false;
  mrmre_method ensemble_method = mrmre_method::EXHAUSTIVE;
  std::size_t solution_count = 10;
  std::size_t feature_count = 0;
  unsigned ensemble_seed = 42;

  static struct option long_options[] = {{"delimiter", required_argument, nullptr, 't'},
                                         {"class", required_argument, nullptr, 'c'},
                                         {"method", required_argument, nullptr, 'm'},
                                         {"discretize", required_argument, nullptr, 'd'},
                                         {"ksg-k", required_argument, nullptr, OPT_KSG_K},
                                         {"missing", required_argument, nullptr, OPT_MISSING},
                                         {"verbosity", required_argument, nullptr, 'v'},
                                         {"write-data", no_argument, nullptr, 'w'},
                                         {"info", no_argument, nullptr, 'i'},
                                         {"ensemble", required_argument, nullptr, 'e'},
                                         {"solutions", required_argument, nullptr, 'n'},
                                         {"features", required_argument, nullptr, 'k'},
                                         {"seed", required_argument, nullptr, 's'},
                                         {"help", no_argument, nullptr, 'h'},
                                         {"version", no_argument, nullptr, 'V'},
                                         {nullptr, 0, nullptr, 0}};

  int c;
  int option_index = 0;
  while ((c = getopt_long(argc, argv, "t:c:m:d:v:wie:n:k:s:hV", long_options, &option_index)) !=
         -1) {
    switch (c) {
    case 't':
      if (strcmp(optarg, "\\t") == 0) {
        delimiter = '\t';
      } else if (strlen(optarg) != 1) {
        std::cerr << argv[0] << ": -t --delimiter  must be a single character\n";
        return 1;
      } else {
        delimiter = optarg[0];
      }
      break;
    case 'c': {
      unsigned long val;
      if (!parse_ulong(optarg, val) || val == 0) {
        std::cerr << argv[0] << ": -c --class  must be a positive integer\n";
        return 1;
      }
      class_attribute = val - 1;
    } break;
    case 'm':
      if (strcmp(optarg, "discrete") == 0) {
        method = mi_method::DISCRETE;
      } else if (strcmp(optarg, "continuous") == 0) {
#ifdef MRMR_HAS_CONTINUOUS
        method = mi_method::CONTINUOUS;
#else
        std::cerr << argv[0]
                  << ": --method=continuous requires building with -DMRMR_CONTINUOUS=ON\n";
        return 1;
#endif
      } else {
        std::cerr << argv[0] << ": -m --method  must be one of {discrete,continuous}\n";
        return 1;
      }
      method_chosen = true;
      break;
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
    case OPT_KSG_K: {
      unsigned long val;
      if (!parse_ulong(optarg, val) || val == 0) {
        std::cerr << argv[0] << ": --ksg-k  must be a positive integer\n";
        return 1;
      }
      ksg_k = val;
    } break;
    case OPT_MISSING:
      if (strcmp(optarg, "error") == 0) {
        missing = missing_strategy::ERROR;
      } else if (strcmp(optarg, "pairwise") == 0) {
        missing = missing_strategy::PAIRWISE;
      } else if (strcmp(optarg, "impute-mode") == 0) {
        missing = missing_strategy::IMPUTE_MODE;
      } else if (strcmp(optarg, "impute-median") == 0) {
        missing = missing_strategy::IMPUTE_MEDIAN;
      } else if (strcmp(optarg, "impute-mean") == 0) {
        missing = missing_strategy::IMPUTE_MEAN;
      } else {
        std::cerr << argv[0]
                  << ": --missing  must be one of {error,pairwise,impute-mode,impute-median,"
                     "impute-mean}\n";
        return 1;
      }
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
        std::cerr << argv[0]
                  << ": -v --verbosity  must be one of {0,1,2,3,quiet,warning,info,debug}\n";
        short_usage(argv[0]);
        return 1;
      }
      break;
    case 'w':
      just_write = true;
      break;
    case 'i':
      show_info = true;
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
      unsigned long val;
      if (!parse_ulong(optarg, val) || val == 0) {
        std::cerr << argv[0] << ": -n --solutions  must be a positive integer\n";
        return 1;
      }
      solution_count = val;
    } break;
    case 'k': {
      unsigned long val;
      if (!parse_ulong(optarg, val) || val == 0) {
        std::cerr << argv[0] << ": -k --features  must be a positive integer\n";
        return 1;
      }
      feature_count = val;
    } break;
    case 's': {
      unsigned long val;
      if (!parse_ulong(optarg, val)) {
        std::cerr << argv[0] << ": -s --seed  must be a non-negative integer\n";
        return 1;
      }
      ensemble_seed = static_cast<unsigned>(val);
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

  // Validate flag combinations
#ifdef MRMR_HAS_CONTINUOUS
  if (method == mi_method::CONTINUOUS && discretization_chosen) {
    std::cerr << argv[0] << ": --discretize is not used with --method=continuous\n";
    return 1;
  }
  if (method == mi_method::DISCRETE && ksg_k != 6 && method_chosen) {
    std::cerr << argv[0] << ": --ksg-k is only used with --method=continuous\n";
    return 1;
  }
#endif

  // Open file if specified
  if (optind < argc) {
    if (optind == argc - 1) {
      ifs = std::ifstream(argv[optind]);
      if (!ifs.is_open()) {
        std::cerr << argv[0] << ": " << argv[optind] << ": No such file or directory\n";
        return 1;
      }
      log_message((std::string("FILE = ") + std::string(argv[optind])).c_str(), DEBUG, STANDARD);
    } else {
      std::cerr << argv[0] << ": too many arguments\n";
      short_usage(argv[0]);
      return 1;
    }
  }

  std::istream &input = ifs.is_open() ? ifs : std::cin;

  // --- Discrete method (default) ---
  if (method == mi_method::DISCRETE) {
    log_message("Reading and transforming dataset...", INFO, START);
    dataset_type data;
    try {
      data = dataset_type(input, discretize, delimiter, missing);
    } catch (std::exception const &e) {
      std::cerr << argv[0] << ": " << e.what() << "\n";
      return 2;
    }
    log_message("DONE", INFO, FINISH);

    if (!discretization_chosen) {
      log_message("No discretization method chosen. Default 'truncate' used...", WARNING, STANDARD);
    }

    // --info: show dataset summary
    if (show_info) {
      std::cout << "Instances:  " << data.num_instances() << "\n";
      std::cout << "Attributes: " << data.num_attributes() << "\n";
      std::cout << "Method:     discrete\n";
      std::cout << "\n";
      std::cout << "  #  Name                Type      Entropy\n";
      for (std::size_t a = 0; a < data.num_attributes(); ++a) {
        std::cout << "  " << (a + 1) << "  " << std::left << std::setw(20) << data.attribute_name(a)
                  << "discrete  " << std::fixed << std::setprecision(4) << data.attribute_entropy(a)
                  << "\n";
      }
      return 0;
    }

    if (just_write) {
      std::cout << data;
      return 0;
    }

    // Standard or ensemble mRMR on discrete data
    if (ensemble_mode) {
      if (feature_count == 0) {
        feature_count = data.num_attributes() - 1;
      }
      log_message("Computing mRMRe ensemble...", INFO, START);
      auto result = mrmre(data, class_attribute, feature_count, solution_count, ensemble_method,
                          ensemble_seed);
      log_message("DONE", INFO, FINISH);

      std::cout << "# Consensus Ranking\n";
      std::cout << "Rank\tIndex\tName\tFrequency\n";
      std::size_t rank = 1;
      for (auto attr_index : result.consensus_ranking) {
        if (attr_index == class_attribute) {
          continue;
        }
        std::size_t freq = result.feature_frequencies[attr_index];
        if (freq == 0 && rank > feature_count) {
          break;
        }
        std::cout << rank++ << '\t' << attr_index << '\t' << data.attribute_name(attr_index) << '\t'
                  << freq << '\n';
      }
    } else {
      log_message("Computing mRMR feature ranking...", INFO, START);
      std::cout << "Rank\tIndex\tName\tEntropy\tMutual Information\tmRMR Score\n";
      mrmr(data, class_attribute,
           [](std::size_t rank, std::size_t index, std::string const &name, double entropy,
              double mi, double score) {
             std::cout << rank << '\t' << index << '\t' << name << '\t' << entropy << '\t' << mi
                       << '\t' << score << '\n';
           });
      log_message("DONE", INFO, FINISH);
    }

    return 0;
  }

#ifdef MRMR_HAS_CONTINUOUS
  // --- Continuous/mixed method ---
  if (method == mi_method::CONTINUOUS) {
    log_message("Reading dataset (continuous/mixed mode)...", INFO, START);
    mixed_dataset data;
    try {
      data = mixed_dataset(input, delimiter, ksg_k);
    } catch (std::exception const &e) {
      std::cerr << argv[0] << ": " << e.what() << "\n";
      return 2;
    }
    log_message("DONE", INFO, FINISH);

    // --info: show dataset summary with types
    if (show_info) {
      std::cout << "Instances:  " << data.num_instances() << "\n";
      std::cout << "Attributes: " << data.num_attributes() << "\n";
      std::cout << "Method:     continuous (KSG, k=" << ksg_k << ")\n";
      std::cout << "\n";
      std::cout << "  #  Name                Type         Entropy\n";
      for (std::size_t a = 0; a < data.num_attributes(); ++a) {
        std::string type_str =
            data.type_of(a) == column_type::CONTINUOUS ? "continuous" : "discrete  ";
        std::cout << "  " << (a + 1) << "  " << std::left << std::setw(20) << data.attribute_name(a)
                  << type_str << "  " << std::fixed << std::setprecision(4)
                  << data.attribute_entropy(a) << "\n";
      }
      return 0;
    }

    // Standard or ensemble mRMR on mixed data
    if (ensemble_mode) {
      if (feature_count == 0) {
        feature_count = data.num_attributes() - 1;
      }
      log_message("Computing mRMRe ensemble (continuous)...", INFO, START);
      try {
        auto result = mrmre(data, class_attribute, feature_count, solution_count, ensemble_method,
                            ensemble_seed);
        log_message("DONE", INFO, FINISH);

        std::cout << "# Consensus Ranking\n";
        std::cout << "Rank\tIndex\tName\tFrequency\n";
        std::size_t rank = 1;
        for (auto attr_index : result.consensus_ranking) {
          if (attr_index == class_attribute) {
            continue;
          }
          std::size_t freq = result.feature_frequencies[attr_index];
          if (freq == 0 && rank > feature_count) {
            break;
          }
          std::cout << rank++ << '\t' << attr_index << '\t' << data.attribute_name(attr_index)
                    << '\t' << freq << '\n';
        }
      } catch (std::exception const &e) {
        std::cerr << argv[0] << ": " << e.what() << "\n";
        return 2;
      }
      return 0;
    }

    log_message("Computing mRMR feature ranking (KSG MI)...", INFO, START);
    std::cout << "Rank\tIndex\tName\tMI(class)\tmRMR Score\n";
    mrmr(data, class_attribute,
         [](std::size_t rank, std::size_t index, std::string const &name, double /*entropy*/,
            double mi, double score) {
           std::cout << rank << '\t' << index << '\t' << name << '\t' << mi << '\t' << score
                     << '\n';
         });
    log_message("DONE", INFO, FINISH);

    return 0;
  }
#endif

  std::cerr << argv[0] << ": unknown method\n";
  return 1;
} catch (std::exception const &e) {
  std::cerr << "error: " << e.what() << '\n';
  return 1;
}
