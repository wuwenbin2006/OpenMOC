#ifdef __cplusplus
#include "log.h"
#endif

#define LOG_C


/**
 * @var log_level
 * @brief Minimum level of logging messages printed to the screen and log file.
 * @details The default logging level is NORMAL.
 */
static logLevel log_level = NORMAL;


/**
 * @var log_filename
 * @brief The name of the output log file. By default this is an empty character
 *        array and must be set for messages to be redirected to a log file.
 */
static std::string log_filename = "";


/**
 * @var output_directory
 * @brief The directory in which a "log" folder will be created for logfiles.
 * @details By default this is the same directory as that where the logger
 *          is invoked by an input file.
 */
static std::string output_directory = ".";


/**
 * @var logging
 * @brief A switch which is set to true once the first message is logged.
 * @details The logging switch is needed to indicate whether the output
 *          log file has been created or not.
 */
static bool logging = false;


/**
 * @var separator_char
 * @brief The character to use for SEPARATOR log messages. The default is "-".
 */
static char separator_char = '*';


/**
 * @var header_char
 * @brief The character to use for HEADER log messages. The default is "*".
 */
static char header_char = '*';


/**
 * @var title_char
 * @brief The character to use for TITLE log messages. The default is "*".
 */
static char title_char = '*';


/**
 * @var line_length
 * @brief The maximum line length for a log message.
 * @details The default is 67 which contributes to the standard 80 characters
 *          when accounting for the log level prepended to each log message.
 */
static int line_length = 67;

/* Rank of the domain */
static int rank = 0;

/* Number of domains */
static int num_ranks = 1;

#ifdef MPIx
/* MPI communicator to transfer data with */
static MPI_Comm _MPI_comm;

/* Boolean to test if MPI is used */
static bool _MPI_present = false;
#endif


/**
 * @var log_error_lock
 * @brief OpenMP mutex lock for ERROR messages which throw exceptions
 */
static omp_lock_t log_error_lock;


/**
 * @brief Initializes the logger for use.
 * @details This should be immediately called when the logger is imported
 *          into Python and before any of its other routines are called. The
 *          routine initializes an OpenMP mutual exclusion lock which is used
 *          to preclude race conditions from occurring when an ERROR message
 *          is reported and program execution is terminated.
 */
void initialize_logger() {
  /* Initialize OpenMP mutex lock for ERROR messages with exceptions */
  omp_init_lock(&log_error_lock);
}


/**
 * @brief Sets the output directory for log files.
 * @details If the directory does not exist, it creates it for the user.
 * @param directory a character array for the log file directory
 */
void set_output_directory(char* directory) {

  output_directory = std::string(directory);
  std::string log_directory;

  /* Check to see if directory exists - if not, create it */
  struct stat st;
  if ((stat(directory, &st)) == 0) {
    log_directory = std::string("") + directory + std::string("/log");
    mkdir(log_directory.c_str(), S_IRWXU);
  }
}


/**
 * @brief Returns the output directory for log files.
 * @return a character array for the log file directory
 */
const char* get_output_directory() {
  return output_directory.c_str();
}


/**
 * @brief Sets the name for the log file.
 * @param filename a character array for log filename
 */
void set_log_filename(char* filename) {
  log_filename = std::string(filename);
}


/**
 * @brief Returns the log filename.
 * @return a character array for the log filename
 */
const char* get_log_filename() {
  return log_filename.c_str();
}


/**
 * @brief Sets the character to be used when printing SEPARATOR log messages.
 * @param c the character for SEPARATOR log messages
 */
void set_separator_character(char c) {
  separator_char = c;
}


/**
 * @brief Returns the character used to format SEPARATOR log messages.
 * @return the character used for SEPARATOR log messages
 */
char get_separator_character() {
  return separator_char;
}


/**
 * @brief Sets the character to be used when printing HEADER log messages.
 * @param c the character for HEADER log messages
 */
void set_header_character(char c) {
  header_char = c;
}


/**
 * @brief Returns the character used to format HEADER type log messages.
 * @return the character used for HEADER type log messages
 */
char get_header_character() {
  return header_char;
}


/**
 * @brief Sets the character to be used when printing TITLE log messages.
 * @param c the character for TITLE log messages
 */
void set_title_character(char c) {
  title_char = c;
}


/**
 * @brief Returns the character used to format TITLE log messages.
 * @return the character used for TITLE log messages
 */
char get_title_character() {
  return title_char;
}


/**
 * @brief Sets the maximum line length for log messages.
 * @details Messages longer than this amount will be broken up into
            multiline messages.
 * @param length the maximum log message line length in characters
 */
