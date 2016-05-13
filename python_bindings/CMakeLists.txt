
project(PyHalide)
cmake_minimum_required(VERSION 3.0) # for up-to-date FindPythonLibs.cmake
set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR} )

# Find Python and boost python
if(USE_PYTHON EQUAL 2)
	find_package(PythonInterp 2.7 REQUIRED)
	find_package(PythonLibs 2.7 REQUIRED)
	find_package(NumPy REQUIRED)
	if (NOT USE_EXTERNAL_BOOST)
		find_package(Boost COMPONENTS python REQUIRED)
	endif()
	find_package(Halide REQUIRED)
else()
	find_package(PythonInterp 3.4 REQUIRED)
	find_package(PythonLibs 3.4 REQUIRED)
	find_package(NumPy REQUIRED)
	if (NOT USE_EXTERNAL_BOOST)
		string( REGEX REPLACE "([0-9]+).([0-9]+).[0-9.]+" "\\1\\2" PYTHON_CONCAT_VERSION_STRING ${PYTHON_VERSION_STRING} )
		find_package(Boost COMPONENTS python-py${PYTHON_CONCAT_VERSION_STRING})
		if(NOT Boost_FOUND)
			find_package(Boost COMPONENTS python3 REQUIRED)
		endif()
	endif()
	find_package(Halide REQUIRED)
endif()

option(USE_BOOST_NUMPY "Use Boost.Numpy dependency instead of Halide.Numpy" OFF)


if(USE_BOOST_NUMPY)
add_definitions( -DUSE_BOOST_NUMPY )
set(BoostNumpy_INCLUDE_DIRS "")
set(BoostNumpy_LIBRARY_DIR "")
set(BoostNumpy_LIBRARIES boost_numpy)
else()
#(NOT USE_BOOST_NUMPY) is true

file(GLOB MicroNumpyCpp
numpy/*.cpp
numpy/*.hpp
)

add_library(halide_numpy STATIC ${MicroNumpyCpp})
target_link_libraries(halide_numpy ${Boost_LIBRARIES} ${PYTHON_LIBRARIES} ${NumPy_LIBRARIES})
set_property(TARGET halide_numpy PROPERTY POSITION_INDEPENDENT_CODE TRUE)

set(BoostNumpy_INCLUDE_DIRS ./numpy)
set(BoostNumpy_LIBRARY_DIR ./numpy)
set(BoostNumpy_LIBRARIES halide_numpy)
endif()

add_definitions("-std=c++11")

include_directories(
  ${HALIDE_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${BoostNumpy_INCLUDE_DIRS}
  ${PYTHON_INCLUDE_DIRS}
  ${NUMPY_INCLUDE_DIRS}
)

link_directories(
  ../build/lib
  ${HALIDE_ROOT_DIR}/lib
  ${Boost_LIBRARY_DIRS}
  ${BoostNumpy_LIBRARY_DIR}
  ${Numpy_LIBRARY_DIR}
)


if(FALSE AND UNIX )
	# Disable the pointer-to-function and pointer-to-object warnings
	add_definitions( -Wno-unused-local-typedefs )
	set_property(SOURCE util.cpp PROPERTY COMPILE_FLAGS -w )
endif()

file(GLOB SrcCpp
python/*.cpp
../src/OutputImageParam.cpp
../src/ImageParam.cpp)

add_library(halide SHARED ${SrcCpp})
target_link_libraries(halide
  ${HALIDE_LIBRARIES}
  ${Boost_LIBRARIES}
  ${BoostNumpy_LIBRARIES}
  ${PYTHON_LIBRARIES}
)

set_target_properties( halide PROPERTIES PREFIX "")
if(APPLE)
set_target_properties( halide PROPERTIES SUFFIX ".so" )
endif()
