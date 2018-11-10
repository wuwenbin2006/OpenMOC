#include "CPUSolver.h"
#include <unordered_map>

/**
 * @brief Constructor initializes array pointers for Tracks and Materials.
 * @details The constructor retrieves the number of energy groups and FSRs
 *          and azimuthal angles from the Geometry and TrackGenerator if
 *          passed in as parameters by the user. The constructor initalizes
 *          the number of OpenMP threads to a default of 1.
 * @param track_generator an optional pointer to the TrackGenerator
 */
CPUSolver::CPUSolver(TrackGenerator* track_generator)
  : Solver(track_generator) {

  setNumThreads(1);
  _FSR_locks = NULL;
  _source_type = "Flat";
#ifdef MPIx
  _track_message_size = 0;
  _MPI_requests = NULL;
  _MPI_sends = NULL;
  _MPI_receives = NULL;
#endif
}


/**
 * @brief Destructor deletes array for OpenMP mutual exclusion locks for
 *        FSR scalar flux updates, and calls Solver parent class destructor
 *        to deletes arrays for fluxes and sources.
 */
CPUSolver::~CPUSolver() {
#ifdef MPIx
  deleteMPIBuffers();
#endif
}


/**
 * @brief Returns the number of shared memory OpenMP threads in use.
 * @return the number of threads
 */
int CPUSolver::getNumThreads() {
  return _num_threads;
}


/**
 * @brief Fills an array with the scalar fluxes.
 * @details This class method is a helper routine called by the OpenMOC
 *          Python "openmoc.krylov" module for Krylov subspace methods.
 *          Although this method appears to require two arguments, in
 *          reality it only requires one due to SWIG and would be called
 *          from within Python as follows:
 *
 * @code
 *          num_fluxes = num_groups * num_FSRs
 *          fluxes = solver.getFluxes(num_fluxes)
 * @endcode
 *
 * @param fluxes an array of FSR scalar fluxes in each energy group
 * @param num_fluxes the total number of FSR flux values
 */
void CPUSolver::getFluxes(FP_PRECISION* out_fluxes, int num_fluxes) {

  if (num_fluxes != _num_groups * _geometry->getNumTotalFSRs())
    log_printf(ERROR, "Unable to get FSR scalar fluxes since there are "
               "%d groups and %d FSRs which does not match the requested "
               "%d flux values", _num_groups, _geometry->getNumTotalFSRs(),
               num_fluxes);

  else if (_scalar_flux == NULL)
    log_printf(ERROR, "Unable to get FSR scalar fluxes since they "
               "have not yet been allocated");

  /* Copy the fluxes into the input array */
  else {
#pragma omp parallel for schedule(guided)
    for (long r=0; r < _num_FSRs; r++) {
      for (int e=0; e < _num_groups; e++)
        out_fluxes[r*_num_groups+e] = _scalar_flux(r,e);
    }
  }
  /* Reduce domain data for domain decomposition */
#ifdef MPIx
  if (_geometry->isDomainDecomposed()) {

    /* Allocate buffer for communication */
    long num_total_FSRs = _geometry->getNumTotalFSRs();
    FP_PRECISION* temp_fluxes = new FP_PRECISION[num_total_FSRs*_num_groups];

    int rank = 0;
    MPI_Comm comm = _geometry->getMPICart();
    MPI_Comm_rank(comm, &rank);
    for (long r=0; r < num_total_FSRs; r++) {

      /* Determine the domain and local FSR ID */
      long fsr_id = r;
      int domain = 0;
      _geometry->getLocalFSRId(r, fsr_id, domain);

      /* Set data if in the correct domain */
      if (domain == rank)
        for (int e=0; e < _num_groups; e++)
          temp_fluxes[r*_num_groups+e] = out_fluxes[fsr_id*_num_groups+e];
      else
        for (int e=0; e < _num_groups; e++)
          temp_fluxes[r*_num_groups+e] = 0.0;
    }

    /* Determine the type of FP_PRECISION and communicate fluxes */
    MPI_Datatype flux_type;
    if (sizeof(FP_PRECISION) == 4)
      flux_type = MPI_FLOAT;
    else
      flux_type = MPI_DOUBLE;

    MPI_Allreduce(temp_fluxes, out_fluxes, num_total_FSRs*_num_groups,
                  flux_type, MPI_SUM, comm);
    delete [] temp_fluxes;
  }
#endif
}



/**
 * @brief Sets the number of shared memory OpenMP threads to use (>0).
 * @param num_threads the number of threads
 */
void CPUSolver::setNumThreads(int num_threads) {
  if (num_threads <= 0)
    log_printf(ERROR, "Unable to set the number of threads to %d "
               "since it is less than or equal to 0", num_threads);

#ifdef MPIx
  /* Check the MPI library has enough thread support */
  int provided;
  MPI_Query_thread(&provided);
  if (num_threads > 1 && provided < MPI_THREAD_SERIALIZED)
    log_printf(WARNING, "Not enough thread support level in the MPI library, "
               "re-compile with another library. Thread support level should"
               "be at least MPI_THREAD_SERIALIZED.");
#endif

  if (_track_generator != NULL)
    if ((_track_generator->getSegmentFormation() == OTF_STACKS ||
       _track_generator->getSegmentFormation() == OTF_TRACKS) &&
       _track_generator->getNumThreads() != num_threads)
      log_printf(WARNING, "The number of threads used in track generation "
               "should match the number of threads used in the solver for OTF "
               "ray-tracing methods, as threaded buffers are shared.");

  /* Set the number of threads for OpenMP */
  _num_threads = num_threads;
  omp_set_num_threads(_num_threads);
}


/**
 * @brief Assign a fixed source for a flat source region and energy group.
 * @details Fixed sources should be scaled to reflect the fact that OpenMOC
 *          normalizes the scalar flux such that the total energy- and
 *          volume-integrated production rate sums to 1.0.
 * @param fsr_id the flat source region ID
 * @param group the energy group
 * @param source the volume-averaged source in this group
 */
void CPUSolver::setFixedSourceByFSR(long fsr_id, int group,
                                    FP_PRECISION source) {

  Solver::setFixedSourceByFSR(fsr_id, group, source);

  /* Allocate the fixed sources array if not yet allocated */
  if (_fixed_sources == NULL) {
    long size = _num_FSRs * _num_groups;
    _fixed_sources = new FP_PRECISION[size];
    memset(_fixed_sources, 0.0, sizeof(FP_PRECISION) * size);
  }

  /* Warn the user if a fixed source has already been assigned to this FSR */
  if (fabs(_fixed_sources(fsr_id,group-1)) > FLT_EPSILON)
    log_printf(WARNING, "Overriding fixed source %f in FSR ID=%d with %f",
               _fixed_sources(fsr_id,group-1), fsr_id, source);

  /* Store the fixed source for this FSR and energy group */
  _fixed_sources(fsr_id,group-1) = source;
}


/**
 * @brief Initializes the FSR volumes and Materials array.
 * @details This method allocates and initializes an array of OpenMP
 *          mutual exclusion locks for each FSR for use in the
 *          transport sweep algorithm.
 */
void CPUSolver::initializeFSRs() {

  Solver::initializeFSRs();

  /* Get FSR locks from TrackGenerator */
  _FSR_locks = _track_generator->getFSRLocks();
}


/**
 * @brief Allocates memory for Track boundary angular flux and leakage
 *        and FSR scalar flux arrays.
 * @details Deletes memory for old flux arrays if they were allocated
 *          for a previous simulation.
 */
void CPUSolver::initializeFluxArrays() {

  /* Delete old flux arrays if they exist */
  if (_boundary_flux != NULL)
    delete [] _boundary_flux;

  if (_start_flux != NULL)
    delete [] _start_flux;

  if (_boundary_leakage != NULL)
    delete [] _boundary_leakage;

  if (_scalar_flux != NULL)
    delete [] _scalar_flux;

  if (_old_scalar_flux != NULL)
    delete [] _old_scalar_flux;
  
  if (_stabilizing_flux != NULL)
    delete [] _stabilizing_flux;

#ifdef MPIx
  if (_geometry->isDomainDecomposed())
    deleteMPIBuffers();
#endif

  long size;

  /* Allocate memory for the Track boundary fluxes and leakage arrays */
  try {
    size = 2 * _tot_num_tracks * _fluxes_per_track;
    long max_size = size;
#ifdef MPIX
    if (_geometry->isDomainDecomposed())
      MPI_Allreduce(&size, &max_size, 1, MPI_LONG, MPI_MAX,
                    _geometry->getMPICart());
#endif
    double max_size_mb = (double) (2 * max_size * sizeof(float))
        / (double) (1e6);
    log_printf(NORMAL, "Max boundary angular flux storage per domain = %6.2f "
               "MB", max_size_mb);

    _boundary_flux = new float[size]();
    _start_flux = new float[size]();

    /* Allocate memory for boundary leakage if necessary. CMFD is not set in
       solver at this point, so the value of _cmfd is always NULL as initial
       value currently */
    if (_geometry->getCmfd() == NULL) {
      _boundary_leakage = new float[_tot_num_tracks];
      memset(_boundary_leakage, 0., _tot_num_tracks * sizeof(float));
    }

    /* Determine the size of arrays for the FSR scalar fluxes */
    size = _num_FSRs * _num_groups;
    max_size = size;
#ifdef MPIX
    if (_geometry->isDomainDecomposed())
      MPI_Allreduce(&size, &max_size, 1, MPI_LONG, MPI_MAX,
                    _geometry->getMPICart());
#endif

    /* Determine the amount of memory allocated */
    int num_flux_arrays = 2;
    if (_stabilize_transport)
      num_flux_arrays++;

    max_size_mb = (double) (num_flux_arrays * max_size * sizeof(FP_PRECISION))
        / (double) (1e6);
    log_printf(NORMAL, "Max scalar flux storage per domain = %6.2f MB",
               max_size_mb);

    /* Allocate scalar fluxes */
    _scalar_flux = new FP_PRECISION[size];
    _old_scalar_flux = new FP_PRECISION[size];
    memset(_scalar_flux, 0., size * sizeof(FP_PRECISION));
    memset(_old_scalar_flux, 0., size * sizeof(FP_PRECISION));

    /* Allocate stabilizing flux vector if necessary */
    if (_stabilize_transport) {
      _stabilizing_flux = new FP_PRECISION[size];
      memset(_stabilizing_flux, 0., size * sizeof(FP_PRECISION));
    }

#ifdef MPIx
    /* Allocate memory for angular flux exchanging buffers */
    if (_geometry->isDomainDecomposed())
      setupMPIBuffers();
#endif
  }

  catch (std::exception &e) {
    log_printf(ERROR, "Could not allocate memory for the fluxes");
  }
}