void set_line_length(int length) {
  line_length = length;
}


/**
 * @brief Sets the minimum log message level which will be printed to the
 *        console and to the log file.
 * @param new_level the minimum logging level as a character array
 */
void set_log_level(const char* new_level) {

  if (strcmp("DEBUG", new_level) == 0) {
    log_level = DEBUG;
    log_printf(INFO, "Logging level set to DEBUG");
  }
  else if (strcmp("INFO", new_level) == 0) {
    log_level = INFO;
    log_printf(INFO, "Logging level set to INFO");
  }
  else if (strcmp("NORMAL", new_level) == 0) {
    log_level = NORMAL;
    log_printf(INFO, "Logging level set to NORMAL");
  }
  else if (strcmp("SEPARATOR", new_level) == 0) {
    log_level = SEPARATOR;
    log_printf(INFO, "Logging level set to SEPARATOR");
  }
  else if (strcmp("HEADER", new_level) == 0) {
    log_level = HEADER;
    log_printf(INFO, "Logging level set to HEADER");
  }
  else if (strcmp("TITLE", new_level) == 0) {
    log_level = TITLE;
    log_printf(INFO, "Logging level set to TITLE");
  }
  else if (strcmp("WARNING", new_level) == 0) {
    log_level = WARNING;
    log_printf(INFO, "Logging level set to WARNING");
  }
  else if (strcmp("CRITICAL", new_level) == 0) {
    log_level = CRITICAL;
    log_printf(INFO, "Logging level set to CRITICAL");
  }
  else if (strcmp("RESULT", new_level) == 0) {
    log_level = RESULT;
    log_printf(INFO, "Logging level set to RESULT");
  }
  else if (strcmp("UNITTEST", new_level) == 0) {
    log_level = UNITTEST;
    log_printf(INFO, "Logging level set to UNITTEST");
  }
  else if (strcmp("ERROR", new_level) == 0) {
      log_level = ERROR;
      log_printf(INFO, "Logging level set to ERROR");
  }
}


/**
 * @brief Return the minimum level for log messages printed to the screen.
 * @return the minimum level for log messages
 */
int get_log_level() {
  return log_level;
}


/**
 * @brief Print a formatted message to the console.
 * @details If the logging level is ERROR, this function will throw a
 *          runtime exception
 * @param level the logging level for this message
 * @param format variable list of C++ formatted arguments
 */
