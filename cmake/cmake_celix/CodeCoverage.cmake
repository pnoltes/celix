# Boost Software License - Version 1.0 - August 17th, 2003
# 
# Permission is hereby granted, free of charge, to any person or organization
# obtaining a copy of the software and accompanying documentation covered by
# this license (the "Software") to use, reproduce, display, distribute,
# execute, and transmit the Software, and to prepare derivative works of the
# Software, and to permit third-parties to whom the Software is furnished to
# do so, all subject to the following:
# 
# The copyright notices in the Software and this entire statement, including
# the above license grant, this restriction and the following disclaimer,
# must be included in all copies of the Software, in whole or in part, and
# all derivative works of the Software, unless such copies or derivative
# works are solely in the form of machine-executable object code generated by
# a source language processor.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
# SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
# FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

# - Enable Code Coverage
#
# 2012-01-31, Lars Bilke
#
# USAGE:
# 1. Copy this file into your cmake modules path
# 2. Add the following line to your CMakeLists.txt:
#      INCLUDE(CodeCoverage)
# 
# 3. Use the function SETUP_TARGET_FOR_COVERAGE to create a custom make target
#    which runs your test executable and produces a lcov code coverage report.
#

# - Changes made by Celix
# 1. Added compiler options using --coverage instead of GCC specific strings
# 2. Added custom target to generate HTML pages for combined coverage results
# 3. Added each coverage target to the overall "coverage" target
# 4. Added "mock" to exclude list for coverage results
# 5. Removed HTML generation from the coverage setup function
# 6. Removed unneeded Cobertura function
#

# Option to enable/disable coverage
option(ENABLE_CODE_COVERAGE "Enables code coverage" FALSE)

# Check if coverage is enabled
IF(ENABLE_CODE_COVERAGE)

    # Check prereqs
    FIND_PROGRAM( GCOV_PATH gcov )
    FIND_PROGRAM( LCOV_PATH lcov )
    FIND_PROGRAM( GENHTML_PATH genhtml )
    FIND_PROGRAM( GCOVR_PATH gcovr PATHS ${CMAKE_SOURCE_DIR}/tests)
    
    IF(NOT GCOV_PATH)
    	MESSAGE(FATAL_ERROR "gcov not found! Aborting...")
    ENDIF() # NOT GCOV_PATH
    
    #IF(NOT CMAKE_COMPILER_IS_GNUCXX)
    #	MESSAGE(FATAL_ERROR "Compiler is not GNU gcc! Aborting...")
    #ENDIF() # NOT CMAKE_COMPILER_IS_GNUCXX
    
    IF ( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
      MESSAGE( WARNING "Code coverage results with an optimised (non-Debug) build may be misleading" )
    ENDIF() # NOT CMAKE_BUILD_TYPE STREQUAL "Debug"
    
    # Setup compiler options
    ADD_DEFINITIONS(--coverage)
    set(CMAKE_SHARED_LINKER_FLAGS "--coverage")
    set(CMAKE_EXE_LINKER_FLAGS "--coverage")
    
    IF(NOT TARGET coverage)
        add_custom_target(coverage
    	    COMMAND ${CMAKE_COMMAND} -E make_directory coverage_results
    	    COMMAND ${GENHTML_PATH} -o coverage_results coverage/*.info.cleaned
    	    COMMAND ${CMAKE_COMMAND} -E remove_directory coverage
    	
    	    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    	    COMMENT "Generating report.\nOpen ./${_outputname}/index.html in your browser to view the coverage report."
        )
    
        SET_TARGET_PROPERTIES(coverage PROPERTIES COVERAGE_TARGET_ADDED "")
    ENDIF()
    
ENDIF(ENABLE_CODE_COVERAGE)

# Param _targetname     The name of new the custom make target
# Param _testrunner     The name of the target which runs the tests
# Param _outputname     lcov output is generated as _outputname.info
#                       HTML report is generated in _outputname/index.html
# Optional fourth parameter is passed as arguments to _testrunner
#   Pass them in list form, e.g.: "-j;2" for -j 2
FUNCTION(SETUP_TARGET_FOR_COVERAGE _targetname _testrunner _outputname)
    IF(ENABLE_CODE_COVERAGE)
    	IF(NOT LCOV_PATH)
    		MESSAGE(FATAL_ERROR "lcov not found! Aborting...")
    	ENDIF() # NOT LCOV_PATH
    
    	IF(NOT GENHTML_PATH)
    		MESSAGE(FATAL_ERROR "genhtml not found! Aborting...")
    	ENDIF() # NOT GENHTML_PATH
    
    	# Setup target
    	ADD_CUSTOM_TARGET(${_targetname}
    		
    		# Cleanup lcov
    		${LCOV_PATH} --directory . --zerocounters
    		
    		# Run tests
    		COMMAND ${_testrunner} ${ARGV3}
    		
    		# Capturing lcov counters and generating report
    		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
    		COMMAND ${LCOV_PATH} --directory . --capture --output-file ${_outputname}.info
    		COMMAND ${LCOV_PATH} --remove ${_outputname}.info 'mock/*' 'test/*' '/usr/*' --output-file ${_outputname}.info.cleaned
    		
    		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    		COMMENT "Resetting code coverage counters to zero.\nProcessing code coverage counters and generating report."
    	)
    	ADD_DEPENDENCIES(coverage ${_targetname})
    ENDIF(ENABLE_CODE_COVERAGE)
ENDFUNCTION() # SETUP_TARGET_FOR_COVERAGE