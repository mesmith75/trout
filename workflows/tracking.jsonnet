local simulation = true;
{
  driver: {
    cpp: 'generate_layers',
    layers: {
      spill: { parent: 'job', total: 1},
    },
  },

  sources: {
    rntuple_source: {
      cpp: 'read_digitised_hits',
      #input_file:  '/vols/ship/masmith/stack/fixed_target_mt_output.root',
      input_file: '../shannon/digitised_hits_time.root',
      ntuple_name: 'straw_tubes_hits',
      field_name:  'hit',
      layer:       'spill',
      simulation: simulation
     },
    tracking_geometry: {
      cpp: 'acts_geometry_provider',
      db_file: '../geometry/my_ship_geometry.db',
    },
    spectrometer_field: {
      cpp: 'acts_field_provider',
      field_file: 'spectromoter_field.cvf',
    },
  },

  modules: {
     spectrometer_tracking: {
         cpp: 'spectrometer_tracking',
         layer: 'spill',
     },
    rec_output: {
      cpp: 'rec_output_module',
      creator: 'fit_seed',
      layer: 'seed',
      rntuple_file: 'track_fit_results.root',
      simulation: simulation
    },
  },
}
