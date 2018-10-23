/**
 * @file Mesh.h
 * @brief The Mesh class for the alternative C++ build.
 * @date November, 2016
 * @author Geoffrey Gunow, MIT, Course 22 (geogunow@mit.edu)
 */

#ifndef SWIG
#ifndef MESH_H
#define MESH_H

#include "Universe.h"
#include "Solver.h"
#include "Geometry.h"


/* A Vector3D is simply a 3-dimensional std::vector of doubles */
typedef std::vector<std::vector<std::vector<double> > > Vector3D;


/**
 * @enum RxType
 * @brief The type of reaction to be tallied
*/
enum RxType {

  FISSION_RX,

  TOTAL_RX,

  ABSORPTION_RX,

  FLUX_RX
};


/**
 * @class Mesh Mesh.h "src/Mesh.h"
 * @brief A Mesh is a lattice overlaid on the Geometry across which reaction
 *        rates can be tallied from converged scalar fluxes in a Solver
 */
class Mesh {

  /* The solver from which scalar fluxes and cross-sections are extracted */
  Solver* _solver;

  /* The lattice defining the zones across which reaction rates are tallied */
  Lattice* _lattice;

  /* A flag indicating whether a lattice has been allocated internally */
  bool _lattice_allocated;

public:

  Mesh(Solver* solver, Lattice* lattice=NULL);
  virtual ~Mesh();

  void createLattice(int num_x, int num_y, int num_z);
  void setLattice(Lattice* lattice);
  std::vector<double> getReactionRates(RxType rx);
  Vector3D getFormattedReactionRates(RxType rx);
  Vector3D getFormattedReactionRates(std::vector<std::vector<double>> 
                                     widths_offsets, RxType rx);
  


};

#endif
#endif