/**
 * @brief Allocates memory for FSR source arrays.
 * @details Deletes memory for old source arrays if they were allocated for a
 *          previous simulation.
 */
void CPUSolver::initializeSourceArrays() {

  /* Delete old sources arrays if they exist */
  if (_reduced_sources != NULL)
    delete [] _reduced_sources;
  if (_fixed_sources != NULL)
    delete [] _fixed_sources;

  long size = _num_FSRs * _num_groups;

  /* Allocate memory for all source arrays */
  _reduced_sources = new FP_PRECISION[size];
  _fixed_sources = new FP_PRECISION[size];

  long max_size = size;
#ifdef MPIX
  if (_geometry->isDomainDecomposed())
    MPI_Allreduce(&size, &max_size, 1, MPI_LONG, MPI_MAX,
                  _geometry->getMPICart());
#endif
  double max_size_mb = (double) (2 * max_size * sizeof(FP_PRECISION))
        / (double) (1e6);
  log_printf(NORMAL, "Max source storage per domain = %6.2f MB",
             max_size_mb);

  /* Initialize fixed sources to zero */
  memset(_fixed_sources, 0.0, sizeof(FP_PRECISION) * size);

  /* Populate fixed source array with any user-defined sources */
  initializeFixedSources();
}


/**
 * @brief Populates array of fixed sources assigned by FSR.
 */
void CPUSolver::initializeFixedSources() {

  Solver::initializeFixedSources();

  long fsr_id;
  int group;
  std::pair<int, int> fsr_group_key;
  std::map< std::pair<int, int>, FP_PRECISION >::iterator fsr_iter;

  /* Populate fixed source array with any user-defined sources */
  for (fsr_iter = _fix_src_FSR_map.begin();
       fsr_iter != _fix_src_FSR_map.end(); ++fsr_iter) {

    /* Get the FSR with an assigned fixed source */
    fsr_group_key = fsr_iter->first;
    fsr_id = fsr_group_key.first;
    group = fsr_group_key.second;

    if (group <= 0 || group > _num_groups)
      log_printf(ERROR,"Unable to use fixed source for group %d in "
                 "a %d energy group problem", group, _num_groups);

    if (fsr_id < 0 || fsr_id >= _num_FSRs)
      log_printf(ERROR,"Unable to use fixed source for FSR %d with only "
                 "%d FSRs in the geometry", fsr_id, _num_FSRs);

    _fixed_sources(fsr_id, group-1) = _fix_src_FSR_map[fsr_group_key];
  }
}


/**
 * @brief Zero each Track's boundary fluxes for each energy group
 *        and polar angle in the "forward" and "reverse" directions.
 */
void CPUSolver::zeroTrackFluxes() {

#pragma omp parallel for schedule(guided)
  for (long t=0; t < _tot_num_tracks; t++) {
    for (int d=0; d < 2; d++) {
      for (int pe=0; pe < _fluxes_per_track; pe++) {
        _boundary_flux(t, d, pe) = 0.0;
        _start_flux(t, d, pe) = 0.0;
      }
    }
  }
}


/**
 * @brief Copies values from the start flux into the boundary flux array
 *        for both the "forward" and "reverse" directions.
 */
void CPUSolver::copyBoundaryFluxes() {
  memcpy(_boundary_flux, _start_flux, 
         sizeof(float)*2*_tot_num_tracks*_fluxes_per_track);

}


/**
 * @brief Computes the total current impingent on boundary CMFD cells from
 *        starting angular fluxes
 */
//FIXME: make suitable for 2D
void CPUSolver::tallyStartingCurrents() {

#pragma omp parallel for schedule(guided)
  for (long t=0; t < _tot_num_tracks; t++) {

    /* Get 3D Track data */
    TrackGenerator3D* track_generator_3D =
        dynamic_cast<TrackGenerator3D*>(_track_generator);
    if (track_generator_3D != NULL) {
      TrackStackIndexes tsi;
      Track3D track;
      track_generator_3D->getTSIByIndex(t, &tsi);
      track_generator_3D->getTrackOTF(&track, &tsi);

      /* Determine the first and last CMFD cells of each track */
      double azim = track.getPhi();
      double polar = track.getTheta();
      double delta_x = cos(azim) * sin(polar) * TINY_MOVE;
      double delta_y = sin(azim) * sin(polar) * TINY_MOVE;
      double delta_z = cos(polar) * TINY_MOVE;
      Point* start = track.getStart();
      Point* end = track.getEnd();

      /* Get the track weight */
      int azim_index = track.getAzimIndex();
      int polar_index = track.getPolarIndex();
      double weight = _quad->getWeightInline(azim_index, polar_index);

      /* Tally currents */
      _cmfd->tallyStartingCurrent(start, delta_x, delta_y, delta_z,
                                  &_start_flux(t, 0, 0), weight);
      _cmfd->tallyStartingCurrent(end, -delta_x, -delta_y, -delta_z,
                                  &_start_flux(t, 1, 0), weight);
    }
    else {
      log_printf(ERROR, "Starting currents not implemented yet for 2D MOC");
    }
  }
}


#ifdef MPIx
/**
 * @brief Buffers used to transfer angular flux information are initialized
 * @details Track connection book-keeping information is also saved for
 *          efficiency during angular flux packing.
 */
void CPUSolver::setupMPIBuffers() {

  /* Determine the size of the buffers */
  _track_message_size = _fluxes_per_track + 3;
  int length = TRACKS_PER_BUFFER * _track_message_size;

  /* Initialize MPI requests and status */
  if (_geometry->isDomainDecomposed()) {

    if (_send_buffers.size() > 0)
      deleteMPIBuffers();

    log_printf(NORMAL, "Setting up MPI Buffers for angular flux exchange...");

    /* Fill the hash map of send buffers */
    std::unordered_map<int, int> neighbor_connections;
    int idx = 0;
    for (int dx=-1; dx <= 1; dx++) {
      for (int dy=-1; dy <= 1; dy++) {
        for (int dz=-1; dz <= 1; dz++) {
          if (abs(dx) + abs(dy) == 1 ||
              (dx == 0 && dy == 0 && dz != 0)) {
            int domain = _geometry->getNeighborDomain(dx, dy, dz);
            if (domain != -1) {
              neighbor_connections.insert({domain, idx});
              float* send_buffer = new float[length];
              _send_buffers.push_back(send_buffer);
              float* receive_buffer = new float[length];
              _receive_buffers.push_back(receive_buffer);
              _neighbor_domains.push_back(domain);
              idx++;
            }
          }
        }
      }
    }

    /* Setup Track communication information for all neighbor domains */
    int num_domains = _neighbor_domains.size();
    _boundary_tracks.resize(num_domains);
    for (int i=0; i < num_domains; i++) {

      /* Initialize Track ID's to -1 */
      int start_idx = _fluxes_per_track + 1;
      for (int idx = start_idx; idx < length; idx += _track_message_size) {
        long* track_info_location =
             reinterpret_cast<long*>(&_send_buffers.at(i)[idx]);
        track_info_location[0] = -1;
        track_info_location =
             reinterpret_cast<long*>(&_receive_buffers.at(i)[idx]);
        track_info_location[0] = -1;
      }
    }

    /* Build array of Track connections */
    _track_connections.resize(2);
    _track_connections.at(0).resize(_tot_num_tracks);
    _track_connections.at(1).resize(_tot_num_tracks);

    /* Determine how many Tracks communicate with each neighbor domain */
    log_printf(NORMAL, "Initializing Track connections accross domains...");
    long num_tracks[num_domains];
    for (int i=0; i < num_domains; i++)
      num_tracks[i] = 0;
#pragma omp parallel for
    for (long t=0; t<_tot_num_tracks; t++) {

      /* Get 3D Track data */
      TrackStackIndexes tsi;
      Track3D track;
      TrackGenerator3D* track_generator_3D =
        dynamic_cast<TrackGenerator3D*>(_track_generator);
      track_generator_3D->getTSIByIndex(t, &tsi);
      track_generator_3D->getTrackOTF(&track, &tsi);

      /* Save the index of the forward and backward connecting Tracks */
      _track_connections.at(0).at(t) = track.getTrackNextFwd();
      _track_connections.at(1).at(t) = track.getTrackNextBwd();

      /* Determine the indexes of connecting domains */
      int domains[2];
      domains[0] = track.getDomainFwd();
      domains[1] = track.getDomainBwd();
      bool interface[2];
      interface[0] = track.getBCFwd() == INTERFACE;
      interface[1] = track.getBCBwd() == INTERFACE;
      for (int d=0; d < 2; d++) {
        if (domains[d] != -1 && interface[d]) {
          int neighbor = neighbor_connections.at(domains[d]);
#pragma omp atomic
          num_tracks[neighbor]++;
        }
      }
    }

    /* Resize the buffers for the counted number of Tracks */
    for (int i=0; i < num_domains; i++) {
      _boundary_tracks.at(i).resize(num_tracks[i]);
      num_tracks[i] = 0;
    }

    /* Determine which Tracks communicate with each neighbor domain */
#pragma omp parallel for
    for (long t=0; t<_tot_num_tracks; t++) {

      /* Get 3D Track data */
      TrackStackIndexes tsi;
      Track3D track;
      TrackGenerator3D* track_generator_3D =
        dynamic_cast<TrackGenerator3D*>(_track_generator);
      track_generator_3D->getTSIByIndex(t, &tsi);
      track_generator_3D->getTrackOTF(&track, &tsi);

      /* Determine the indexes of connecting domains */
      int domains[2];
      domains[0] = track.getDomainFwd();
      domains[1] = track.getDomainBwd();
      bool interface[2];
      interface[0] = track.getBCFwd() == INTERFACE;
      interface[1] = track.getBCBwd() == INTERFACE;
      for (int d=0; d < 2; d++) {
        if (domains[d] != -1 && interface[d]) {
          int neighbor = neighbor_connections.at(domains[d]);

          long slot;
#pragma omp critical
          {
            slot = num_tracks[neighbor];
            num_tracks[neighbor]++;
          }

          _boundary_tracks.at(neighbor).at(slot) = 2*t + d;
        }
      }
    }

    log_printf(NORMAL, "Finished setting up MPI buffers...");

    /* Setup MPI communication bookkeeping */
    _MPI_requests = new MPI_Request[2*num_domains];
    _MPI_sends = new bool[num_domains];
    _MPI_receives = new bool[num_domains];
    for (int i=0; i < num_domains; i++) {
      _MPI_sends[i] = false;
      _MPI_receives[i] = false;
    }
  }
}


/**
 * @brief The arrays used to store angular flux information are deleted along
 *        with book-keeping information for track connections.
 */
