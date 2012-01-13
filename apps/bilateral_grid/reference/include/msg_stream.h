/*! \file Msg_stream

\verbatim

Copyright (c) 2004, Sylvain Paris and Francois Sillion
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.

    * Neither the name of ARTIS, GRAVIR-IMAG nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

\endverbatim

 *  This file contains code made by Sylvain Paris under supervision of
 * François Sillion for his PhD work with <a
 * href="http://www-artis.imag.fr">ARTIS project</a>. ARTIS is a
 * research project in the GRAVIR/IMAG laboratory, a joint unit of
 * CNRS, INPG, INRIA and UJF.
 *
 *  Use <a href="http://www.stack.nl/~dimitri/doxygen/">Doxygen</a>
 * with DISTRIBUTE_GROUP_DOC option to produce an nice html
 * documentation.
 *
 *  This file defines streams for error messages: they display an
 * header and potentially exit the application after the message. They
 * can also be used as normal STL streams with (\e endl,\e flush,...).
 *
 *  The two streams \e error and \e warning are defined by default.
 *
 *  For instance the following code displays "fatal error : i has a
 *  wrong value (1)" and then exits.
  \code
  int i = 1;
  Message::error<<"i has a wrong value  ("<<i<<")"<<Message::done;
  \endcode 
 */


#ifndef __MSG_STREAM__
#define __MSG_STREAM__


#include <iostream>
#include <string>

namespace Message{


  
class Warning_stream;

//! Fonction to notify the end of a message.
inline Warning_stream& done(Warning_stream& w);


//! Class defining an ouput for warnings and errors.
class Warning_stream{
    
public:
  //! Classical constructor.
  inline Warning_stream(const char* head,
			const bool fatal=false,
			std::ostream* const output=&std::cerr);

  //! Classical operator.
  template<typename T>
  inline Warning_stream& operator<<(const T& to_print);

  //! Specialized version for the function pointers.
  inline Warning_stream&
  operator<<(Warning_stream& (*f)(Warning_stream&));

  inline Warning_stream&
  operator<<(std::ostream& (*f)(std::ostream&));

  //! Classical destructor.
  inline virtual ~Warning_stream();

  friend Warning_stream& done(Warning_stream& w);

  
private:
  //! If true, done() exits.
  bool is_fatal;

  //! If true, displays the header at the message beginning.
  bool output_header;

  //! Header of the messages.
  std::string header;

  //! Flux de sortie.
  std::ostream* const out;
};



/*
  
  ########################
  # Predefined variables #
  ########################

*/

namespace {
  Warning_stream warning("warning : ",false,&std::cerr);
  Warning_stream error("fatal error : ",true,&std::cerr);
}


  
  
/*

  ##########
  # Macros #
  ##########

 */


#define VALUE_OF(var) #var<<" = "<<var
#define WHERE "file: "<<__FILE__<<"   line: "<<__LINE__





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







Warning_stream::Warning_stream(const char* head,
			       const bool fatal,
			       std::ostream* const output)
  :is_fatal(fatal),
   output_header(true),
   header(head),
   out(output) {}

Warning_stream&
Warning_stream::operator<<(Warning_stream& (*f)(Warning_stream&)){
  return f(*this);
}


Warning_stream&
Warning_stream::operator<<(std::ostream& (*f)(std::ostream&)){
  
  f(*out);
  return *this;
}


template<typename T>
Warning_stream&
Warning_stream::operator<<(const T& to_print){
  if (output_header){
    (*out)<<"\n"<<header;
    output_header = false;
  }
  
  (*out)<<to_print;

  return *this;
}


/*!
  A message looks like:
  \code
  wout<<"Mon message"<<done;
  \endcode
 */
Warning_stream& done(Warning_stream& w){
  *(w.out)<<"\n";
  w.out->flush();
  
  if (w.is_fatal){
    exit(1);
  }

  w.output_header = true;

  return w;
}


Warning_stream::~Warning_stream(){}

} // END OF namespace Message


#endif
