pybind11_add_module(halide_py
                    PyArgument.cpp            
                    PyBoundaryConditions.cpp  
                    PyBuffer.cpp              
                    PyConciseCasts.cpp        
                    PyDerivative.cpp          
                    PyEnums.cpp               
                    PyError.cpp               
                    PyExpr.cpp                
                    PyExternFuncArgument.cpp  
                    PyFunc.cpp                
                    PyFuncRef.cpp             
                    PyHalide.cpp              
                    PyIROperator.cpp        
                    PyImageParam.cpp        
                    PyInlineReductions.cpp  
                    PyLambda.cpp            
                    PyLoopLevel.cpp         
                    PyMachineParams.cpp     
                    PyModule.cpp    
                    PyParam.cpp     
                    PyPipeline.cpp  
                    PyRDom.cpp      
                    PyStage.cpp
                    PyTarget.cpp
                    PyTuple.cpp
                    PyType.cpp
                    PyVar.cpp
                    PyVarOrRVar.cpp)
set_target_properties(halide_py PROPERTIES LIBRARY_OUTPUT_NAME "halide"
                                           LINK_FLAGS "${HALIDE_PY_LINK_FLAG}")
target_link_libraries(halide_py PRIVATE "${HALIDE_COMPILER_LIB}")
target_include_directories(halide_py PRIVATE "${HALIDE_INCLUDE_DIR}" "${HALIDE_TOOLS_DIR}")
target_compile_options(halide_py PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-cast-qual>
                                         $<$<CXX_COMPILER_ID:GNU>:-Wno-suggest-override>)

install(TARGETS halide_py LIBRARY DESTINATION bin)
