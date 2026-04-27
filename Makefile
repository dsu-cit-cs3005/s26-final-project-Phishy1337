# Compiler
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pedantic

# Targets
all: test_robot RobotWarz robots

RobotBase.o: RobotBase.cpp RobotBase.h
	$(CXX) $(CXXFLAGS) -fPIC -c RobotBase.cpp

test_robot: test_robot.cpp RobotBase.o
	$(CXX) $(CXXFLAGS) test_robot.cpp RobotBase.o -ldl -o test_robot

RobotWarz: RobotWarz.cpp Arena.cpp RobotBase.o Arena.h
	$(CXX) $(CXXFLAGS) RobotWarz.cpp Arena.cpp RobotBase.o -ldl -o RobotWarz

.PHONY: robots
robots: test_robot
	./test_robot Robot_Flame_e_o.cpp
	./test_robot Robot_Ratboy.cpp
	./test_robot Robot_AAAAAAAAAAAAAHHHHH.cpp

clean:
	rm -f *.o test_robot RobotWarz *.so
