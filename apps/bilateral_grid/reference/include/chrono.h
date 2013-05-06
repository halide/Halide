/*! \file
  \verbatim
  
    Copyright (c) 2006, Sylvain Paris and Frédo Durand

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

  \endverbatim
*/



#ifndef __CHRONO__
#define __CHRONO__

#include <ctime>

#include <iostream>
#include <sstream>
#include <string>

#include "msg_stream.h"

class Chrono{

public:
  inline Chrono(const std::string t=std::string());

  inline void start();
  inline void stop();
  inline void reset();

  inline std::string report();
  inline std::string time_report();

  inline float time_in_seconds();
  
  inline ~Chrono();

private:
  const std::string title;

  bool         is_started;
  bool         reported;
  std::clock_t start_clock;
  std::clock_t cumulative_clock;
  unsigned int n_starts;
};



/*
  
  #############################################
  #############################################
  #############################################
  ######                                 ######
  ######   I M P L E M E N T A T I O N   ######
  ######                                 ######
  #############################################
  #############################################
  #############################################
  
*/


Chrono::Chrono(const std::string t):
  title(t),
  is_started(false),
  reported(false),
  start_clock(0),
  cumulative_clock(0),
  n_starts(0){

}


void Chrono::start(){
  
  if (is_started){
    Message::warning<<"Chrono '"<<title<<"' is already started. Nothing done."<<Message::done;
    return;
  }

  is_started = true;
  n_starts++;
  start_clock = std::clock();
}


void Chrono::stop(){
  if (!is_started){
    Message::warning<<"Chrono '"<<title<<"' is not started. Nothing done."<<Message::done;
    return;
  }

  cumulative_clock += std::clock() - start_clock;
  is_started = false;
}


void Chrono::reset(){
  if (is_started){
    Message::warning<<"Chrono '"<<title<<"' is started during reset request.\n Only reset cumulative time."<<Message::done;
    return;
  }

  cumulative_clock = 0;
}


std::string Chrono::report(){
  if (is_started){
    Message::warning<<"Chrono '"<<title<<"' is started.\n Cannot provide a report."<<Message::done;
    return std::string();
  }

  std::ostringstream msg;
  msg<<"["<<title<<"] cumulative time: "<<(static_cast<float>(cumulative_clock)/CLOCKS_PER_SEC)
     <<"s\t#run: "<<n_starts<<"\taverage time: "
     <<(static_cast<float>(cumulative_clock)/CLOCKS_PER_SEC/n_starts)<<"s";
  reported = true;
  return msg.str();
}


std::string Chrono::time_report(){
  if (is_started){
    Message::warning<<"Chrono '"<<title<<"' is started.\n Cannot provide a time report."<<Message::done;
    return std::string();
  }

  std::ostringstream msg;
  msg<<(static_cast<float>(cumulative_clock)/CLOCKS_PER_SEC);
  reported = true;
  return msg.str();
}


float Chrono::time_in_seconds(){

  if (is_started){
    Message::warning<<"Chrono '"<<title<<"' is started.\n Cannot provide a time measure."<<Message::done;
  }

  reported = true;

  return static_cast<float>(cumulative_clock) / CLOCKS_PER_SEC;
}


Chrono::~Chrono(){
  if (is_started){
    Message::warning<<"Chrono '"<<title<<"' is started and is being destroyed."<<Message::done;
    stop();
  }

  if (!reported){
    Message::warning<<"Chrono '"<<title<<"' is destroyed without having given its result.\n"
		    <<report()<<Message::done;
    
  }
}



#endif