void CPUSolver::deleteMPIBuffers() {
  for (int i=0; i < _send_buffers.size(); i++) {
    delete [] _send_buffers.at(i);
  }
  _send_buffers.clear();

  for (int i=0; i < _send_buffers.size(); i++) {
    delete [] _send_buffers.at(i);
  }
  _receive_buffers.clear();
  _neighbor_domains.clear();

  for (int i=0; i < _boundary_tracks.size(); i++)
    _boundary_tracks.at(i).clear();
  _boundary_tracks.clear();

  delete [] _MPI_requests;
  delete [] _MPI_sends;
  delete [] _MPI_receives;
}


/**
 * @brief Prints out tracking information for cycles, traversing domain
 *        interfaces
 * @details This function prints Track starting and ending points for a cycle
 *          that traverses the entire Geometry.
 * @param track_start The starting Track ID from which the cycle is followed
 * @param domain_start The domain for the starting Track
 * @param length The number of Tracks to follow across the cycle
 */
void CPUSolver::printCycle(long track_start, int domain_start, int length) {

  /* Initialize buffer for MPI communication */
  int message_size = sizeof(sendInfo);

  /* Initialize MPI requests and status */
  MPI_Comm MPI_cart = _geometry->getMPICart();
  int num_ranks;
  MPI_Comm_size(MPI_cart, &num_ranks);
  MPI_Status stat;
  MPI_Request request[num_ranks];

  int rank;
  MPI_Comm_rank(MPI_cart, &rank);

  /* Loop over all tracks and exchange fluxes */
  long curr_track = track_start;
  int curr_rank = domain_start;
  bool fwd = true;
  for (int t=0; t < length; t++) {

    /* Check if this rank is sending the Track */
    if (rank == curr_rank) {

      /* Get 3D Track data */
      TrackStackIndexes tsi;
      Track3D track;
      TrackGenerator3D* track_generator_3D =
        dynamic_cast<TrackGenerator3D*>(_track_generator);
      track_generator_3D->getTSIByIndex(curr_track, &tsi);
      track_generator_3D->getTrackOTF(&track, &tsi);

      /* Get connecting tracks */
      long connect;
      bool connect_fwd;
      Point* start;
      Point* end;
      int next_domain;
      if (fwd) {
        connect = track.getTrackPrdcFwd();
        connect_fwd = track.getNextFwdFwd();
        start = track.getStart();
        end = track.getEnd();
        next_domain = track.getDomainFwd();
      }
      else {
        connect = track.getTrackPrdcBwd();
        connect_fwd = track.getNextBwdFwd();
        start = track.getEnd();
        end = track.getStart();
        next_domain = track.getDomainBwd();
      }

      /* Write information */
      log_printf(NODAL, "Rank %d: Track (%f, %f, %f) -> (%f, %f, %f)", rank,
                 start->getX(), start->getY(), start->getZ(), end->getX(),
                 end->getY(), end->getZ());

      /* Check domain for reflected boundaries */
      if (next_domain == -1) {
        next_domain = curr_rank;
        if (fwd)
          connect = track.getTrackNextFwd();
        else
          connect = track.getTrackNextBwd();
      }

      /* Pack the information */
      sendInfo si;
      si.track_id = connect;
      si.domain = next_domain;
      si.fwd = connect_fwd;

      /* Send the information */
      for (int i=0; i < num_ranks; i++)
        if (i != rank)
          MPI_Isend(&si, message_size, MPI_BYTE, i, 0, MPI_cart, &request[i]);

      /* Copy information */
      curr_rank = next_domain;
      fwd = connect_fwd;
      curr_track = connect;

      /* Wait for sends to complete */
      bool complete = false;
      while (!complete) {
        complete = true;
        for (int i=0; i < num_ranks; i++) {
          if (i != rank) {
            int flag;
            MPI_Test(&request[i], &flag, &stat);
            if (flag == 0)
              complete = false;
          }
        }
      }
    }

    /* Receiving info */
    else {

      /* Create object to receive sent information */
      sendInfo si;

      /* Issue the receive from the current node */
      MPI_Irecv(&si, message_size, MPI_BYTE, curr_rank, 0, MPI_cart,
                &request[0]);

      /* Wait for receive to complete */
      bool complete = false;
      while (!complete) {
        complete = true;
        int flag;
        MPI_Test(&request[0], &flag, &stat);
        if (flag == 0)
          complete = false;
      }

      /* Copy information */
      curr_rank = si.domain;
      fwd = si.fwd;
      curr_track = si.track_id;
    }

    MPI_Barrier(MPI_cart);
  }

  /* Join MPI at the end of communication */
  MPI_Barrier(MPI_cart);
}


/**
 * @brief Angular flux transfer information is packed into buffers.
 * @details On each domain, angular flux and track connection information
 *          is packed into buffers. Each buffer pertains to a neighboring
 *          domain. This function proceeds packing buffers until for each
 *          neighboring domain either all the tracks have been packed or the
 *          associated buffer is full. This provided integer array contains
 *          the index of the last track handled for each neighboring domain.
 *          These numbers are updated at the end with the last track handled.
 */
void CPUSolver::packBuffers(std::vector<long> &packing_indexes) {

  /* Fill send buffers for every domain */
  int num_domains = packing_indexes.size();
  for (int i=0; i < num_domains; i++) {
    int send_domain = _neighbor_domains.at(i);

    /* Reset send buffers */
    int start_idx = _fluxes_per_track + 1;
    int max_idx = _track_message_size * TRACKS_PER_BUFFER;
#pragma omp parallel for
    for (int idx = start_idx; idx < max_idx; idx += _track_message_size) {
      long* track_info_location =
        reinterpret_cast<long*>(&_send_buffers.at(i)[idx]);
      track_info_location[0] = -1;
    }

    /* Fill send buffers with Track information */
    int max_buffer_idx = _boundary_tracks.at(i).size() -
          packing_indexes.at(i);
    if (max_buffer_idx > TRACKS_PER_BUFFER)
      max_buffer_idx = TRACKS_PER_BUFFER;
#pragma omp parallel for
    for (int b=0; b < max_buffer_idx; b++) {

      long boundary_track_idx = packing_indexes.at(i) + b;
      long buffer_index = b * _track_message_size;

      /* Get 3D Track data */
      long boundary_track = _boundary_tracks.at(i).at(boundary_track_idx);
      long t = boundary_track / 2;
      int d = boundary_track - 2*t;
      int connect_track = _track_connections.at(d).at(t);

      /* Fill buffer with angular fluxes */
      for (int pe=0; pe < _fluxes_per_track; pe++)
        _send_buffers.at(i)[buffer_index + pe] = _boundary_flux(t,d,pe);

      /* Assign the connecting Track information */
      int idx = buffer_index + _fluxes_per_track;
      _send_buffers.at(i)[idx] = d;
      long* track_info_location =
        reinterpret_cast<long*>(&_send_buffers.at(i)[idx+1]);
      track_info_location[0] = connect_track;
    }

    /* Record the next Track ID */
    packing_indexes.at(i) += max_buffer_idx;
  }
}


/**
 * @brief Transfers all angular fluxes at interfaces to their appropriate
 *        domain neighbors
 * @details The angular fluxes stored in the _boundary_flux array that
 *          intersect INTERFACE boundaries are transfered to their appropriate
 *          neighbor's _start_flux array at the periodic indexes.
 */
void CPUSolver::transferAllInterfaceFluxes() {

  /* Initialize MPI requests and status */
  MPI_Comm MPI_cart = _geometry->getMPICart();
  MPI_Status stat;

  /* Wait for all MPI Ranks to be done with communication */
  _timer->startTimer();
  MPI_Barrier(MPI_cart);
  _timer->stopTimer();
  _timer->recordSplit("Idle time");

  /* Initialize timer for total function cost */
  _timer->startTimer();

  /* Create bookkeeping vectors */
  std::vector<long> packing_indexes;

  /* Resize vectors to the number of domains */
  int num_domains = _neighbor_domains.size();
  packing_indexes.resize(num_domains);

  /* Start communication rounds */
  while (true) {

    int rank;
    MPI_Comm_rank(MPI_cart, &rank);

    /* Pack buffers with angular flux data */
    _timer->startTimer();
    packBuffers(packing_indexes);
    _timer->stopTimer();
    _timer->recordSplit("Packing time");

    /* Send and receive from all neighboring domains */
    _timer->startTimer();
    bool communication_complete = true;
    for (int i=0; i < num_domains; i++) {

      /* Get the communicating neighbor domain */
      int domain = _neighbor_domains.at(i);

      /* Check if a send/receive needs to be created */
      long* first_track_idx =
        reinterpret_cast<long*>(&_send_buffers.at(i)[_fluxes_per_track+1]);
      long first_track = first_track_idx[0];
      if (first_track != -1) {

        /* Send outgoing flux */
        MPI_Isend(_send_buffers.at(i), _track_message_size *
                  TRACKS_PER_BUFFER, MPI_FLOAT, domain, 0, MPI_cart,
                  &_MPI_requests[i*2]);
        _MPI_sends[i] = true;

        /* Receive incoming flux */
        MPI_Irecv(_receive_buffers.at(i), _track_message_size *
                  TRACKS_PER_BUFFER, MPI_FLOAT, domain, 0, MPI_cart,
                  &_MPI_requests[i*2+1]);
        _MPI_receives[i] = true;

        /* Mark communication as ongoing */
        communication_complete = false;
      }
    }

    /* Check if communication is done */
    if (communication_complete) {
      _timer->stopTimer();
      _timer->recordSplit("Communication time");
      break;
    }

    /* Block for communication round to complete */
    bool round_complete = false;
    while (!round_complete) {

      round_complete = true;
      int flag;

      /* Check forward and backward send/receive messages */
      for (int i=0; i < num_domains; i++) {

        /* Wait for send to complete */
        if (_MPI_sends[i] == true) {
          MPI_Test(&_MPI_requests[i*2], &flag, &stat);
          if (flag == 0)
            round_complete = false;
        }

        /* Wait for receive to complete */
        if (_MPI_receives[i] == true) {
          MPI_Test(&_MPI_requests[i*2+1], &flag, &stat);
          if (flag == 0)
            round_complete = false;
        }
      }
    }

    /* Reset status for next communication round and copy fluxes */
    for (int i=0; i < num_domains; i++) {

      /* Reset send */
      _MPI_sends[i] = false;

      /* Copy angular fluxes if necessary */
      if (_MPI_receives[i]) {

        /* Get the buffer for the connecting domain */
        float* buffer = _receive_buffers.at(i);
        for (int t=0; t < TRACKS_PER_BUFFER; t++) {

          /* Get the Track ID */
          float* curr_track_buffer = &buffer[t*_track_message_size];
          long* track_idx =
            reinterpret_cast<long*>(&curr_track_buffer[_fluxes_per_track+1]);
          long track_id = track_idx[0];

          /* Check if the angular fluxes are active */
          if (track_id != -1) {
            int dir = curr_track_buffer[_fluxes_per_track];

            for (int pe=0; pe < _fluxes_per_track; pe++)
              _start_flux(track_id, dir, pe) = curr_track_buffer[pe];
          }
        }
      }

      /* Reset receive */
      _MPI_receives[i] = false;
    }
    _timer->stopTimer();
    _timer->recordSplit("Communication time");
  }

  /* Join MPI at the end of communication */
  MPI_Barrier(MPI_cart);
  _timer->stopTimer();
  _timer->recordSplit("Total transfer time");
}


