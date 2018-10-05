#include "../../../src/CPUSolver.h"
#include "../../../src/CPULSSolver.h"
#include "../../../src/log.h"
#include "../../../src/Mesh.h"
#include <array>
#include <iostream>

int main(int argc, char* argv[]) {

#ifdef MPIx
  MPI_Init(&argc, &argv);
  log_set_ranks(MPI_COMM_WORLD);
#endif
  
  int arg_index = 0;
  std::string msg_string;
  while (arg_index < argc) {
    msg_string += argv[arg_index];
    msg_string += " ";
    arg_index++;    
  }
  
  Runtime_Parametres runtime;
  set_Runtime_Parametres(runtime, argc, argv);
  
  residualType aaa = (residualType)runtime._MOC_src_residual_type;
  segmentationType bbb = (segmentationType)runtime._segmentation_type;
  /* stuck here for debug tools to attach */
  while (runtime._debug_flag) ;
  
  /* Define simulation parameters */
  #ifdef OPENMP
  int num_threads = runtime._num_threads;
  //omp_get_num_procs();
  #else
  int num_threads = 1;
  #endif

  /* Set logging information */
  set_log_filename(runtime._log_filename);
  set_log_level(runtime._log_level);
  set_line_length(120);

  log_printf(NORMAL, "%s", msg_string.c_str());
  log_printf(NORMAL, "Azimuthal spacing = %f", runtime._azim_spacing);
  log_printf(NORMAL, "Azimuthal angles = %d", runtime._num_azim);
  log_printf(NORMAL, "Polar spacing = %f", runtime._polar_spacing);
  log_printf(NORMAL, "Polar angles = %d", runtime._num_polar);

  /* Create CMFD mesh */
  log_printf(NORMAL, "Creating CMFD mesh...");
  Cmfd* cmfd = new Cmfd();
  cmfd->setSORRelaxationFactor(1.5);
  if(runtime._cell_widths_x.empty() || runtime._cell_widths_y.empty() ||
      runtime._cell_widths_z.empty()) 
    cmfd->setLatticeStructure(runtime._NCx, runtime._NCy, runtime._NCz);
  else {
    std::vector< std::vector<double> > cmfd_widths{runtime._cell_widths_x, 
        runtime._cell_widths_y, runtime._cell_widths_z};
    cmfd->setWidths(cmfd_widths);
  }
  std::vector<std::vector<int> > cmfd_group_structure{{1,2,3},{4,5},{6,7}};
  cmfd->setGroupStructure(cmfd_group_structure);
  cmfd->setKNearest(runtime._knearest);
  cmfd->setCentroidUpdateOn(runtime._CMFD_centroid_update_on);
  cmfd->useAxialInterpolation(runtime._use_axial_interpolation);

  /* Create the geometry */
  log_printf(NORMAL, "Creating geometry...");
  Geometry *geometry = new Geometry();
  geometry->loadFromFile(runtime._geo_filename, false); 
#ifdef MPIx
  geometry->setDomainDecomposition(runtime._NDx, runtime._NDy, runtime._NDz, MPI_COMM_WORLD); 
  geometry->setNumDomainModules(runtime._NMx, runtime._NMy, runtime._NMz);
#endif
  geometry->setCmfd(cmfd);
  geometry->initializeFlatSourceRegions();

  /* Generate tracks */
  log_printf(NORMAL, "Initializing the track generator...");
  Quadrature* quad = new EqualWeightPolarQuad();
  quad->setNumAzimAngles(runtime._num_azim);
  quad->setNumPolarAngles(runtime._num_polar);
  TrackGenerator3D track_generator(geometry, runtime._num_azim, runtime._num_polar, runtime._azim_spacing,
                                   runtime._polar_spacing);
  track_generator.setNumThreads(num_threads);
  track_generator.setQuadrature(quad);
  track_generator.setSegmentFormation((segmentationType)runtime._segmentation_type);
  std::vector<FP_PRECISION> seg_zones {-32.13, -10.71, 10.71, 32.13};
  track_generator.setSegmentationZones(seg_zones);
  track_generator.generateTracks();

  /* Run simulation */
  CPULSSolver solver(&track_generator);
  //solver.useExponentialIntrinsic();
  solver.setVerboseIterationReport();
  solver.setNumThreads(num_threads);
  solver.setConvergenceThreshold(runtime._tolerance);
  solver.computeEigenvalue(runtime._max_iters, 
                           (residualType)runtime._MOC_src_residual_type);
  solver.printTimerReport();

  /* Extract reaction rates */
  Mesh mesh(&solver);
  mesh.createLattice(runtime._NOx, runtime._NOy, runtime._NOz);
  Vector3D rx_rates = mesh.getFormattedReactionRates(FISSION_RX);

  int my_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  if (my_rank == 0) {
    for (int k=0; k < rx_rates.at(0).at(0).size(); k++) {  //rx_rates.at(0).at(0).size()
      //std::cout << " -------- z = " << k << " ----------" << std::endl;
      for (int j=rx_rates.at(0).size()-1; j >= 0 ; j--) {
        for (int i=0; i < rx_rates.size(); i++) {
          std::cout << rx_rates.at(i).at(j).at(k) << " ";
        }
        std::cout << std::endl;
      }
    }
  }

  log_printf(TITLE, "Finished");
#ifdef MPIx
  MPI_Finalize();
#endif
  return 0;
}
/*
C5G7 rodded B refined parameters.
-debug 0 -ndx 3 -ndy 3 -ndz 15 -nmx 1 -nmy 1 -nmz 1 -ncx 51 -ncy 51 -ncz 45 -nox 51 -noy 51 -noz 9 \
-num_threads 1 -azim_spacing 0.05 -num_azim 64 -polar_spacing 0.75 -num_polar 14 -tolerance 1.0e-5 \
-max_iters 40 -log_level DEBUG -knearest 1 -CMFD_flux_update_on 1 -CMFD_centroid_update_on 1 \
-use_axial_interpolation 0 -log_filename log_binbin.log
*/
/*
test.problem
-debug 0 -ndx 2 -ndy 2 -ndz 2 -nmx 1 -nmy 1 -nmz 1 -ncx 2 -ncy 2 -ncz 2 -nox 2 -noy 2 -noz 1 \
-num_threads 1 -azim_spacing 0.05 -num_azim 64 -polar_spacing 0.75 -num_polar 10 -tolerance 1.0e-5 \
-max_iters 100 -log_level NORMAL -knearest 1 -CMFD_flux_update_on 1 -CMFD_centroid_update_on 0 \
-use_axial_interpolation 0 -geo_file_name non-uniform-lattice.geo \
-widths_x 0.05,1.26,1.26,0.05 \
-widths_y 0.05,1.26,1.26,0.05 \
-widths_z 1.0,0.25,1.25 \
-log_filename log_binbin

*/

