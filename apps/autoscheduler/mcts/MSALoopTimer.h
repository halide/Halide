#pragma once

#include <chrono>
#include <ctime>
#include <iostream>
#include <thread>
#include <cstdlib>


namespace msa {

    //template <class Clock>	// template doesn't work for some reason, reverting to typedef
    class LoopTimer {
        typedef std::chrono::high_resolution_clock Clock;
        typedef std::chrono::microseconds Units;
    public:
        bool verbose;

        Clock::time_point start_time;
        Clock::time_point loop_start_time;

        Units avg_loop_duration;
        Units run_duration;

        LoopTimer():verbose(false) {}

        //--------------------------------------------------------------
        // initialize timer. Call before the loop starts
        void init() {
            start_time = Clock::now();
            iterations = 0;
        }

        //--------------------------------------------------------------
        // indicate start of loop
        void loop_start() {
            loop_start_time = Clock::now();
            iterations++;
        }

        //--------------------------------------------------------------
        // indicate end of loop
        void loop_end() {
            auto loop_end_time = Clock::now();
            auto current_loop_duration = std::chrono::duration_cast<Units>(loop_end_time - loop_start_time);

            run_duration = std::chrono::duration_cast<Units>(loop_end_time - start_time);
            avg_loop_duration = std::chrono::duration_cast<Units>(run_duration/iterations);

            if(verbose) {
                std::cout << iterations << ": ";
                std::cout << "run_duration: " << run_duration.count() << ", ";
                std::cout << "current_loop_duration: " << current_loop_duration.count() << ", ";
                std::cout << "avg_loop_duration: " << avg_loop_duration.count() << ", ";
                std::cout << std::endl;
            }
        }

        //--------------------------------------------------------------
        // check if current total run duration (since init) exceeds max_millis
        bool check_duration(unsigned int max_millis) const {
            // estimate when the next loop will end
            auto next_loop_end_time = Clock::now() + avg_loop_duration;
            return next_loop_end_time > start_time + std::chrono::milliseconds(max_millis);
        }

        //--------------------------------------------------------------
        // return average loop duration
        unsigned int avg_loop_duration_micros() const {
            return std::chrono::duration_cast<Units>(avg_loop_duration).count();
        }

        //--------------------------------------------------------------
        // return current total run duration (since init)
        unsigned int run_duration_micros() const {
            return std::chrono::duration_cast<Units>(run_duration).count();
        }



        //--------------------------------------------------------------
        //--------------------------------------------------------------
        //--------------------------------------------------------------
        // Example usage (and for testing)
        static void test(unsigned int max_millis) {
            LoopTimer timer;
            timer.verbose = true;

            // initialize timer
            timer.init();

            while(true) {
                // indicate start of loop for timer
                timer.loop_start();

                // sleep for a random duration
                int sleep_duration = 50 + rand() % 50;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration));

                // indicate end of loop for timer
                timer.loop_end();

                // exit loop if current total run duration (since init) exceeds max_millis
                if(timer.check_duration(max_millis)) break;
            }
            std::cout << "total run time: " << timer.run_duration_micros() << ", ";
            std::cout << "avg_loop_duration: " << timer.avg_loop_duration_micros() << ", ";
            std::cout << std::endl;
        }

    private:
        unsigned int iterations;
    };




}