/**
 * @brief A debugging tool used to check track links across domains
 * @details Domains are traversed in rank order. For each domain, all tracks
 *          are traversed and for each track, information is requested about
 *          the connecting track - specifically its angles, and its location.
 *          When the information is returned, it is checked by the requesting
 *          domain for consistency.
 */
void CPUSolver::boundaryFluxChecker() {

  /* Get MPI information */
  MPI_Comm MPI_cart = _geometry->getMPICart();
  MPI_Request req;
  MPI_Status stat;
  int my_rank;
  MPI_Comm_rank(MPI_cart, &my_rank);
  int num_ranks;
  MPI_Comm_size(MPI_cart, &num_ranks);

  /* Loop over all domains for requesting information */
  int tester = 0;
  int new_tester = 0;
  while (tester < num_ranks) {

    if (tester == my_rank) {

      /* Loop over all tracks */
      for (long t=0; t < _tot_num_tracks; t++) {

        /* Get the Track */
        TrackStackIndexes tsi;
        Track3D track;
        TrackGenerator3D* track_generator_3D =
          dynamic_cast<TrackGenerator3D*>(_track_generator);
        track_generator_3D->getTSIByIndex(t, &tsi);
        track_generator_3D->getTrackOTF(&track, &tsi);

        /* Check connection information on both directions */
        for (int dir=0; dir < 2; dir++) {

          boundaryType bc;
          if (dir == 0)
            bc = track.getBCFwd();
          else
            bc = track.getBCBwd();

          /* Check domain boundaries */
          if (bc == INTERFACE) {

            /* Get connection information */
            int dest;
            long connection[2];
            if (dir == 0) {
              dest = track.getDomainFwd();
              connection[0] = track.getTrackNextFwd();
            }
            else {
              dest = track.getDomainBwd();
              connection[0] = track.getTrackNextBwd();
            }
            connection[1] = dir;

            /* Check for a valid destination */
            if (dest == -1)
              log_printf(ERROR, "Track %d on domain %d has been found to have "
                         "a INTERFACE boundary but no connecting domain", t,
                         my_rank);

            /* Send a request for info */
            MPI_Send(connection, 2, MPI_LONG, dest, 0, MPI_cart);

            /* Receive infomation */
            int receive_size = _fluxes_per_track + 2 * 5;
            float buffer[receive_size];
            MPI_Recv(buffer, receive_size, MPI_FLOAT, dest, 0, MPI_cart, &stat);

            /* Unpack received infomrmation */
            float angular_fluxes[_fluxes_per_track];
            for (int i=0; i < _fluxes_per_track; i++)
              angular_fluxes[i] = buffer[i];

            double track_info[5];
            for (int i=0; i < 5; i++) {
              int idx = _fluxes_per_track + 2 * i;
              double* track_info_location =
                reinterpret_cast<double*>(&buffer[idx]);
              track_info[i] = track_info_location[0];
            }
            double x = track_info[0];
            double y = track_info[1];
            double z = track_info[2];
            double theta = track_info[3];
            double phi = track_info[4];

            /* Check received information */

            /* Get the connecting point */
            Point* point;
            if (dir == 0)
              point = track.getEnd();
            else
              point = track.getStart();

            /* Check position */
            if (fabs(point->getX() - x) > 1e-5 ||
                fabs(point->getY() - y) > 1e-5 ||
                fabs(point->getZ() - z) > 1e-5)
              log_printf(ERROR, "Track linking error: Track %d in domain %d "
                         "with connecting point (%f, %f, %f) does not connect "
                         "with \n Track %d in domain %d at point (%f, %f, %f)",
                         t, my_rank, point->getX(), point->getY(),
                         point->getZ(), connection[0], dest, x, y, z);

            /* Check double reflection */
            bool x_min = fabs(point->getX() - _geometry->getMinX()) < 1e-5;
            bool x_max = fabs(point->getX() - _geometry->getMaxX()) < 1e-5;
            bool x_bound = x_min || x_max;
            bool z_min = fabs(point->getZ() - _geometry->getMinZ()) < 1e-5;
            bool z_max = fabs(point->getZ() - _geometry->getMaxZ()) < 1e-5;
            bool z_bound = z_min || z_max;

            /* Forgive angle differences on double reflections */
            if (x_bound && z_bound) {
              phi = track.getPhi();
              theta = track.getTheta();
            }

            if (fabs(track.getPhi() - phi) > 1e-5 ||
                fabs(track.getTheta() - theta) > 1e-5)
              log_printf(ERROR, "Track linking error: Track %d in domain %d "
                         "with direction (%f, %f) does not match Track %d in "
                         " domain %d with direction (%f, %f)",
                         t, my_rank, track.getTheta(), track.getPhi(),
                         connection[0], dest, theta, phi);

            for (int pe=0; pe < _fluxes_per_track; pe++) {
              if (fabs(angular_fluxes[pe] - _boundary_flux(t, dir, pe))
                  > 1e-7) {
                std::string dir_string;
                if (dir == 0)
                  dir_string = "FWD";
                else
                  dir_string = "BWD";
                log_printf(ERROR, "Angular flux mismatch found on Track %d "
                           "in domain %d in %s direction at index %d. Boundary"
                           " angular flux at this location is %f but the "
                           "starting flux at connecting Track %d in domain %d "
                           "in the -- direction is %f", t, my_rank,
                           dir_string.c_str(), pe, _boundary_flux(t, dir, pe),
                           connection[0], dest, angular_fluxes[pe]);
                //FIXME Include track direction in track_info when debugging, 
                //      but not in production mode
              }
            }
          }

          /* Check on-node boundaries */
          else {

            /* Get the connecting Track */
            long connecting_idx;
            if (dir == 0)
              connecting_idx = track.getTrackNextFwd();
            else
              connecting_idx = track.getTrackNextBwd();

            TrackStackIndexes connecting_tsi;
            Track3D connecting_track;
            track_generator_3D->getTSIByIndex(connecting_idx, &connecting_tsi);
            track_generator_3D->getTrackOTF(&connecting_track,
                                            &connecting_tsi);

            /* Extract Track information */
            double x, y, z;
            bool connect_fwd;
            Point* point;
            if (dir == 0) {
              connect_fwd = track.getNextFwdFwd();
              point = track.getEnd();
            }
            else {
              connect_fwd = track.getNextBwdFwd();
              point = track.getStart();
            }
            if (connect_fwd) {
              x = connecting_track.getStart()->getX();
              y = connecting_track.getStart()->getY();
              z = connecting_track.getStart()->getZ();
            }
            else {
              x = connecting_track.getEnd()->getX();
              y = connecting_track.getEnd()->getY();
              z = connecting_track.getEnd()->getZ();
            }
            double phi = connecting_track.getPhi();
            double theta = connecting_track.getTheta();

            /* Check angular fluxes */
            for (int pe=0; pe < _fluxes_per_track; pe++) {
              if (fabs(_start_flux(connecting_idx, !connect_fwd, pe)
                  - _boundary_flux(t, dir, pe)) > 1e-7) {
                std::string dir_string;
                std::string dir_conn_string;
                if (dir == 0)
                  dir_string = "FWD";
                else
                  dir_string = "BWD";
                if (connect_fwd)
                  dir_conn_string = "FWD";
                else
                  dir_conn_string = "BWD";
                log_printf(ERROR, "Angular flux mismatch found on Track %d "
                           "in domain %d in %s direction at index %d. Boundary"
                           " angular flux at this location is %f but the "
                           "starting flux at connecting Track %d in domain %d "
                           "in the %s direction is %f", t, my_rank, dir_string.c_str(),
                           pe, _boundary_flux(t, dir, pe), connecting_idx,
                           my_rank,  dir_conn_string.c_str(),
                           _start_flux(connecting_idx, !connect_fwd, pe));
              }
            }

            /* Test reflective boundaries */
            if (bc == REFLECTIVE) {

              /* Check that the reflecting Track has a different direction */
              if (fabs(phi - track.getPhi()) < 1e-5 &&
                  fabs(theta - track.getTheta()) < 1e-5)
                log_printf(ERROR, "Reflective boundary found on Track %d "
                           "with azimuthal angle %f and polar angle %f but "
                           "the reflective Track at index %d has the same "
                           "angles.", t, phi, theta, connecting_idx);

              /* Check that the reflecting Track shares the connecting point */
              if (fabs(point->getX() - x) > 1e-5 ||
                  fabs(point->getY() - y) > 1e-5 ||
                  fabs(point->getZ() - z) > 1e-5) {
                log_printf(ERROR, "Track linking error: Reflective Track %d "
                           "with connecting point (%f, %f, %f) does not "
                           "connect with Track %d at point (%f, %f, %f)",
                           t, point->getX(), point->getY(), point->getZ(),
                           connecting_idx, x, y, z);
              }
            }

            /* Test periodic boundaries */
            if (bc == PERIODIC) {

              /* Check that the periodic Track has the same direction */
              if (fabs(phi - track.getPhi()) < 1e-5 ||
                  fabs(theta - track.getTheta()) < 1e-5)
                log_printf(ERROR, "Periodic boundary found on Track %d "
                           "with azimuthal angle %f and polar angle %f but "
                           "the periodic Track at index %d has azimuthal "
                           " angle %f and polar angle %f", t, track.getPhi(),
                           track.getTheta(), connecting_idx, phi, theta);

              /* Check that the periodic Track has a does not share the same
                 connecting point */
              if (fabs(point->getX() - x) < 1e-5 &&
                  fabs(point->getY() - y) < 1e-5 &&
                  fabs(point->getZ() - z) < 1e-5)
                log_printf(ERROR, "Periodic boundary found on Track %d "
                           "at connecting point (%f, %f, %f) but the "
                           "connecting periodic Track at index %d has the "
                           "same connecting point", t, x, y, z,
                           connecting_idx);
            }
          }
        }
      }

      /* Broadcast new tester */
      tester++;
      long broadcast[2];
      broadcast[0] = -1;
      broadcast[1] = tester;
      for (int i=0; i < my_rank; i++) {
        MPI_Send(broadcast, 2, MPI_LONG, i, 0, MPI_cart);
      }
      for (int i = my_rank+1; i < num_ranks; i++) {
        MPI_Send(broadcast, 2, MPI_LONG, i, 0, MPI_cart);
      }
    }
    /* Responder */
    else {

      /* Look for messages */
      int message;
      MPI_Iprobe(tester, MPI_ANY_TAG, MPI_cart, &message, &stat);

      /* Check for an information request */
      if (message) {

        /* Receive the request for information */
        long connection[2];
        MPI_Recv(connection, 2, MPI_LONG, tester, 0, MPI_cart, &stat);

        /* Check for a broadcast */
        if (connection[0] == -1) {
          tester = connection[1];
        }
        else {
          /* Handle an information request */

          /* Fill the requested information */
          long t = connection[0];
          int dir = connection[1];

          /* Get the Track */
          TrackStackIndexes tsi;
          Track3D track;
          TrackGenerator3D* track_generator_3D =
            dynamic_cast<TrackGenerator3D*>(_track_generator);
          track_generator_3D->getTSIByIndex(t, &tsi);
          track_generator_3D->getTrackOTF(&track, &tsi);

          /* Fill the information */
          int send_size = _fluxes_per_track + 2 * 5;
          float buffer[send_size];
          for (int pe=0; pe < _fluxes_per_track; pe++)
            buffer[pe] = _start_flux(t, dir, pe);

          /* Get the connecting point */
          Point* point;
          if (dir == 0)
            point = track.getStart();
          else
            point = track.getEnd();

          /* Fill tracking data */
          double track_data[5];
          track_data[0] = point->getX();
          track_data[1] = point->getY();
          track_data[2] = point->getZ();
          track_data[3] = track.getTheta();
          track_data[4] = track.getPhi();

          for (int i=0; i < 5; i++) {
            int idx = _fluxes_per_track + 2 * i;
            double* track_info_location =
              reinterpret_cast<double*>(&buffer[idx]);
            track_info_location[0] = track_data[i];
          }

          /* Send the information */
          MPI_Send(buffer, send_size, MPI_FLOAT, tester, 0, MPI_cart);
        }
      }
    }
  }
  MPI_Barrier(MPI_cart);
  log_printf(NORMAL, "Passed boundary flux check");
}
#endif


