cmake_minimum_required(VERSION 3.12)
project(raft-p2p)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_AUTOMOC ON)
find_package( Qt5Core REQUIRED )
find_package( Qt5Widgets REQUIRED )
find_package( Qt5Gui REQUIRED )

include_directories(.)

add_executable(raft-p2p
        main.cc
        main.hh
        raft-p2p.pro)

qt5_use_modules( raft-p2p Core Widgets Gui Network)