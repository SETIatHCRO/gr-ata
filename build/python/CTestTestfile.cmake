# CMake generated Testfile for 
# Source directory: /home/ewhite/src/gr-ata/python
# Build directory: /home/ewhite/src/gr-ata/build/python
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(qa_control "/usr/bin/sh" "/home/ewhite/src/gr-ata/build/python/qa_control_test.sh")
set_tests_properties(qa_control PROPERTIES  _BACKTRACE_TRIPLES "/opt/lib/cmake/gnuradio/GrTest.cmake;110;add_test;/home/ewhite/src/gr-ata/python/CMakeLists.txt;48;GR_ADD_TEST;/home/ewhite/src/gr-ata/python/CMakeLists.txt;0;")
add_test(qa_trackscan "/usr/bin/sh" "/home/ewhite/src/gr-ata/build/python/qa_trackscan_test.sh")
set_tests_properties(qa_trackscan PROPERTIES  _BACKTRACE_TRIPLES "/opt/lib/cmake/gnuradio/GrTest.cmake;110;add_test;/home/ewhite/src/gr-ata/python/CMakeLists.txt;49;GR_ADD_TEST;/home/ewhite/src/gr-ata/python/CMakeLists.txt;0;")
add_test(qa_onoff "/usr/bin/sh" "/home/ewhite/src/gr-ata/build/python/qa_onoff_test.sh")
set_tests_properties(qa_onoff PROPERTIES  _BACKTRACE_TRIPLES "/opt/lib/cmake/gnuradio/GrTest.cmake;110;add_test;/home/ewhite/src/gr-ata/python/CMakeLists.txt;50;GR_ADD_TEST;/home/ewhite/src/gr-ata/python/CMakeLists.txt;0;")
add_test(qa_ifswitch "/usr/bin/sh" "/home/ewhite/src/gr-ata/build/python/qa_ifswitch_test.sh")
set_tests_properties(qa_ifswitch PROPERTIES  _BACKTRACE_TRIPLES "/opt/lib/cmake/gnuradio/GrTest.cmake;110;add_test;/home/ewhite/src/gr-ata/python/CMakeLists.txt;51;GR_ADD_TEST;/home/ewhite/src/gr-ata/python/CMakeLists.txt;0;")