/**
 * @brief Set the scalar flux for each FSR and energy group to some value.
 * @param value the value to assign to each FSR scalar flux
 */
void CPUSolver::flattenFSRFluxes(FP_PRECISION value) {

#pragma omp parallel for schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {
    for (int e=0; e < _num_groups; e++)
      _scalar_flux(r,e) = value;
  }
}


/**
 * @brief Set the scalar flux for each FSR to a chi spectrum
 */
void CPUSolver::flattenFSRFluxesChiSpectrum() {
  if (_chi_spectrum_material == NULL)
    log_printf(ERROR, "A flattening of the FSR fluxes for a chi spectrum was "
               "requested but no chi spectrum material was set.");

  FP_PRECISION* chi = _chi_spectrum_material->getChi();
#pragma omp parallel for schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {
    for (int e=0; e < _num_groups; e++)
      _scalar_flux(r,e) = chi[e];
  }
}


/**
 * @brief Stores the FSR scalar fluxes in the old scalar flux array.
 */
void CPUSolver::storeFSRFluxes() {

#pragma omp parallel for schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {
    for (int e=0; e < _num_groups; e++)
      _old_scalar_flux(r,e) = _scalar_flux(r,e);
  }
}


/**
 * @brief Normalizes all FSR scalar fluxes and Track boundary angular
 *        fluxes to the total fission source (times \f$ \nu \f$).
 */
double CPUSolver::normalizeFluxes() {

  double* int_fission_sources = _regionwise_scratch;

  /* Compute total fission source for each FSR, energy group */
#pragma omp parallel
  {
    int tid = omp_get_thread_num();
    FP_PRECISION* group_fission_sources = _groupwise_scratch.at(tid);
#pragma omp for schedule(guided)
    for (long r=0; r < _num_FSRs; r++) {

      /* Get pointers to important data structures */
      FP_PRECISION* nu_sigma_f = _FSR_materials[r]->getNuSigmaF();
      FP_PRECISION volume = _FSR_volumes[r];

      for (int e=0; e < _num_groups; e++)
        group_fission_sources[e] = nu_sigma_f[e] * _scalar_flux(r,e) * volume;

      int_fission_sources[r] = pairwise_sum<FP_PRECISION>(group_fission_sources,
                                                        _num_groups);
    }
  }

  /* Compute the total fission source */
  double tot_fission_source = pairwise_sum<double>(int_fission_sources,
                                                         _num_FSRs);

  /* Get the total number of source regions */
  long total_num_FSRs = _num_FSRs;

#ifdef MPIx
  /* Reduce total fission rates across domians */
  if (_geometry->isDomainDecomposed()) {

    /* Get the communicator */
    MPI_Comm comm = _geometry->getMPICart();

    /* Determine the floating point precision */
    double reduced_fission;

    /* Reduce fission rates */
    MPI_Allreduce(&tot_fission_source, &reduced_fission, 1, MPI_DOUBLE,
                  MPI_SUM, comm);
    tot_fission_source = reduced_fission;
    
    /* Get total number of FSRs across all domains */
    MPI_Allreduce(&_num_FSRs, &total_num_FSRs, 1, MPI_LONG, MPI_SUM, comm);
  }
#endif

  /* Normalize scalar fluxes in each FSR */
  double norm_factor = total_num_FSRs / tot_fission_source;

  log_printf(DEBUG, "Tot. Fiss. Src. = %f, Norm. factor = %f",
             tot_fission_source, norm_factor);

#pragma omp parallel for schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {
    for (int e=0; e < _num_groups; e++)
      _scalar_flux(r, e) *= norm_factor;
  }

  /* Normalize angular boundary fluxes for each Track */
#pragma omp parallel for schedule(guided)
  for (long idx=0; idx < 2 * _tot_num_tracks * _fluxes_per_track; idx++) {
    _start_flux[idx] *= norm_factor;
    _boundary_flux[idx] *= norm_factor;
  }

  return norm_factor;
}


/**
 * @brief Computes the total source (fission, scattering, fixed) in each FSR.
 * @details This method computes the total source in each FSR based on
 *          this iteration's current approximation to the scalar flux.
 */
void CPUSolver::computeFSRSources(int iteration) {

  long num_negative_sources = 0;

  /* For all FSRs, find the source */
#pragma omp parallel for schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {

    int tid = omp_get_thread_num();
    Material* material = _FSR_materials[r];
    FP_PRECISION* nu_sigma_f = material->getNuSigmaF();
    FP_PRECISION* chi = material->getChi();
    FP_PRECISION* sigma_t = material->getSigmaT();
    FP_PRECISION* fission_sources = _groupwise_scratch.at(tid);

    /* Initialize the fission sources to zero */
    FP_PRECISION fission_source = 0.0;

    /* Compute fission source for each group */
    if (material->isFissionable()) {
      for (int e=0; e < _num_groups; e++)
        fission_sources[e] = _scalar_flux(r,e) * nu_sigma_f[e];

      fission_source = pairwise_sum<FP_PRECISION>(fission_sources,
                                                  _num_groups);
      fission_source /= _k_eff;
    }

    /* Compute total (fission+scatter+fixed) source for group G */
    FP_PRECISION* scatter_sources = _groupwise_scratch.at(tid);
    FP_PRECISION* sigma_s = material->getSigmaS();
    for (int G=0; G < _num_groups; G++) {
      int first_idx = G * _num_groups;
      for (int g=0; g < _num_groups; g++)
        scatter_sources[g] = sigma_s[first_idx+g] * _scalar_flux(r,g);
      double scatter_source =
          pairwise_sum<FP_PRECISION>(scatter_sources, _num_groups);

      _reduced_sources(r,G) = fission_source * chi[G];
      _reduced_sources(r,G) += scatter_source + _fixed_sources(r,G);
      _reduced_sources(r,G) *= ONE_OVER_FOUR_PI;

      /* Correct negative sources to (near) zero */
      if (_reduced_sources(r,G) < 0.0) {
#pragma omp atomic
        num_negative_sources++;
        if (iteration < 30)
          _reduced_sources(r,G) = 1.0e-20;
      }
    }
  }

  /* Tally the total number of negative source across the entire problem */
  long total_num_negative_sources = num_negative_sources;
  int num_negative_source_domains = (num_negative_sources > 0);
  int total_num_negative_source_domains = num_negative_source_domains;
#ifdef MPIx
  if (_geometry->isDomainDecomposed()) {
    MPI_Allreduce(&num_negative_sources, &total_num_negative_sources, 1,
                  MPI_LONG, MPI_SUM, _geometry->getMPICart());
    MPI_Allreduce(&num_negative_source_domains,
                  &total_num_negative_source_domains, 1,
                  MPI_INT, MPI_SUM, _geometry->getMPICart());
  }
#endif

  /* Report negative sources */
  if (total_num_negative_sources > 0) {
    if (_geometry->isRootDomain()) {
      log_printf(WARNING, "Computed %ld negative sources on %d domains",
                 total_num_negative_sources,
                 total_num_negative_source_domains);
      if (iteration < 30)
        log_printf(WARNING, "Negative sources corrected to zero");
    }
  }
}


/**
 * @brief Computes the residual between source/flux iterations.
 * @param res_type the type of residuals to compute
 *        (SCALAR_FLUX, FISSION_SOURCE, TOTAL_SOURCE)
 * @return the average residual in each FSR
 */
