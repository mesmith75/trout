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

# Sim file reader plugin
add_library(read_sim_file MODULE src/read_sim_file.cpp)
target_link_libraries(
    read_sim_file
    PRIVATE
        phlex::module
        SHiP::SHiPDataModel
        ROOT::RIO
        ROOT::ROOTNTuple
)

# Passthrough plugin
add_library(passthrough MODULE src/passthrough.cpp)
target_link_libraries(
    passthrough
    PRIVATE phlex::module SHiP::SHiPDataModel
)

# IP selection
add_library(ip_selector MODULE src/ip_selector.cpp)
target_link_libraries(
    ip_selector
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
    ip_selector
    rec_output_module
)

install(TARGETS ${PLUGIN_TARGETS} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY workflows/ DESTINATION share/trout/workflows)
