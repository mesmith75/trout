cmake_minimum_required(VERSION 3.21)
project(trout VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure plugin modules retain DT_NEEDED entries for all linked libraries,
# even if no symbols are directly referenced at link time (e.g. ROOT
# dictionary-only dependencies like SHiPDataModel's collection proxies).
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-as-needed")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-as-needed")

configure_file(TroutConfig.h.in TroutConfig.h)

# Dependencies
find_package(SHiPGeometry CONFIG REQUIRED)
find_package(SHiPGeometryService 0.3 CONFIG REQUIRED)
find_package(SHiPDataModel REQUIRED)
find_package(SHiPFieldService REQUIRED)
find_package(Acts CONFIG REQUIRED)

# ---- Covfie/runtime fingerprint ----
set(COVFIE_FINGERPRINT "unknown" CACHE STRING "Manual covfie/build fingerprint")

# Try to enrich with git info from current project checkout
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_git_sha)
        set(COVFIE_FINGERPRINT "${COVFIE_FINGERPRINT}|proj:${_git_sha}")
    endif()
endif()

# Phlex's cmake config does not call find_dependency for its transitive deps
find_package(Boost REQUIRED COMPONENTS json)
find_package(TBB REQUIRED)
find_package(spdlog REQUIRED)
find_package(phlex REQUIRED)
find_package(
    ROOT
    REQUIRED
    COMPONENTS Core RIO Hist MathCore ROOTNTuple
)

# Digitised (flat, record-per-hit) file reader plugin
add_library(read_digitised_hits MODULE src/io/read_digitised_hits.cpp)
target_link_libraries(
    read_digitised_hits
    PRIVATE phlex::module SHiP::SHiPDataModel ROOT::RIO ROOT::ROOTNTuple
)

# ACTS tracking-geometry provider plugin (builds Acts::TrackingGeometry for
# the straw-tube stations from the real SHiP geometry via SHiPGeometryService)
add_library(acts_geometry_provider MODULE src/acts_geometry_provider.cpp)
target_link_libraries(
    acts_geometry_provider
    PRIVATE phlex::module SHiPGeometryService::SHiPGeometryService Acts::Core
)
target_include_directories(
    acts_geometry_provider
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# ACTS magnetic-field provider plugin (loads the spectrometer's covfie field
# map once and shares it as an Acts::MagneticFieldProvider product, the same
# "build once on layer job" pattern as acts_geometry_provider above)
add_library(acts_field_provider MODULE src/acts_field_provider.cpp)
target_link_libraries(
    acts_field_provider
    PRIVATE phlex::module SHiPFieldService::SHiPFieldService Acts::Core
)
target_include_directories(
    acts_field_provider
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Passthrough plugin
add_library(passthrough MODULE src/passthrough.cpp)
target_link_libraries(passthrough PRIVATE phlex::module SHiP::SHiPDataModel)

# IP selection
add_library(ip_selector MODULE src/ip_selector.cpp)
target_link_libraries(ip_selector PRIVATE phlex::module SHiP::SHiPDataModel)

# Pattern recognition + track finding, split into phlex-native
# prepare/unfold/fit stages so per-seed fitting is parallelized by the
# framework (see keep_tracks above for the original single-call-per-spill
# version this is an alternative to). One file per pipeline stage —
# spectrometer_tracking.cpp just wires prepare_measurements/generate_seeds/
# fit_seed together; see its header comment for the stage breakdown.
add_library(
    spectrometer_tracking
    MODULE
    src/detectors/spectrometer/spectrometer_tracking.cpp
    src/detectors/spectrometer/prepare_measurements.cpp
    include/detectors/spectrometer/prepare_measurements.hpp
    src/detectors/spectrometer/generate_seeds.cpp
    include/detectors/spectrometer/generate_seeds.hpp
    src/detectors/spectrometer/fit_seed.cpp
    include/detectors/spectrometer/fit_seed.hpp
    include/PhlexModuleProxy.hpp
    include/detectors/spectrometer/SpillContext.hpp
    include/detectors/spectrometer/SpectrometerCkf.hpp
    include/detectors/spectrometer/SpectrometerMeasurements.hpp
    include/IndexSourceLink.hpp
    include/GeometryContainers.hpp
    include/acts_geometry_provider.hpp
    include/acts_field_provider.hpp
    src/detectors/spectrometer/HitUncertainty.cpp
    include/detectors/spectrometer/HitUncertainty.hpp
)
target_link_libraries(
    spectrometer_tracking
    PRIVATE
        phlex::module
        SHiP::SHiPDataModel
        Acts::Core
        SHiPFieldService::SHiPFieldService
        SHiPFieldService::G4Adapter
        SHiPGeometryService::SHiPGeometryService
)
target_include_directories(
    spectrometer_tracking
    PUBLIC "${PROJECT_BINARY_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(
    upstream_tagger_reco
    MODULE
    src/detectors/upstream_tagger/upstream_tagger_reco.cpp
)
target_link_libraries(
    upstream_tagger_reco
    PRIVATE phlex::module SHiP::SHiPDataModel
)

add_library(
    surround_tagger_reco
    MODULE
    src/detectors/surround_tagger/surround_tagger_reco.cpp
)
target_link_libraries(
    surround_tagger_reco
    PRIVATE phlex::module SHiP::SHiPDataModel
)

add_library(
    calorimeter_reco
    MODULE
    src/detectors/calorimeter/calorimeter_reco.cpp
)
target_link_libraries(
    calorimeter_reco
    PRIVATE phlex::module SHiP::SHiPDataModel
)

add_library(
    timing_detector_reco
    MODULE
    src/detectors/timing_detector/timing_detector_reco.cpp
)
target_link_libraries(
    timing_detector_reco
    PRIVATE phlex::module SHiP::SHiPDataModel
)

# Reconstruction output module plugin
add_library(rec_output_module MODULE src/io/rec_output_module.cpp)
target_include_directories(
    rec_output_module
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    rec_output_module
    PRIVATE
        phlex::module
        SHiP::SHiPDataModel
        ROOT::Hist
        ROOT::RIO
        ROOT::ROOTHistUtil
        ROOT::ROOTNTuple
        Boost::headers
        spdlog::spdlog
)

# Install
include(GNUInstallDirs)

set(PLUGIN_TARGETS
    read_digitised_hits
    passthrough
    ip_selector
    upstream_tagger_reco
    surround_tagger_reco
    calorimeter_reco
    timing_detector_reco
    rec_output_module
)

install(TARGETS ${PLUGIN_TARGETS} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY workflows/ DESTINATION share/trout/workflows)