double CPUSolver::computeResidual(residualType res_type) {

  long norm;
  double residual;
  double* residuals = _regionwise_scratch;
  memset(residuals, 0., _num_FSRs * sizeof(double));

  FP_PRECISION* reference_flux = _old_scalar_flux;
  if (_calculate_residuals_by_reference)
    reference_flux = _reference_flux;

  if (res_type == SCALAR_FLUX) {

    norm = _num_FSRs;

    for (long r=0; r < _num_FSRs; r++) {
      for (int e=0; e < _num_groups; e++)
        if (reference_flux(r,e) > 0.) {
          residuals[r] += pow((_scalar_flux(r,e) - reference_flux(r,e)) /
                              reference_flux(r,e), 2);
      }
    }
  }

  else if (res_type == FISSION_SOURCE) {

    norm = _num_fissionable_FSRs;

    double new_fission_source, old_fission_source;
    FP_PRECISION* nu_sigma_f;
    Material* material;

    for (long r=0; r < _num_FSRs; r++) {
      new_fission_source = 0.;
      old_fission_source = 0.;
      material = _FSR_materials[r];

      if (material->isFissionable()) {
        nu_sigma_f = material->getNuSigmaF();

        for (int e=0; e < _num_groups; e++) {
          new_fission_source += _scalar_flux(r,e) * nu_sigma_f[e];
          old_fission_source += reference_flux(r,e) * nu_sigma_f[e];
        }

        if (old_fission_source > 0.)
          residuals[r] = pow((new_fission_source -  old_fission_source) /
                              old_fission_source, 2);
      }
    }
  }

  else if (res_type == TOTAL_SOURCE) {

    norm = _num_FSRs;

    double new_total_source, old_total_source;
    double inverse_k_eff = 1.0 / _k_eff;
    FP_PRECISION* nu_sigma_f;
    Material* material;

    for (long r=0; r < _num_FSRs; r++) {
      new_total_source = 0.;
      old_total_source = 0.;
      material = _FSR_materials[r];

      if (material->isFissionable()) {
        nu_sigma_f = material->getNuSigmaF();

        for (int e=0; e < _num_groups; e++) {
          new_total_source += _scalar_flux(r,e) * nu_sigma_f[e];
          old_total_source += reference_flux(r,e) * nu_sigma_f[e];
        }

        new_total_source *= inverse_k_eff;
        old_total_source *= inverse_k_eff;
      }

      /* Compute total scattering source for group G */
      FP_PRECISION* sigma_s = material->getSigmaS();
      for (int G=0; G < _num_groups; G++) {
        int first_idx = G * _num_groups;
        for (int g=0; g < _num_groups; g++) {
          new_total_source += sigma_s[first_idx+g] * _scalar_flux(r,g);
          old_total_source += sigma_s[first_idx+g] * reference_flux(r,g);
        }
      }

      if (old_total_source > 0.)
        residuals[r] = pow((new_total_source -  old_total_source) /
                            old_total_source, 2);
    }
  }

  /* Sum up the residuals from each FSR and normalize */
  residual = pairwise_sum<double>(residuals, _num_FSRs);

#ifdef MPIx
  /* Reduce residuals across domains */
  if (_geometry->isDomainDecomposed()) {

    /* Get the communicator */
    MPI_Comm comm = _geometry->getMPICart();

    /* Reduce residuals */
    double reduced_res;
    MPI_Allreduce(&residual, &reduced_res, 1, MPI_DOUBLE, MPI_SUM, comm);
    residual = reduced_res;

    /* Reduce normalization factors */
    long reduced_norm;
    MPI_Allreduce(&norm, &reduced_norm, 1, MPI_LONG, MPI_SUM, comm);
    norm = reduced_norm;
  }
#endif

  if (res_type == FISSION_SOURCE && norm == 0)
      log_printf(ERROR, "The Solver is unable to compute a "
                 "FISSION_SOURCE residual without fissionable FSRs");

  /* Error check residual componenets */
  if (residual < 0.0) {
    log_printf(WARNING, "MOC Residual mean square error %6.4f less than zero", 
               residual);
    residual = 0.0;
  }
  if (norm <= 0) {
    log_printf(WARNING, "MOC resdiual norm %d less than one", norm);
    norm = 1;
  }

  /* Compute RMS residual */
  residual = sqrt(residual / norm);
  return residual;
}


/**
 * @brief Compute \f$ k_{eff} \f$ from successive fission sources.
 */
void CPUSolver::computeKeff() {

  double* FSR_rates = _regionwise_scratch;
  double rates[3];

  int num_rates = 1;
  if (!_keff_from_fission_rates)
    num_rates = 2;

  /* Loop over all FSRs and compute the volume-integrated total rates */
  for (int type=0; type < num_rates; type++) {
#pragma omp parallel for schedule(guided)
    for (long r=0; r < _num_FSRs; r++) {

      int tid = omp_get_thread_num();
      FP_PRECISION* group_rates = _groupwise_scratch.at(tid);
      FP_PRECISION volume = _FSR_volumes[r];
      Material* material = _FSR_materials[r];

      /* Get cross section for desired rate */
      FP_PRECISION* sigma;
      if (type == 0)
        sigma = material->getNuSigmaF();
      else
        sigma = material->getSigmaA();

      for (int e=0; e < _num_groups; e++)
        group_rates[e] = sigma[e] * _scalar_flux(r,e);

      FSR_rates[r] = pairwise_sum<FP_PRECISION>(group_rates, _num_groups);
      FSR_rates[r] *= volume;
    }

    /* Reduce new fission rates across FSRs */
    rates[type] = pairwise_sum<double>(FSR_rates, _num_FSRs);
  }

  /* Compute total leakage rate */
  if (!_keff_from_fission_rates) {
    rates[2] = pairwise_sum<float>(_boundary_leakage, _tot_num_tracks);
    num_rates=3;
  }

  /* Get the total number of source regions */
  long total_num_FSRs = _num_FSRs;
  
#ifdef MPIx
  /* Reduce rates across domians */
  if (_geometry->isDomainDecomposed()) {

    /* Get the communicator */
    MPI_Comm comm = _geometry->getMPICart();

    /* Copy local rates */
    double local_rates[num_rates];
    for (int i=0; i < num_rates; i++)
      local_rates[i] = rates[i];
 
     /* Reduce computed rates */
    MPI_Allreduce(local_rates, rates, num_rates, MPI_DOUBLE, MPI_SUM, comm);
    
    /* Get total number of FSRs across all domains */
    MPI_Allreduce(&_num_FSRs, &total_num_FSRs, 1, MPI_LONG, MPI_SUM, comm);
  }
#endif
  if (!_keff_from_fission_rates)
    /* Compute k-eff from fission, absorption, and leakage rates */
    _k_eff = rates[0] / (rates[1] + rates[2]);
  else
    _k_eff *= rates[0] / total_num_FSRs;
}


/**
 * @brief This method performs one transport sweep of all azimuthal angles,
 *        Tracks, Track segments, polar angles and energy groups.
 * @details The method integrates the flux along each Track and updates the
 *          boundary fluxes for the corresponding output Track, while updating
 *          the scalar flux in each flat source region.
 */
void CPUSolver::transportSweep() {

  log_printf(DEBUG, "Transport sweep with %d OpenMP threads",
      _num_threads);

  if (_cmfd != NULL && _cmfd->isFluxUpdateOn())
    _cmfd->zeroCurrents();

  /* Initialize flux in each FSR to zero */
  flattenFSRFluxes(0.0);

  /* Copy starting flux to current flux */
  copyBoundaryFluxes();

  /* Tally the starting fluxes to boundaries */
  if (_cmfd != NULL)
    if (_cmfd->isSigmaTRebalanceOn())
      tallyStartingCurrents();

  /* Zero boundary leakage tally */
  if (_cmfd == NULL)
    memset(_boundary_leakage, 0., _tot_num_tracks * sizeof(float));

  /* Tracks are traversed and the MOC equations from this CPUSolver are applied
     to all Tracks and corresponding segments */
  if (_OTF_transport) {
    TransportSweepOTF sweep_tracks(_track_generator);
    sweep_tracks.setCPUSolver(this);
    sweep_tracks.execute();
  }
  else {
    TransportSweep sweep_tracks(this);
    sweep_tracks.execute();
  }

#ifdef MPIx
  /* Transfer all interface fluxes after the transport sweep */
  if (_track_generator->getGeometry()->isDomainDecomposed())
    transferAllInterfaceFluxes();
#endif
}


/**
 * @brief Computes the contribution to the FSR scalar flux from a Track segment.
 * @details This method integrates the angular flux for a Track segment across
 *          energy groups and polar angles, and tallies it into the FSR
 *          scalar flux, and updates the Track's angular flux.
 * @param curr_segment a pointer to the Track segment of interest
 * @param azim_index azimuthal angle index for this segment
 * @param polar_index polar angle index for this segment 
 * @param track_flux a pointer to the Track's angular flux
 * @param fsr_flux a pointer to the temporary FSR flux buffer
 */
void CPUSolver::tallyScalarFlux(segment* curr_segment,
                                int azim_index, int polar_index,
                                float* track_flux,
                                FP_PRECISION* fsr_flux) {

  long fsr_id = curr_segment->_region_id;
  FP_PRECISION length = curr_segment->_length;
  FP_PRECISION* sigma_t = curr_segment->_material->getSigmaT();

  /* The change in angular flux along this Track segment in the FSR */
  ExpEvaluator* exp_evaluator = _exp_evaluators[azim_index][polar_index];

  /* Set the FSR scalar flux buffer to zero */
  memset(fsr_flux, 0.0, _num_groups * sizeof(FP_PRECISION));

  if (_solve_3D) {

    FP_PRECISION length_2D = exp_evaluator->convertDistance3Dto2D(length);

    for (int e=0; e < _num_groups; e++) {

      FP_PRECISION tau = sigma_t[e] * length_2D;

      /* Compute the exponential */
      FP_PRECISION exponential = exp_evaluator->computeExponential(tau, 0);

      /* Attenuate and tally the flux */
      FP_PRECISION delta_psi = (tau * track_flux[e] - length_2D *
              _reduced_sources(fsr_id, e)) * exponential;
      fsr_flux[e] += delta_psi * _quad->getWeightInline(azim_index,
                                                        polar_index);
      track_flux[e] -= delta_psi;
    }
  }
  else {

    int pe = 0;

    /* Loop over energy groups */
    for (int e=0; e < _num_groups; e++) {

      FP_PRECISION tau = sigma_t[e] * length;

      /* Loop over polar angles */
      for (int p=0; p < _num_polar/2; p++) {

        /* Compute the exponential */
        FP_PRECISION exponential = exp_evaluator->computeExponential(tau, p);

        /* Attenuate and tally the flux */
        FP_PRECISION delta_psi = (tau * track_flux[pe] -
                length * _reduced_sources(fsr_id,e)) * exponential;
        fsr_flux[e] += delta_psi * _quad->getWeightInline(azim_index, p);
        track_flux[pe] -= delta_psi;
        pe++;
      }
    }
  }

  /* Atomically increment the FSR scalar flux from the temporary array */
  omp_set_lock(&_FSR_locks[fsr_id]);
  {
    for (int e=0; e < _num_groups; e++)
      _scalar_flux(fsr_id,e) += fsr_flux[e];
  }
  omp_unset_lock(&_FSR_locks[fsr_id]);
}


