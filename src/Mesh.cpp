#include "Mesh.h"

#ifndef SWIG

/**
 * @brief The Mesh constructor
 * @details If no lattice is given, a default lattice can be constructed with
 *          the Mesh::createLattice function.
 * @param solver The solver from which scalar fluxes and cross-sections are
 *        extracted
 * @param lattice An optional parameter for the lattice across which reaction
 *        rates are tallied
 */
Mesh::Mesh(Solver* solver, Lattice* lattice) {
  _solver = solver;
  _lattice = lattice;
  _lattice_allocated = false;
}


/**
 * @brief The Mesh destrcutor deletes its lattice if the lattice was allocated
 *        internally
 */
Mesh::~Mesh() {
  if (_lattice_allocated)
    delete _lattice;
}


/**
 * @brief Creates an internal lattice over which to tally reaction rates with
 *        the user-input dimensions
 * @param num_x the number of mesh cells in the x-direction
 * @param num_y the number of mesh cells in the y-direction
 * @param num_z the number of mesh cells in the z-direction
 */
void Mesh::createLattice(int num_x, int num_y, int num_z) {

  /* Delete the current lattice if currently allocated */
  if (_lattice_allocated)
    delete _lattice;

  /* Allocate a new lattice */
  _lattice = new Lattice();
  _lattice->setNumX(num_x);
  _lattice->setNumY(num_y);
  _lattice->setNumZ(num_z);
  _lattice_allocated = true;

  /* Get the root universe */
  Geometry* geometry = _solver->getGeometry();
  Universe* root_universe = geometry->getRootUniverse();

  /* Determine the geometry widths in each direction */
  double width_x = (root_universe->getMaxX() - root_universe->getMinX())/num_x;
  double width_y = (root_universe->getMaxY() - root_universe->getMinY())/num_y;
  double width_z = (root_universe->getMaxZ() - root_universe->getMinZ())/num_z;

  /* Determine the center-point of the geometry */
  double offset_x = (root_universe->getMinX() + root_universe->getMaxX()) / 2;
  double offset_y = (root_universe->getMinY() + root_universe->getMaxY()) / 2;
  double offset_z = (root_universe->getMinZ() + root_universe->getMaxZ()) / 2;

  /* Create the Mesh lattice */
  _lattice->setWidth(width_x, width_y, width_z);
  _lattice->setOffset(offset_x, offset_y, offset_z);
  _lattice->computeSizes();
}

void Mesh::setLattice(Lattice* lattice) {
  /* Delete the current lattice if currently allocated */
  if (_lattice_allocated)
    delete _lattice;
  
  _lattice = lattice;
  _lattice_allocated = false;
}

/**
 * @brief Tallies reaction rates of the given type over the Mesh lattice
 * @param rx The type of reaction to tally
 * @return The reaction rates in a 1D vector indexed by the lattice cell IDs
 */
std::vector<double> Mesh::getReactionRates(RxType rx) {

  /* Check that the Mesh contains a lattice */
  if (_lattice == NULL)
    log_printf(ERROR, "A Lattice must be set or created to get reaction rates "
                      "form a Mesh object");

  /* Extract fluxes and geometry information */
  Geometry* geometry = _solver->getGeometry();
  FP_PRECISION* volumes = _solver->getTrackGenerator()->getFSRVolumesBuffer();
  FP_PRECISION* fluxes = _solver->getFluxesArray();
  long num_fsrs = geometry->getNumFSRs();

  /* Create a 1D array of reaction rates with the appropriate size */
  std::vector<double> rx_rates;
  int size = _lattice->getNumX() * _lattice->getNumY() * _lattice->getNumZ();
  rx_rates.resize(size);

  /* Extract the number of groups */
  int num_groups = geometry->getNumEnergyGroups();

  /* Create temporary array for cross-sections */
  FP_PRECISION temp_array[num_groups];

  /* Loop over all flat source regions */
  for (long r=0; r < num_fsrs; r++) {

    /* Determine the FSR material and which Mesh cell contains it */
    Material* mat = geometry->findFSRMaterial(r);
    Point* pt = geometry->getFSRPoint(r);
    int lat_cell = _lattice->getLatticeCell(pt);

    /* Determine the volume and cross-sections of the FSR */
    FP_PRECISION volume = volumes[r];
    FP_PRECISION* xs_array;
    switch (rx) {
      case FISSION_RX:
        xs_array = mat->getSigmaF();
        break;
      case TOTAL_RX:
        xs_array = mat->getSigmaT();
        break;
      case ABSORPTION_RX:
        {
          xs_array = temp_array;
          FP_PRECISION* scattering = mat->getSigmaS();
          for (int g=0; g < num_groups; g++) {
            xs_array[g] = 0.0;
            for (int gp=0; gp < num_groups; gp++) {
              xs_array[g] += scattering[gp*num_groups + g];
            }
          }
        }
        break;
      case FLUX_RX:
        xs_array = temp_array;
        for (int g=0; g < num_groups; g++)
          xs_array[g] = 1.0;
        break;
      default:
        log_printf(ERROR, "Unrecognized reaction type in Mesh object");
    }

    /* Tally the reaction rates summed across all groups */
    for (int g=0; g < num_groups; g++) {
      double xs = xs_array[g];
      rx_rates.at(lat_cell) += fluxes[r*num_groups + g] * volume * xs;
    }
  }

  /* If domain decomposed, do a reduction */
#ifdef MPIx
  if (geometry->isDomainDecomposed()) {
    MPI_Comm comm = geometry->getMPICart();
    double* rx_rates_array = &rx_rates[0];
    double* rx_rates_send = new double[size];
    for (int i=0; i < size; i++)
      rx_rates_send[i] = rx_rates_array[i];
    MPI_Allreduce(rx_rates_send, rx_rates_array, size, MPI_DOUBLE, MPI_SUM,
                  comm);
    delete [] rx_rates_send;
  }
#endif

  return rx_rates;
}


