{
  driver: {
    cpp: 'generate_layers',
    layers: {
      // Must match the number of entries in your RNTuple
      spill: { parent: 'job', total: 100},
    },
  },

  sources: {
    rntuple_source: {
      cpp: 'read_sim_file',
      input_file:  'fixed_target_mt_output.root',      // the file written by form_module
      ntuple_name: 'events',            // the RNTuple name inside the file
      field_name:  'sim_particles',  // the field/suffix to read
      layer:       'spill'
     },
  },

  modules: {
    passthrough: {
      cpp: 'passthrough',
      layer: 'spill',
    },

    output: {
      cpp: 'form_module',
      products: ['processed_value'],   // re-saves under the new creator
    },
  },
}