/**
 * @brief Tallies the current contribution from this segment across the
 *        the appropriate CMFD mesh cell surface.
 * @param curr_segment a pointer to the Track segment of interest
 * @param azim_index the azimuthal index for this segmenbt
 * @param polar_index the polar index for this segmenbt
 * @param track_flux a pointer to the Track's angular flux
 * @param fwd boolean indicating direction of integration along segment
 */
void CPUSolver::tallyCurrent(segment* curr_segment, int azim_index,
                             int polar_index, float* track_flux,
                             bool fwd) {

  /* Tally surface currents if CMFD is in use */
  if (_cmfd != NULL && _cmfd->isFluxUpdateOn())
    _cmfd->tallyCurrent(curr_segment, track_flux, azim_index, polar_index, fwd);
}


/**
 * @brief Updates the boundary flux for a Track given boundary conditions.
 * @details For reflective boundary conditions, the outgoing boundary flux
 *          for the Track is given to the reflecting Track. For vacuum
 *          boundary conditions, the outgoing flux tallied as leakage.
 * @param track a pointer to the Track of interest
 * @param azim_index azimuthal angle index for this segment
 * @param polar_index polar angle index for this segment
 * @param direction the Track direction (forward - true, reverse - false)
 * @param track_flux a pointer to the Track's outgoing angular flux
 */
void CPUSolver::transferBoundaryFlux(Track* track,
                                     int azim_index, int polar_index,
                                     bool direction,
                                     float* track_flux) {

  /* Extract boundary conditions for this Track and the pointer to the
   * outgoing reflective Track, and index into the leakage array */
  boundaryType bc_out;
  boundaryType bc_in;
  long track_out_id;
  int start_out;

  /* For the "forward" direction */
  if (direction) {
    bc_in = track->getBCBwd();
    bc_out = track->getBCFwd();
    track_out_id = track->getTrackNextFwd();
    start_out = _fluxes_per_track * (!track->getNextFwdFwd());
  }

  /* For the "reverse" direction */
  else {
    bc_in = track->getBCFwd();
    bc_out = track->getBCBwd();
    track_out_id = track->getTrackNextBwd();
    start_out = _fluxes_per_track * (!track->getNextBwdFwd());
  }

  /* Determine if flux should be transferred */
  if (bc_out == REFLECTIVE || bc_out == PERIODIC) {
    float* track_out_flux = &_start_flux(track_out_id, 0, start_out);
    for (int pe=0; pe < _fluxes_per_track; pe++)
      track_out_flux[pe] = track_flux[pe];
  }
  if (bc_in == VACUUM) {
    long track_id = track->getUid();
    float* track_in_flux = &_start_flux(track_id, !direction, 0);
    for (int pe=0; pe < _fluxes_per_track; pe++)
      track_in_flux[pe] = 0.0;
  }

  /* Tally leakage if applicable */
  if (_cmfd == NULL) {
    if (bc_out == VACUUM) {
      long track_id = track->getUid();
      FP_PRECISION weight = _quad->getWeightInline(azim_index, polar_index);
      for (int pe=0; pe < _fluxes_per_track; pe++)
        _boundary_leakage[track_id] += weight * track_flux[pe];
    }
  }
}


/**
 * @brief Add the source term contribution in the transport equation to
 *        the FSR scalar flux.
 */
void CPUSolver::addSourceToScalarFlux() {

  FP_PRECISION volume;
  FP_PRECISION* sigma_t;
  long num_negative_fluxes = 0;

  /* Add in source term and normalize flux to volume for each FSR */
  /* Loop over FSRs, energy groups */
#pragma omp parallel for private(volume, sigma_t) schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {
    volume = _FSR_volumes[r];
    sigma_t = _FSR_materials[r]->getSigmaT();

    for (int e=0; e < _num_groups; e++) {
      _scalar_flux(r, e) /= (sigma_t[e] * volume);
      _scalar_flux(r, e) += FOUR_PI * _reduced_sources(r, e) / sigma_t[e];
      if (_scalar_flux(r, e) < 0.0) {
        _scalar_flux(r, e) = 1.0e-20;
#pragma omp atomic
        num_negative_fluxes++;
      }
    }
  }

  /* Tally the total number of negative fluxes across the entire problem */
  long total_num_negative_fluxes = num_negative_fluxes;
  int num_negative_flux_domains = (num_negative_fluxes > 0);
  int total_num_negative_flux_domains = num_negative_flux_domains;
#ifdef MPIx
  if (_geometry->isDomainDecomposed()) {
    MPI_Allreduce(&num_negative_fluxes, &total_num_negative_fluxes, 1,
                  MPI_LONG, MPI_SUM, _geometry->getMPICart());
    MPI_Allreduce(&num_negative_flux_domains,
                  &total_num_negative_flux_domains, 1,
                  MPI_INT, MPI_SUM, _geometry->getMPICart());
  }
#endif

  /* Report negative fluxes */
  if (total_num_negative_fluxes > 0) {
    if (_geometry->isRootDomain()) {
      log_printf(WARNING, "Computed %ld negative fluxes on %d domains",
                 total_num_negative_fluxes,
                 total_num_negative_flux_domains);
    }
  }

}


/**
 * @brief Computes the stabilizing flux for transport stabilization
 */
void CPUSolver::computeStabilizingFlux() {

  if (_stabilization_type == DIAGONAL) {
    
    /* Loop over all flat source regions */
#pragma omp parallel for
    for (long r=0; r < _num_FSRs; r++) {

      /* Extract the scattering matrix */
      FP_PRECISION* scattering_matrix = _FSR_materials[r]->getSigmaS();
    
      /* Extract total cross-sections */
      FP_PRECISION* sigma_t = _FSR_materials[r]->getSigmaT();

      for (int e=0; e < _num_groups; e++) {
      
        /* Extract the in-scattering (diagonal) element */
        FP_PRECISION sigma_s = scattering_matrix[e*_num_groups+e];
      
        /* For negative cross-sections, add the absolute value of the 
           in-scattering rate to the stabilizing flux */
        if (sigma_s < 0.0)
          _stabilizing_flux(r, e) = -_scalar_flux(r,e) * _stabilization_factor 
              * sigma_s / sigma_t[e];
      }
    }
  }
  else if (_stabilization_type == YAMAMOTO) {

    /* Treat each group */
#pragma omp parallel for
    for (int e=0; e < _num_groups; e++) {

      /* Look for largest absolute scattering ratio */
      FP_PRECISION max_ratio = 0.0;
      for (long r=0; r < _num_FSRs; r++) {
        
        /* Extract the scattering value */
        FP_PRECISION scat = _FSR_materials[r]->getSigmaSByGroup(e+1, e+1);
    
        /* Extract total cross-sections */
        FP_PRECISION total = _FSR_materials[r]->getSigmaTByGroup(e+1);

        /* Determine scattering ratio */
        FP_PRECISION ratio = std::abs(scat / total);
        if (ratio > max_ratio)
          max_ratio = ratio;
      }
      max_ratio *= _stabilization_factor;
      for (long r=0; r < _num_FSRs; r++) {
        _stabilizing_flux(r, e) = _scalar_flux(r,e) * max_ratio;
      }
    }
  }
  else if (_stabilization_type == GLOBAL) {
    
    /* Get the multiplicative factor */
    FP_PRECISION mult_factor = 1.0 / _stabilization_factor - 1.0;
   
    /* Apply the global muliplicative factor */ 
#pragma omp parallel for
    for (long r=0; r < _num_FSRs; r++)
      for (int e=0; e < _num_groups; e++)
        _stabilizing_flux(r, e) = mult_factor * _scalar_flux(r,e);
  }
}


/**
 * @brief Adjusts the scalar flux for transport stabilization
 */
void CPUSolver::stabilizeFlux() {

  if (_stabilization_type == DIAGONAL) {
  
    /* Loop over all flat source regions */
#pragma omp parallel for
    for (long r=0; r < _num_FSRs; r++) {

      /* Extract the scattering matrix */
      FP_PRECISION* scattering_matrix = _FSR_materials[r]->getSigmaS();
    
      /* Extract total cross-sections */
      FP_PRECISION* sigma_t = _FSR_materials[r]->getSigmaT();
    
      for (int e=0; e < _num_groups; e++) {
      
        /* Extract the in-scattering (diagonal) element */
        FP_PRECISION sigma_s = scattering_matrix[e*_num_groups+e];
      
        /* For negative cross-sections, add the stabilizing flux
           and divide by the diagonal matrix element used to form it so that
           no bias is introduced but the source iteration is stabilized */
        if (sigma_s < 0.0) {
          _scalar_flux(r, e) += _stabilizing_flux(r,e);
          _scalar_flux(r, e) /= (1.0 - _stabilization_factor * sigma_s / 
                                 sigma_t[e]);
        }
      }
    }
  }
  else if (_stabilization_type == YAMAMOTO) {

    /* Treat each group */
#pragma omp parallel for
    for (int e=0; e < _num_groups; e++) {

      /* Look for largest absolute scattering ratio */
      FP_PRECISION max_ratio = 0.0;
      for (long r=0; r < _num_FSRs; r++) {
        
        /* Extract the scattering value */
        FP_PRECISION scat = _FSR_materials[r]->getSigmaSByGroup(e+1, e+1);
    
        /* Extract total cross-sections */
        FP_PRECISION total = _FSR_materials[r]->getSigmaTByGroup(e+1);

        /* Determine scattering ratio */
        FP_PRECISION ratio = std::abs(scat / total);
        if (ratio > max_ratio)
          max_ratio = ratio;
      }
      max_ratio *= _stabilization_factor;
      for (long r=0; r < _num_FSRs; r++) {
        _scalar_flux(r, e) += _stabilizing_flux(r, e);
        _scalar_flux(r, e) /= (1 + max_ratio);
      }
    }
  }
  else if (_stabilization_type == GLOBAL) {

    /* Apply the damping factor */    
#pragma omp parallel for
    for (long r=0; r < _num_FSRs; r++) {
      for (int e=0; e < _num_groups; e++) {
        _scalar_flux(r, e) += _stabilizing_flux(r, e);
        _scalar_flux(r, e) *= _stabilization_factor;
      }
    }
  }
}


/**
 * @brief Computes the volume-averaged, energy-integrated nu-fission rate in
 *        each FSR and stores them in an array indexed by FSR ID.
 * @details This is a helper method for SWIG to allow users to retrieve
 *          FSR nu-fission rates as a NumPy array. An example of how this
 *          method can be called from Python is as follows:
 *
 * @code
 *          num_FSRs = geometry.getNumFSRs()
 *          fission_rates = solver.computeFSRFissionRates(num_FSRs)
 * @endcode
 *
 * @param fission_rates an array to store the nu-fission rates (implicitly
 *                      passed in as a NumPy array from Python)
 * @param num_FSRs the number of FSRs passed in from Python
 */