/**
 * @brief Tallies reaction rates of the given type over the Mesh lattice
 * @param rx The type of reaction to tally
 * @return The reaction rates in a 3D vector indexed by the lattice cell
 *         x, y, and z indexes
 */
Vector3D Mesh::getFormattedReactionRates(RxType rx) {

  /* Extract reaction rates */
  Vector3D rx_rates;
  std::vector<double> rx_rates_array = getReactionRates(rx);

  /* Format reaction rates into a 3D array */
  int num_x = _lattice->getNumX();
  for (int i=0; i < num_x; i++) {
    int num_y = _lattice->getNumY();
    std::vector<std::vector<double> > vector_2D;
    for (int j=0; j < num_y; j++) {
      int num_z = _lattice->getNumZ();
      std::vector<double> vector_1D;
      for (int k=0; k < num_z; k++) {
        int idx = k * num_x * num_y + j * num_x + i;
        vector_1D.push_back(rx_rates_array[idx]);
      }
      vector_2D.push_back(vector_1D);
    }
    rx_rates.push_back(vector_2D);
  }
  return rx_rates;
}

Vector3D Mesh::getFormattedReactionRates(std::vector<std::vector<double>> widths_offsets, RxType rx) {
  Vector3D rx_rates;
  
  /* Get the root universe */
  Geometry* geometry = _solver->getGeometry();
  Universe* root_universe = geometry->getRootUniverse();

  /* Determine the center-point of the geometry */
  double offset_x = (root_universe->getMinX() + root_universe->getMaxX()) / 2;
  double offset_y = (root_universe->getMinY() + root_universe->getMaxY()) / 2;
  double offset_z = (root_universe->getMinZ() + root_universe->getMaxZ()) / 2;  
  
  
  Lattice real_lattice;
  real_lattice.setNumX(widths_offsets[0].size());
  real_lattice.setNumY(widths_offsets[1].size());
  real_lattice.setNumZ(widths_offsets[2].size());
  real_lattice.setWidths(widths_offsets[0], widths_offsets[1], widths_offsets[2]);
  if(widths_offsets.size() == 3) 
    real_lattice.setOffset(offset_x, offset_y, offset_z);
  else
    real_lattice.setOffset(widths_offsets[3][0], widths_offsets[3][1], widths_offsets[3][2]);
  real_lattice.computeSizes();
  
  Lattice wrap_lattice;
  std::vector<double> widths_x = widths_offsets[0];
  std::vector<double> widths_y = widths_offsets[1];
  std::vector<double> widths_z = widths_offsets[2];
  
  std::vector<bool> surface(6, false);
  if(fabs(real_lattice.getMinX() - root_universe->getMinX()) > FLT_EPSILON) {
    widths_x.insert(widths_x.begin(),fabs(real_lattice.getMinX() - root_universe->getMinX()));
    surface[0]=true;
  }
  if(fabs(real_lattice.getMinY() - root_universe->getMinY()) > FLT_EPSILON)  {
    widths_y.insert(widths_y.begin(),fabs(real_lattice.getMinY() - root_universe->getMinY()));
    surface[1]=true;
  }
  if(fabs(real_lattice.getMinZ() - root_universe->getMinZ()) > FLT_EPSILON)  {
    widths_z.insert(widths_z.begin(),fabs(real_lattice.getMinZ() - root_universe->getMinZ()));
    surface[2]=true;
  }
  if(fabs(real_lattice.getMaxX() - root_universe->getMaxX()) > FLT_EPSILON)  {
    widths_x.push_back(fabs(real_lattice.getMaxX() - root_universe->getMaxX()));
    surface[3]=true;
  }
  if(fabs(real_lattice.getMaxY() - root_universe->getMaxY()) > FLT_EPSILON)  {
    widths_y.push_back(fabs(real_lattice.getMaxY() - root_universe->getMaxY()));
    surface[4]=true;
  }
  if(fabs(real_lattice.getMaxZ() - root_universe->getMaxZ()) > FLT_EPSILON)  {
    widths_z.push_back(fabs(real_lattice.getMaxZ() - root_universe->getMaxZ()));  
    surface[5]=true;
  }
  
  wrap_lattice.setNumX(widths_x.size());
  wrap_lattice.setNumY(widths_y.size());
  wrap_lattice.setNumZ(widths_z.size());
  wrap_lattice.setWidths(widths_x, widths_y, widths_z);
  wrap_lattice.setOffset(offset_x, offset_y, offset_z);                  
  wrap_lattice.computeSizes();
  
  setLattice(&wrap_lattice);
  
  rx_rates = getFormattedReactionRates(rx);
  
  if(surface[0]) rx_rates.erase(rx_rates.begin());
  if(surface[3]) rx_rates.pop_back();
  
  if(surface[1]) 
    for(int i=0; i<rx_rates.size(); i++)
      rx_rates[i].erase(rx_rates[i].begin());
  if(surface[4]) 
    for(int i=0; i<rx_rates.size(); i++)
      rx_rates[i].pop_back();
  
  if(surface[2]) 
    for(int i=0; i<rx_rates.size(); i++)
      for(int j=0; j<rx_rates[i].size(); j++)
      rx_rates[i][j].erase(rx_rates[i][j].begin());
  if(surface[5]) 
    for(int i=0; i<rx_rates.size(); i++)
      for(int j=0; j<rx_rates[i].size(); j++)
        rx_rates[i][j].pop_back();
  
  return rx_rates;
}




#endif