void log_printf(logLevel level, const char* format, ...) {

  char message[1024];
  std::string msg_string;
  if (level >= log_level) {
    va_list args;

    va_start(args, format);
    vsprintf(message, format, args);
    va_end(args);

    /* Append the log level to the message */
    switch (level) {
    case (DEBUG):
      {
        std::string msg = std::string(message);
        std::string level_prefix = "[  DEBUG  ]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (INFO):
      {
        std::string msg = std::string(message);
        std::string level_prefix = "[  INFO   ]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (NORMAL):
      {
        if (rank != 0)
          return;

        std::string msg = std::string(message);
        std::string level_prefix = "[  NORMAL ]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (NODAL):
      {

        std::string msg = std::string(message);
        std::stringstream ss;
        ss << "[  NODE " << rank << " ]  ";
        std::string level_prefix = ss.str();

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }

    case (SEPARATOR):
      {
        if (rank != 0)
          return;
        std::string pad = std::string(line_length, separator_char);
        std::string prefix = std::string("[SEPARATOR]  ");
        std::stringstream ss;
        ss << prefix << pad << "\n";
        msg_string = ss.str();
        break;
      }
    case (HEADER):
      {
        if (rank != 0)
          return;
        int size = strlen(message);
        int halfpad = (line_length - 4 - size) / 2;
        std::string pad1 = std::string(halfpad, header_char);
        std::string pad2 = std::string(halfpad +
                           (line_length - 4 - size) % 2, header_char);
        std::string prefix = std::string("[  HEADER ]  ");
        std::stringstream ss;
        ss << prefix << pad1 << "  " << message << "  " << pad2 << "\n";
        msg_string = ss.str();
        break;
      }
    case (TITLE):
      {
        if (rank != 0)
          return;
        int size = strlen(message);
        int halfpad = (line_length - size) / 2;
        std::string pad = std::string(halfpad, ' ');
        std::string prefix = std::string("[  TITLE  ]  ");
        std::stringstream ss;
        ss << prefix << std::string(line_length, title_char) << "\n";
        ss << prefix << pad << message << pad << "\n";
        ss << prefix << std::string(line_length, title_char) << "\n";
        msg_string = ss.str();
        break;
      }
    case (WARNING):
      {
        std::string msg = std::string(message);
        std::string level_prefix = "[ WARNING ]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (CRITICAL):
      {
        std::string msg = std::string(message);
        std::string level_prefix = "[ CRITICAL]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (RESULT):
      {
        if (rank != 0)
          return;
        std::string msg = std::string(message);
        std::string level_prefix = "[  RESULT ]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (UNITTEST):
      {
        std::string msg = std::string(message);
        std::string level_prefix = "[   TEST  ]  ";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";

        break;
      }
    case (ERROR):
      {
        /* Create message based on runtime error stack */
        std::string msg = std::string(message);
        std::string level_prefix = "";

        /* If message is too long for a line, split into many lines */
        if (int(msg.length()) > line_length)
          msg_string = create_multiline_msg(level_prefix, msg);

        /* Puts message on single line */
        else
          msg_string = level_prefix + msg + "\n";
      }
    }

    /* If this is our first time logging, add a header with date, time */
    if (!logging) {

      /*
      if (rank != 0)
        return;
      */

      /* If output directory was not defined by user, then log file is
       * written to a "log" subdirectory. Create it if it doesn't exist */
      if (output_directory.compare(".") == 0)
        set_output_directory((char*)".");

      /* Write the message to the output file */
      std::ofstream log_file;
      log_file.open((output_directory + "/log/" + log_filename).c_str(),
                   std::ios::app);

      /* Append date, time to the top of log output file */
      time_t rawtime;
      struct tm * timeinfo;
      time (&rawtime);
      timeinfo = localtime (&rawtime);
      log_file << "Current local time and date: " << asctime(timeinfo);
      logging = true;

      log_file.close();
    }

    /* Write the log message to the log_file */
    std::ofstream log_file;
    log_file.open((output_directory + "/log/" + log_filename).c_str(),
                  std::ios::app);
    log_file << msg_string;
    log_file.close();

    /* Write the log message to the shell */
    if (level == ERROR) {
      omp_set_lock(&log_error_lock);
      {
#ifdef MPIx
        if (_MPI_present) {
          printf("%s", "[  ERROR  ] ");
          printf("%s", msg_string.c_str());
          fflush(stdout);
          MPI_Finalize();
        }
#endif
        throw std::logic_error(msg_string.c_str());
      }
      omp_unset_lock(&log_error_lock);
    }
    else {
      printf("%s", &msg_string[0]);
      fflush(stdout);
    }
  }
}


/**
 * @brief Breaks up a message which is too long for a single line into a
 *        multiline message.
 * @details This is an internal function which is called by log_printf and
 *          should not be called directly by the user.
 * @param level a string containing log level prefix
 * @param message a string containing the log message
 * @return a string with a formatted multiline message
 */
std::string create_multiline_msg(std::string level, std::string message) {

  int size = message.length();

  std::string substring;
  int start = 0;
  int end = line_length;

  std::string msg_string;

  /* Loop over msg creating substrings for each line */
  while (end < size + line_length) {

    /* Append log level to the beginning of each line */
    msg_string += level;

    /* Begin multiline messages with ellipsis */
    if (start != 0)
      msg_string += "... ";

    /* Find the current full length substring for line*/
    substring = message.substr(start, line_length);

    /* Truncate substring to last complete word */
    if (end < size-1) {
      int endspace = substring.find_last_of(" ");
      if (message.at(endspace+1) != ' ' &&
          endspace != int(std::string::npos)) {
        end -= line_length - endspace;
        substring = message.substr(start, end-start);
      }
    }

    /* concatenate substring to output message */
    msg_string += substring + "\n";

    /* Reduce line length to account for ellipsis prefix */
    if (start == 0)
      line_length -= 4;

    /* Update substring indices */
    start = end;
    end += line_length + 1;
  }

  /* Reset line length */
  line_length += 4;

  return msg_string;
}


/**
 * @brief Set the rank of current domain in the communicator. Only rank 0 print
 *        to stdout or a logfile, except for prints with log_level NODAL.
 * @param comm a MPI communicator to transfer data with
 */
#ifdef MPIx
void log_set_ranks(MPI_Comm comm) {
  _MPI_comm = comm;
  _MPI_present = true;
  MPI_Comm_size(comm, &num_ranks);
  MPI_Comm_rank(comm, &rank);
}
#endif


int set_Runtime_Parametres(Runtime_Parametres &RP, int argc, char *argv[]) {
  
  int arg_index = 0;
  int print_usage = 0;
  
  while (arg_index < argc) {
    if(strcmp(argv[arg_index], "-debug") == 0) {
      arg_index++;
      RP._debug_flag = atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-ndx") == 0) {
      arg_index++;
      RP._NDx=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-ndy") == 0){
      arg_index++;
      RP._NDy=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-ndz") == 0) {
      arg_index++;
      RP._NDz=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-nmx") == 0) {
      arg_index++;
      RP._NMx=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-nmy") == 0) {
      arg_index++;
      RP._NMy=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-nmz") == 0 ) {
      arg_index++;
      RP._NMz=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-ncx") == 0) {
      arg_index++;
      RP._NCx=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-ncy") == 0) {
      arg_index++;
      RP._NCy=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-ncz") == 0) {
      arg_index++;
      RP._NCz=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-nox") == 0) {
      arg_index++;
      RP._NOx=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-noy") == 0) {
      arg_index++;
      RP._NOy=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-noz") == 0) {
      arg_index++;
      RP._NOz=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-num_threads") == 0 ) {
      arg_index++;
      RP._num_threads=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-azim_spacing") == 0) {
      arg_index++;
      RP._azim_spacing=atof(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-num_azim") == 0) {
      arg_index++;
      RP._num_azim=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-polar_spacing") == 0) {
      arg_index++;
      RP._polar_spacing=atof(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-num_polar") == 0) {
      arg_index++;
      RP._num_polar=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-tolerance") == 0) {
      arg_index++;
      RP._tolerance=atof(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-max_iters") == 0) {
      arg_index++;
      RP._max_iters=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-log_level") == 0) {
      arg_index++;
      RP._log_level=argv[arg_index++];
    }
    else if(strcmp(argv[arg_index], "-knearest") == 0) {
      arg_index++;
      RP._knearest=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-CMFD_flux_update_on") == 0) {
      arg_index++;
      RP._CMFD_flux_update_on=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-CMFD_centroid_update_on") == 0) {
      arg_index++;
      RP._CMFD_centroid_update_on=atoi(argv[arg_index++]);
    } 
    else if(strcmp(argv[arg_index], "-use_axial_interpolation") == 0) {
      arg_index++;
      RP._use_axial_interpolation=atoi(argv[arg_index++]);
    } 
    else if(strcmp(argv[arg_index], "-help") == 0) {
      print_usage = 1;
      break;
    }
    else if(strcmp(argv[arg_index], "-log_filename") == 0) {
      arg_index++;
      RP._log_filename = argv[arg_index++];
    }
    else if(strcmp(argv[arg_index], "-geo_file_name") == 0) {
      arg_index++;
      RP._geo_filename = std::string(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-widths_x") == 0) {
      arg_index++;
      char *buf = argv[arg_index];
      char *outer_ptr = NULL;
      char *p;
      while((p = strtok_r(buf, ",", &outer_ptr)) != NULL) {
        RP._cell_widths_x.push_back(atof(p));
        buf = NULL;
      }
      arg_index++;
    }
    else if(strcmp(argv[arg_index], "-widths_y") == 0) {
      arg_index++;
      char *buf = argv[arg_index];
      char *outer_ptr = NULL;
      char *p;
      while((p = strtok_r(buf, ",", &outer_ptr)) != NULL) {
        RP._cell_widths_y.push_back(atof(p));
        buf = NULL;
      }
      arg_index++;      
    }
    else if(strcmp(argv[arg_index], "-widths_z") == 0) {
      arg_index++;
      char *buf = argv[arg_index];
      char *outer_ptr = NULL;
      char *p;
      while((p = strtok_r(buf, ",", &outer_ptr)) != NULL) {
        RP._cell_widths_z.push_back(atof(p));
        buf = NULL;
      }
      arg_index++;
    }
    else if(strcmp(argv[arg_index], "-ls_solver") == 0) {
      arg_index++;
      RP._linear_solver=atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-seg_zones") == 0) {
      arg_index++;
      char *buf = argv[arg_index];
      char *outer_ptr = NULL;
      char *p;
      while((p = strtok_r(buf, ",", &outer_ptr)) != NULL) {
        RP._seg_zones.push_back(atof(p));
        buf = NULL;
      }
      arg_index++;
    }
    else if(strcmp(argv[arg_index], "-MOC_src_residual_type") == 0) {
      arg_index++;
      RP._MOC_src_residual_type = atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-SOR_factor") == 0) {
      arg_index++;
      RP._SOR_factor = atof(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-CMFD_relaxation_factor") == 0) {
      arg_index++;
      RP._CMFD_relaxation_factor = atof(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-segmentation_type") == 0) {
      arg_index++;
      RP._segmentation_type = atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-CMFD_group_structure") == 0) {
      arg_index++;
      char *buf = argv[arg_index];
      char *outer_ptr = NULL;
      char *inner_ptr = NULL;
      char *p;
      while((p = strtok_r(buf, "/", &outer_ptr)) != NULL) {
        buf = p;
        std::vector<int> tmp;
        while((p = strtok_r(buf, ",", &inner_ptr)) != NULL) {
          tmp.push_back(atoi(p));
          buf = NULL;
        }
        
        RP._CMFD_group_structure.push_back(tmp);
        buf = NULL;
      }
      arg_index++;
    }  
  
  
  
  
    else if(strcmp(argv[arg_index], "-verbose_report") == 0) {
      arg_index++;
      RP._verbose_report = atoi(argv[arg_index++]);
    }
    else if(strcmp(argv[arg_index], "-time_report") == 0) {
      arg_index++;
      RP._time_report = atoi(argv[arg_index++]);
    } 
    else {
      arg_index++;
    }
  }
  int myid;
  //-num_threads 2 -azim_spacing 0.05 -num_azim 16 -polar_spacing 1.0 -num_polar 6 -tolerance 1.0e-5 \
  //-max_iters 40 -log_level DEBUG -knearest 3 -CMFD_flux_update_on 1 -CMFD_centroid_update_on 1
#ifdef MPIx
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);
#endif
  if ((print_usage) && (myid == 0)) {
    printf("\nRuntime_Parametres usage To be fixup\n");
    printf("Usage: %s [<options>], default value in ()\n", argv[0]);
    printf("\n");
    printf("-debug                  : 1 or (0), stuck in infinite while loop\n");
    printf("-ndx, -ndy, -ndz        : (1,1,1) domain decomposation Topo\n");
    printf("-nmx, -nmy, -nmz        : (1,1,1) modular Topo in a domain\n");
    printf("-ncx, -ncy, -ncz        : (1,1,1)CMFD lattice Topo\n");
    printf("-nox, -noy, -noz        : (1,1,1)reaction output Topo\n");
    printf("-num_threads            : (1) Number of OpenMP threads to use\n");
    printf("-azim_spacing           : (0.05)\n");
    printf("-num_azim               : (64)\n");
    printf("-polar_spacing          : (0.75)\n");
    printf("-num_polar              : (10)\n");
    printf("-MOC_src_tolerance      : (1.0E-5) MOC source convergence tolerance\n");
    printf("-MOC_src_residual_type  : (1-FISSION_SOURCE) 0-SCALAR_FLUX, 1-FISSION_SOURCE, 2-TOTAL_SOURCE\n");

    
    printf("-max_iters              : (40) Max. Number of outter iterations\n");
    printf("-log_level              : (NORMAL)\n");
    printf("-knearest               : (1) knearest CMFD update\n");
    printf("-CMFD_flux_update_on    : (1)switch of the CMFD update\n");
    printf("-CMFD_centroid_update_on: (1)switch of the CMFD knearest centroid update\n");
    printf("-use_axial_interpolation: (0)switch of the CMFD axial interpolation update\n");
    printf("-SOR_factor:            : (1.0)set CMFD SOR relaxation factor\n");
    printf("-CMFD_relaxation_factor : (1.0)set CMFD relaxation factor\n");
    printf("-CMFD_group_structure   : (No group condensation) set CMFD group structure with ',' and '/' \n");


    printf("-log_file_name          : (NULL) the file name of the log file\n");
    printf("-geo_file_name          : (NULL) the file name of the geometry file\n");
    printf("-widths_x               : (null) the widths of non-uniform CMFD meshes in x direction\n");
    printf("-widths_y               : (null) the widths of non-uniform CMFD meshes in y direction\n");
    printf("-widths_z               : (null) the widths of non-uniform CMFD meshes in z direction\n");
    printf("-ls_solver              : (1) set the linear source solver\n");
    printf("-seg_zones              : (null) set the segmentation zones\n");
    printf("-segmentation_type      : (3-OTF_STACKS) 0-EXPLICIT_2D, 1-EXPLICIT_3D, 2-OTF_TRACKS, 3-OTF_STACKS \n");
    
    printf("-verbose_report         : (1)switch of the verbose iteration report\n");
    printf("-time_report            : (1)switch of the time report\n");
    
    printf("\n");
  }
  
  if (print_usage) {
#ifdef MPIx
    MPI_Finalize();
#endif
    exit(0);
    //return (0);
  }
}
