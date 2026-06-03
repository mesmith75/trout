cmake_minimum_required(VERSION 3.21)
project(trout LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(SHiPDataModel REQUIRED)

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
find_package(Geant4 REQUIRED)

# Pythia8 has no CMake config; create an imported target from pythia8-config
find_program(PYTHIA8_CONFIG pythia8-config REQUIRED)
execute_process(
    COMMAND ${PYTHIA8_CONFIG} --prefix
    OUTPUT_VARIABLE PYTHIA8_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_library(Pythia8::Pythia8 SHARED IMPORTED)
set_target_properties(
    Pythia8::Pythia8
    PROPERTIES
        IMPORTED_LOCATION
            "${PYTHIA8_PREFIX}/lib/libpythia8${CMAKE_SHARED_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${PYTHIA8_PREFIX}/include"
)

find_path(Random123_INCLUDE_DIR Random123/philox.h REQUIRED)

# Sim file reader plugin
add_library(read_sim_file MODULE src/read_sim_file.cpp)
target_link_libraries(
    read_sim_file
    PRIVATE phlex::module SHiP::SHiPDataModel
)

# Passthrough plugin
add_library(passthrough MODULE src/passthrough.cpp)
target_link_libraries(
    passthrough
    PRIVATE phlex::module SHiP::SHiPDataModel
)

# Reconstruction output module plugin
add_library(rec_output_module MODULE src/rec_output_module.cpp)
target_link_libraries(
    rec_output_module
    PRIVATE
        phlex::module
        SHiP::SHiPDataModel
        ROOT::Hist
        ROOT::RIO
        ROOT::ROOTNTuple
        Boost::headers
)

# Install
include(GNUInstallDirs)

set(PLUGIN_TARGETS
    read_sim_file 
    passthrough
    rec_output_module
)

install(TARGETS ${PLUGIN_TARGETS} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS pythia8_benchmark RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY workflows/ DESTINATION share/trout/workflows)
