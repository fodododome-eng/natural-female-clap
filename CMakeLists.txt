cmake_minimum_required(VERSION 3.22)
project(NaturalFemaleCLAP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG 1.2.2
)
FetchContent_MakeAvailable(clap)

add_library(natural_female SHARED src/natural_female.cpp)

target_include_directories(natural_female PRIVATE
    ${clap_SOURCE_DIR}/include
)

target_compile_definitions(natural_female PRIVATE
    CLAP_WRAPPER_NAME="Natural Female Formant"
)

if(ANDROID)
    target_link_libraries(natural_female PRIVATE log)
endif()

set_target_properties(natural_female PROPERTIES
    PREFIX ""
    OUTPUT_NAME "natural_female"
    SUFFIX ".clap"
)
