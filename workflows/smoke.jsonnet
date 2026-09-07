local simulation = true;
{
  driver: {
    cpp: 'generate_layers',
    layers: {
      spill: { parent: 'job', total: 10 },
    },
  },

  sources: {
    rntuple_source: {
      cpp: 'read_digitised_hits',
      input_file: 'smoke_input.root',
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
    upstream_tagger_reco: {
        cpp: 'upstream_tagger_reco',
        layer: 'spill'
    },
    surround_tagger_reco: {
        cpp: 'surround_tagger_reco',
        layer: 'spill'
    },
    calorimeter_reco: {
        cpp: 'calorimeter_reco',
        layer: 'spill'
    },
    timing_detector_reco: {
        cpp: 'timing_detector_reco',
        layer: 'spill'
    },
    rec_output: {
      cpp: 'rec_output_module',
      creator: 'fit_seed',
      layer: 'seed',
      rntuple_file: 'smoke_reco_output.root',
      histo_file: 'smoke_reco_validation.root',
      simulation: simulation
    },
  },
}
