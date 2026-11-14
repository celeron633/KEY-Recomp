// SPDX-License-Identifier: GPL-3.0-only
#include "controller/motion.h"
#include <limits>
#include <stdexcept>
#include <cstdio>
using namespace key::controls;
void check(bool condition) { if (!condition) throw std::runtime_error("Motion contract failed"); }
bool near(float a, float b) { return std::abs(a-b) < .001f; }
int main() {
    const auto flat = sideways_acceleration({0,9.80665f,0},1,false);
    check(near(flat[0],0) && near(flat[1],-1) && near(flat[2],0));
    const auto right = sideways_acceleration({9.80665f,0,0},1,false);
    check(near(right[0],0) && near(right[1],0) && near(right[2],-1));
    const auto left = sideways_acceleration({9.80665f,0,0},1,true);
    check(near(left[2],1));
    const auto forward = sideways_acceleration({0,0,9.80665f},1,false);
    check(near(forward[0],-1) && near(forward[2],0));
    Motion a, b;
    for (int i=0;i<100;++i) a.sample({.2f,-.3f,0},{0,9.80665f,0},.01f,1,0,false,false,true);
    for (int i=0;i<200;++i) b.sample({.2f,-.3f,0},{0,9.80665f,0},.005f,1,0,false,false,true);
    check(near(a.x,-.3f) && near(a.y,.2f) && near(a.x,b.x) && near(a.y,b.y));
    a.sample({100,100,0},{0,9.80665f,0},10,1,0,false,false,true);
    check(near(a.x,-.3f)); // A host stall must not integrate seconds of stale gyro data.
    a.sample({std::numeric_limits<float>::quiet_NaN(),0,0},{0,9.80665f,0},.01f,1,0,false,false,true);
    check(near(a.x,-.3f));
    a.recenter(); check(a.x==0 && a.y==0);
    a.sample({.2f,-.3f,0},{0,9.80665f,0},.01f,1,0,true,true,true);
    check(near(a.x,.003f) && near(a.y,-.002f));
    a.calibrate();
    for(int i=0;i<100;++i) a.sample({.01f,.02f,0},{0,9.80665f,0},.01f,1,0,false,false,true);
    a.sample({1,0,0},{0,9.80665f,0},.01f,1,0,false,false,true);
    check(a.calibrating && a.calibration_seconds==0);
    for(int i=0;i<210;++i) a.sample({.01f,.02f,0},{0,9.80665f,0},.01f,1,.001f,false,false,true);
    check(a.calibrated && !a.calibrating && near(a.bias[0],.01f) && near(a.bias[1],.02f));
    check(near(a.x,0) && near(a.y,0));
    a.sample({20,-20,0},{0,9.80665f,0},.1f,5,0,false,false,true);
    check(a.x==-1 && a.y==1);
    std::puts("PASS: sideways gravity axes, gyro time integration, inversion, bounds, invalid samples, stall rejection and stationary bias calibration");
}
