/**
 * @file log.h
 * @brief Utility functions for writing log messages to the screen
 * @details Applies level-based logging to print formatted messages
 *          to the screen and to a log file.
 * @author William Boyd (wboyd@mit.edu)
 * @date January 22, 2012
 *
 */

#ifndef LOG_H_
#define LOG_H_

#ifdef __cplusplus
#ifdef SWIG
#include "Python.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string.h>
#include <stdexcept>
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <omp.h>
#include <vector>
#endif

#ifdef MPIx
#include <mpi.h>
#endif

#ifdef SWIG
#define printf PySys_WriteStdout
#endif


/**
 * @enum logLevels
 * @brief Logging levels characterize an ordered set of message types
 *        which may be printed to the screen.
 */


/**
 * @var logLevel
 * @brief Logging levels characterize an ordered set of message types
 *        which may be printed to the screen.
 */
typedef enum logLevels {
  /** A debugging message */
  DEBUG,

  /** An informational but verbose message */
  INFO,

  /** A brief progress update on run progress */
  NORMAL,

  /** A brief progress update by node on run progress */
  NODAL,

  /** A message of a single line of characters */
  SEPARATOR,

  /** A message centered within a line of characters */
  HEADER,

  /** A message sandwiched between two lines of characters */
  TITLE,

  /** A message for to warn the user */
  WARNING,

  /** A message to warn of critical program conditions */
  CRITICAL,

  /** A message containing program results */
  RESULT,

  /** A messsage for unit testing */
  UNITTEST,

  /** A message reporting error conditions */
  ERROR
} logLevel;


/**
 * @brief A function stub used to convert C++ exceptions into Python exceptions
 *        through SWIG.
 * @details This method is not defined in the C++ source. It is defined in the
 *          SWIG inteface files (i.e., openmoc/openmoc.i)
 * @param msg a character array for the exception message
 */
extern void set_err(const char *msg);

void initialize_logger();
void set_output_directory(char* directory);
const char* get_output_directory();
void set_log_filename(char* filename);
const char* get_log_filename();

void set_separator_character(char c);
char get_separator_character();
void set_header_character(char c);
char get_header_character();
void set_title_character(char c);
char get_title_character();
void set_line_length(int length);
void set_log_level(const char* new_level);
int get_log_level();

void log_printf(logLevel level, const char *format, ...);
std::string create_multiline_msg(std::string level, std::string message);
#ifdef MPIx
void log_set_ranks(MPI_Comm comm);
#endif

struct Runtime_Parametres {
  Runtime_Parametres() : _debug_flag(false), _NDx(1), _NDy(1), _NDz(1),
    _NMx(1), _NMy(1), _NMz(1), _NCx(1), _NCy(1), _NCz(1), _NOx(1), _NOy(1), _NOz(1),
    _num_threads(1), _azim_spacing(0.05), _num_azim(64), _polar_spacing(0.75), 
    _num_polar(10), _tolerance(1.0E-5), _max_iters(40), _log_level("NORMAL"),
    _knearest(1), _CMFD_flux_update_on(true), _CMFD_centroid_update_on(false),
    _use_axial_interpolation(false), _log_filename(NULL), 
    _linear_solver(true)
    {}
  bool _debug_flag; //To debug or not when running, dead while loop
  int _NDx, _NDy, _NDz; //Domain decomposation Topo
  int _NMx, _NMy, _NMz; //Moduler Topo. definition in a sub-domain
  int _NCx, _NCy, _NCz; //CMFD Topo
  int _NOx, _NOy, _NOz; //Reaction rate output Topo
  int _num_threads; //Number of OpenMP threads
  double _azim_spacing;
  int _num_azim;
  double _polar_spacing;
  int _num_polar;
  double _tolerance;
  int _max_iters;
  const char* _log_level;
  int _knearest; //the order of knearest update
  bool _CMFD_flux_update_on;//CMFD flux update
  bool _CMFD_centroid_update_on; //CMFD_centroid_update or conventional update
  bool _use_axial_interpolation; //whether to use axial interpolation for CMFD update
  char* _log_filename; //Log file name
  std::string _geo_filename; //Geometry file name
  
  /** Physical dimensions of non-uniform CMFD meshes (for whole geometry) */
  std::vector<double> _cell_widths_x;
  std::vector<double> _cell_widths_y;
  std::vector<double> _cell_widths_z;
  
  bool _linear_solver; //Linear source if true

};

int set_Runtime_Parametres(Runtime_Parametres &RP, int argc, char *argv[]);

#endif /* LOG_H_ */