void CPUSolver::computeFSRFissionRates(double* fission_rates, long num_FSRs) {

  if (_scalar_flux == NULL)
    log_printf(ERROR, "Unable to compute FSR fission rates since the "
               "source distribution has not been calculated");

  log_printf(INFO, "Computing FSR fission rates...");

  FP_PRECISION* nu_sigma_f;
  FP_PRECISION vol;

  /* Initialize fission rates to zero */
  for (long r=0; r < _num_FSRs; r++)
    fission_rates[r] = 0.0;

  /* Loop over all FSRs and compute the volume-weighted fission rate */
#pragma omp parallel for private (nu_sigma_f, vol) schedule(guided)
  for (long r=0; r < _num_FSRs; r++) {
    nu_sigma_f = _FSR_materials[r]->getNuSigmaF();
    vol = _FSR_volumes[r];

    for (int e=0; e < _num_groups; e++)
      fission_rates[r] += nu_sigma_f[e] * _scalar_flux(r,e) * vol;
  }

  /* Reduce domain data for domain decomposition */
#ifdef MPIx
  if (_geometry->isDomainDecomposed()) {

    /* Allocate buffer for communcation */
    long num_total_FSRs = _geometry->getNumTotalFSRs();
    double* temp_fission_rates = new double[num_total_FSRs];
    for (int i=0; i < num_total_FSRs; i++)
      temp_fission_rates[i] = 0;

    int rank = 0;
    MPI_Comm comm = _geometry->getMPICart();
    MPI_Comm_rank(comm, &rank);
    for (long r=0; r < num_total_FSRs; r++) {

      /* Determine the domain and local FSR ID */
      long fsr_id = r;
      int domain = 0;
      _geometry->getLocalFSRId(r, fsr_id, domain);

      /* Set data if in the correct domain */
      if (domain == rank)
        temp_fission_rates[r] = fission_rates[fsr_id];
    }

    MPI_Allreduce(temp_fission_rates, fission_rates, num_total_FSRs,
                  MPI_DOUBLE, MPI_SUM, comm);
    delete [] temp_fission_rates;
  }
#endif
}


/**
 * @brief A function that prints a summary of the input parameters
 */
void CPUSolver::printInputParamsSummary() {

  /* Print general solver input params summary */
  Solver::printInputParamsSummary();

  /* Print threads used */
  log_printf(NORMAL, "Using %d threads", _num_threads);
}


/**
 * @brief A function that prints the source region fluxes on a 2D mesh grid
 * @param dim1 coordinates of the mesh grid in the first direction
 * @param dim2 coordinates of the mesh grid in the second direction
 * @param offset The location of the mesh grid center on the perpendicular axis
 * @param plane 'xy', 'xz' or 'yz' the plane in which the mesh grid lies
 */
void CPUSolver::printFSRFluxes(std::vector<double> dim1,
                               std::vector<double> dim2,
                               double offset, const char* plane) {

  int rank = 0;
#ifdef MPIx
  MPI_Comm comm;
  if (_geometry->isDomainDecomposed()) {
    comm = _geometry->getMPICart();
    MPI_Comm_rank(comm, &rank);
  }

  MPI_Datatype precision;
  if (sizeof(FP_PRECISION) == 4)
    precision = MPI_FLOAT;
  else
    precision = MPI_DOUBLE;
#endif
  std::vector<long> fsr_ids = _geometry->getSpatialDataOnGrid(dim1, dim2,
                                                              offset, plane,
                                                              "fsr");
  std::vector<int> domain_contains_coords(fsr_ids.size());
  std::vector<int> num_contains_coords(fsr_ids.size());
#pragma omp parallel for
  for (long r=0; r < fsr_ids.size(); r++) {
    if (fsr_ids.at(r) != -1)
      domain_contains_coords.at(r) = 1;
    else
      domain_contains_coords.at(r) = 0;
  }

#ifdef MPIx
  if (_geometry->isDomainDecomposed())
    MPI_Allreduce(&domain_contains_coords[0], &num_contains_coords[0],
                  fsr_ids.size(), MPI_INT, MPI_SUM, comm);
#endif
  if (!_geometry->isDomainDecomposed())
    for (int i=0; i < fsr_ids.size(); i++)
      num_contains_coords[i] = domain_contains_coords[i];

  for (int e=0; e < _num_groups; e++) {

    std::vector<FP_PRECISION> domain_fluxes(fsr_ids.size(), 0);
    std::vector<FP_PRECISION> total_fluxes(fsr_ids.size());

#pragma omp parallel for
    for (long r=0; r < fsr_ids.size(); r++) {
      if (domain_contains_coords.at(r) != 0)
        domain_fluxes.at(r) = getFlux(fsr_ids.at(r), e+1);
    }

#ifdef MPIx
    if (_geometry->isDomainDecomposed())
      MPI_Allreduce(&domain_fluxes[0], &total_fluxes[0],
                    fsr_ids.size(), precision, MPI_SUM, comm);
#endif
    if (!_geometry->isDomainDecomposed())
      for (int i=0; i < fsr_ids.size(); i++)
        total_fluxes[i] = domain_fluxes[i];

    if (rank == 0) {
      for (int i=0; i<dim1.size(); i++) {
        for (int j=0; j<dim2.size(); j++) {
          int r = i + j*dim1.size();
          double flux = total_fluxes.at(r) / num_contains_coords.at(r);
          log_printf(NORMAL, "(%d: %f, %d: %f) -> %f", i, dim1.at(i), j, 
                     dim2.at(j), flux);
        }
      }
    }
  }
}


/**
 * @brief A function that prints fsr fluxes in xy plane at z=middle
 * @details Recommend deletion, since redundant printFluxes
 */
void CPUSolver::printFluxesTemp() {

  Universe* root = _geometry->getRootUniverse();

  int nx = 100;
  int ny = 100;
  int nz = 1;

  double x_min = root->getMinX() + 2*TINY_MOVE;
  double x_max = root->getMaxX() - 2*TINY_MOVE;
  double y_min = root->getMinY() + 2*TINY_MOVE;
  double y_max = root->getMaxY() - 2*TINY_MOVE;
  double z_min = root->getMinZ() + 2*TINY_MOVE;
  double z_max = root->getMaxZ() - 2*TINY_MOVE;

  std::vector<double> x(nx);
  std::vector<double> y(ny);
  for (int i=0; i < nx; i++)
    x.at(i) = x_min + i * (x_max - x_min) / nx;
  for (int j=0; j < ny; j++)
    y.at(j) = y_min + j * (y_max - y_min) / ny;

  double z_mid = (z_min + z_max) / 2 + TINY_MOVE;

  printFSRFluxes(x, y, z_mid, "xy");
}


/**
 * @brief A function that prints the number of FSRs with negative sources in 
 *        the whole geometry subdivided by a 3D lattice. The number of negative
 *        sources per energy group is also printed out.
 * @param iteration the current iteration
 * @param num_x number of divisions in X direction
 * @param num_y number of divisions in Y direction
 * @param num_z number of divisions in Z direction 
 */
void CPUSolver::printNegativeSources(int iteration, int num_x, int num_y,
                                     int num_z) {

  long long iter = iteration;
  std::string fname = "k_negative_sources_iter_";
  std::string iter_num = std::to_string(iter);
  fname += iter_num;
  std::ofstream out(fname);

  /* Create a lattice */
  Lattice lattice;
  lattice.setNumX(num_x);
  lattice.setNumY(num_y);
  lattice.setNumZ(num_z);

  /* Get the root universe */
  Universe* root_universe = _geometry->getRootUniverse();

  /* Determine the geometry widths in each direction */
  double width_x = (root_universe->getMaxX() - root_universe->getMinX())/num_x;
  double width_y = (root_universe->getMaxY() - root_universe->getMinY())/num_y;
  double width_z = (root_universe->getMaxZ() - root_universe->getMinZ())/num_z;

  /* Determine the center-point of the geometry */
  double offset_x = (root_universe->getMinX() + root_universe->getMaxX()) / 2;
  double offset_y = (root_universe->getMinY() + root_universe->getMaxY()) / 2;
  double offset_z = (root_universe->getMinZ() + root_universe->getMaxZ()) / 2;

  /* Create the Mesh lattice */
  lattice.setWidth(width_x, width_y, width_z);
  lattice.setOffset(offset_x, offset_y, offset_z);

  /* Create a group-wise negative source mapping */
  int by_group[_num_groups];
  for (int e=0; e < _num_groups; e++)
    by_group[e] = 0;

  int mapping[num_x*num_y*num_z];
  for (int i=0; i < num_x*num_y*num_z; i++)
    mapping[i] = 0;

  /* Loop over all flat source regions */
  for (long r=0; r < _num_FSRs; r++) {

    /* Determine the Mesh cell containing the FSR */
    Point* pt = _geometry->getFSRPoint(r);
    int lat_cell = lattice.getLatticeCell(pt);

    /* Determine the number of negative sources */
    for (int e=0; e < _num_groups; e++) {
      if (_reduced_sources(r,e) < 0.0) {
        by_group[e]++;
        mapping[lat_cell]++;
      }
    }
  }

  /* If domain decomposed, do a reduction */
#ifdef MPIx
  if (_geometry->isDomainDecomposed()) {
    int size = num_x * num_y * num_z;
    int neg_src_send[size];
    for (int i=0; i < size; i++)
      neg_src_send[i] = mapping[i];
    MPI_Allreduce(neg_src_send, mapping, size, MPI_INT, MPI_SUM,
                  _geometry->getMPICart());

    int neg_src_grp_send[size];
    for (int e=0; e < _num_groups; e++)
        neg_src_grp_send[e] = by_group[e];
    MPI_Allreduce(neg_src_grp_send, by_group, _num_groups, MPI_INT, MPI_SUM,
                  _geometry->getMPICart());
  }
#endif


  /* Print negative source distribution to file */
  if (_geometry->isRootDomain()) {
    out << "[NORMAL]  Group-wise distribution of negative sources:"
        << std::endl;
    for (int e=0; e < _num_groups; e++)
      out << "[NORMAL]  Group "  << e << ": " << by_group[e] << std::endl;
    out << "[NORMAL]  Spatial distribution of negative sources:" << std::endl;
    for (int z=0; z < num_z; z++) {
      out << " -------- z = " << z << " ----------" << std::endl;
      for (int y=0; y < num_y; y++) {
        for (int x=0; x < num_x; x++) {
          int ind = (z * num_y + y) * num_x + x;
          out << mapping[ind] << " ";
        }
        out << std::endl;
      }
    }
  }
  out.close();
}
